#!/usr/bin/env python3
"""
TcpServer 压力测试客户端
用法: python3 stress_test.py <server_ip> <server_port> [并发数] [总请求数]
示例: python3 stress_test.py 127.0.0.1 8080 100 10000
"""

import asyncio
import time
import random
import sys

SERVER_IP = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
SERVER_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
CONCURRENCY = int(sys.argv[3]) if len(sys.argv) > 3 else 100
TOTAL_REQUESTS = int(sys.argv[4]) if len(sys.argv) > 4 else 10000

# ---------- 协议编解码（与 C++ 端对齐） ----------
SEP = " "
LINE_SEP = "\r\n"

def encode_request(x, y, op):
    """构造一个完整的协议包: len\r\n x op y\r\n"""
    text = f"{x}{SEP}{op}{SEP}{y}"
    length = len(text)
    return f"{length}{LINE_SEP}{text}{LINE_SEP}".encode()

def decode_response(data):
    """从返回数据中解析 result 和 exitcode"""
    pos = data.find(LINE_SEP.encode())
    if pos == -1:
        return None
    length = int(data[:pos])
    body = data[pos + len(LINE_SEP): pos + len(LINE_SEP) + length]
    parts = body.decode().split(SEP)
    if len(parts) != 2:
        return None
    return int(parts[0]), int(parts[1])  # result, exitcode

async def send_one_request(reader, writer, x, y, op):
    """发送一个请求并等待响应，返回延迟（毫秒）"""
    pkg = encode_request(x, y, op)
    start = time.perf_counter()
    writer.write(pkg)
    await writer.drain()

    # 兼容 Python 3.6：手动按字节读取长度行，不依赖 readuntil
    len_line = b""
    while True:
        ch = await reader.read(1)
        if not ch:
            raise asyncio.IncompleteReadError(b"", None)
        if ch == b"\n":
            break
        len_line += ch
    # len_line 形如 b"5\r"，去掉可能的 \r
    len_line = len_line.rstrip(b"\r")
    length = int(len_line)
    # 读正文 + 尾部 \r\n
    body = await reader.readexactly(length + len(LINE_SEP))
    elapsed = (time.perf_counter() - start) * 1000

    result, exitcode = decode_response(len_line + b"\r\n" + body)
    return elapsed, exitcode

async def worker(server_ip, server_port, requests_per_worker, stats):
    """单个并发 worker"""
    try:
        reader, writer = await asyncio.open_connection(server_ip, server_port)
    except Exception as e:
        stats["connect_fail"] += requests_per_worker
        return

    for _ in range(requests_per_worker):
        x = random.randint(1, 1000)
        y = random.randint(1, 1000)
        op = random.choice(["+", "-", "*", "/", "%"])
        try:
            elapsed, exitcode = await send_one_request(reader, writer, x, y, op)
            stats["success"] += 1
            stats["latencies"].append(elapsed)
            if exitcode != 0:
                stats["biz_error"] += 1
        except asyncio.IncompleteReadError:
            stats["conn_closed"] += 1
            # 重连
            try:
                reader, writer = await asyncio.open_connection(server_ip, server_port)
            except:
                stats["connect_fail"] += 1
                break
        except Exception as e:
            stats["fail"] += 1

    try:
        writer.close()
        # Python 3.6 没有 wait_closed，直接跳过
        if sys.version_info >= (3, 7):
            await writer.wait_closed()
    except:
        pass

async def main():
    print(f"{'='*50}")
    print(f"  压力测试配置")
    print(f"  目标: {SERVER_IP}:{SERVER_PORT}")
    print(f"  并发数: {CONCURRENCY}")
    print(f"  总请求数: {TOTAL_REQUESTS}")
    print(f"{'='*50}\n")

    requests_per_worker = TOTAL_REQUESTS // CONCURRENCY
    remainder = TOTAL_REQUESTS % CONCURRENCY

    stats = {
        "success": 0,
        "fail": 0,
        "biz_error": 0,
        "conn_closed": 0,
        "connect_fail": 0,
        "latencies": [],
    }

    tasks = []
    for i in range(CONCURRENCY):
        count = requests_per_worker + (1 if i < remainder else 0)
        tasks.append(worker(SERVER_IP, SERVER_PORT, count, stats))

    start_time = time.perf_counter()
    await asyncio.gather(*tasks)
    total_time = time.perf_counter() - start_time

    # ---------- 结果统计 ----------
    print(f"\n{'='*50}")
    print(f"  压力测试结果")
    print(f"{'='*50}")
    print(f"  总耗时:       {total_time:.2f} 秒")
    print(f"  总请求数:     {TOTAL_REQUESTS}")
    print(f"  成功数:       {stats['success']}")
    print(f"  失败数:       {stats['fail']}")
    print(f"  连接断开:     {stats['conn_closed']}")
    print(f"  连接失败:     {stats['connect_fail']}")
    print(f"  业务错误:     {stats['biz_error']} (除零/取模零等)")
    if total_time > 0:
        qps = stats["success"] / total_time
        print(f"  QPS:          {qps:.0f} req/s")
    print(f"  成功率:       {stats['success']/TOTAL_REQUESTS*100:.1f}%")

    if stats["latencies"]:
        lats = sorted(stats["latencies"])
        avg = sum(lats) / len(lats)
        p50 = lats[len(lats) // 2]
        p90 = lats[int(len(lats) * 0.9)]
        p99 = lats[int(len(lats) * 0.99)]
        print(f"\n  延迟统计 (ms):")
        print(f"    平均: {avg:.2f}")
        print(f"    P50:  {p50:.2f}")
        print(f"    P90:  {p90:.2f}")
        print(f"    P99:  {p99:.2f}")
        print(f"    最小: {lats[0]:.2f}")
        print(f"    最大: {lats[-1]:.2f}")
    print(f"{'='*50}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("用法: python3 stress_test.py <server_ip> <server_port> [并发数] [总请求数]")
        print("示例: python3 stress_test.py 127.0.0.1 8080 100 10000")
        sys.exit(1)
    # 兼容 Python 3.6：asyncio.run() 只在 3.7+ 存在
    if sys.version_info >= (3, 7):
        asyncio.run(main())
    else:
        loop = asyncio.get_event_loop()
        try:
            loop.run_until_complete(main())
        finally:
            loop.close()

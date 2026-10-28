#include "TcpServer.hpp"
#include "HttpProtocol.hpp"
#include <unistd.h>
#include <memory>

using namespace tcpS_ns;

static void usage(std::string proc)
{
    std::cerr << "Usage:\n\t" << proc << " port"
              << "\n\n";
}
// 静态文件根目录，所有http请求都在这个目录下找文件
static const std::string ROOT_DIR = "./www";

void handleHttp(Connection *conn)
{
    HttpRequest req;
    // 循环解析：支持HTTP流水线（一个连接连续多个请求）
    while (req.Parse(conn->inbuffer))
    {
        HttpResponse resp;
        // 只处理GET，其他方法直接返回400
        if (req.method != "GET")
        {
            resp = HttpResponse::BadRequest();
        }
        else
        {
            std::string path = req.path;
            // 访问根目录 / 自动重定向到 index.html
            if (path == "/") path = "/index.html";

            // 简单防目录穿越：路径包含".."直接拒绝
            if (path.find("..") != std::string::npos)
            {
                resp = HttpResponse::BadRequest();
            }
            else
            {
                // 拼接磁盘上完整文件路径 ./www/index.html
                std::string fullPath = ROOT_DIR + path;
                // 二进制模式打开静态文件（图片/html/js）
                std::ifstream ifs(fullPath, std::ios::binary);
                if (!ifs)
                {
                    // 文件打不开 → 404
                    resp = HttpResponse::NotFound();
                }
                else
                {
                    // 一次性读取整个文件到body
                    std::ostringstream oss;
                    oss << ifs.rdbuf();
                    resp.body = oss.str();
                    // 根据后缀获取MIME类型
                    resp.contentType = GetMimeType(path);
                    resp.status = 200;
                    resp.statusText = "OK";
                }
            }
        }

        // 判断是否长连接：看请求头Connection
        auto it = req.headers.find("Connection");
        if (it != req.headers.end() && it->second == "keep-alive")
            resp.keepAlive = true;
        conn->shoutClose = !resp.keepAlive;

        // 把响应报文追加到连接的输出缓冲区，等待发送
        conn->outbuffer += resp.ToString();

        // 如果是短连接，处理完这一个请求就退出循环，关闭连接
        if (!resp.keepAlive)
            break;
    }

    // 有发送回调并且输出缓冲区不为空，调用发送函数把outbuffer发出去
    if (conn->_send && !conn->outbuffer.empty())
        conn->_send(conn);
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        usage(argv[0]);
        exit(USAGE_ERR);
    }
    uint16_t port = atoi(argv[1]);

    std::unique_ptr<TcpServer> tsvr(new TcpServer(handleHttp, port));
    tsvr->initServer();
    tsvr->Dispatcher();

    return 0;
}
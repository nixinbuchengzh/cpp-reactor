#pragma once

#include <iostream>
#include <functional>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <utility>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/eventfd.h>

#include "log.hpp"
#include "sock.hpp"
#include "Epoller.hpp"
#include "Util.hpp"
#include "ThreadPool.h"

static const uint16_t defaultport = 8080;

namespace tcpS_ns
{
    class Connection;
    class TcpServer;

    using func_t = std::function<void(Connection *)>;

    // 业务线程返回给 IO 线程的结果
    struct BusinessResult
    {
        std::string out;             // 要发送的响应字节
        std::string leftover;        // 没解析完的半个请求
        bool closeAfterSend = false; // 响应发完后关闭连接
    };

    using business_t = std::function<BusinessResult(std::string)>;

    class Connection
    {
    public:
        int _fd;
        std::string inbuffer;  // 只由 IO 线程读写
        std::string outbuffer; // 只由 IO 线程读写

        bool shoutClose; // 对端已关闭，响应发完就关
        bool working;    // 是否有业务任务正在线程池里
        bool closed;     // 是否已经被 IO 线程关闭

        func_t _recv;
        func_t _send;
        func_t _excepter;

        TcpServer *tsp;

    public:
        Connection(int sock, TcpServer *p)
            : _fd(sock), shoutClose(false), working(false), closed(false), tsp(p)
        {
        }

        void Register(func_t recv, func_t send, func_t excepter)
        {
            _recv = recv;
            _send = send;
            _excepter = excepter;
        }

        ~Connection()
        {
            Close();
        }

        void Close()
        {
            if (_fd >= 0)
            {
                close(_fd);
                _fd = -1;
            }
        }
    };

    class TcpServer
    {
    private:
        // 业务线程算完后放到这里，通过 eventfd 交还给 IO 线程
        struct WorkerResult
        {
            std::shared_ptr<Connection> conn;
            std::string out;
            std::string leftover;
            size_t consumed = 0;
            bool closeAfterSend = false;
        };

        // 读事件：只负责 recv，然后把数据交给业务线程池
        static void Recver(Connection *conn)
        {
            char buffer[4096];

            for (;;)
            {
                ssize_t s = recv(conn->_fd, buffer, sizeof(buffer), 0);
                if (s > 0)
                {
                    conn->inbuffer.append(buffer, static_cast<size_t>(s));
                    continue;
                }
                else if (s == 0) // 对端关闭
                {
                    conn->shoutClose = true;
                    break;
                }
                else
                {
                    if (errno == EINTR)
                        continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;

                    if (conn->_excepter)
                        conn->_excepter(conn);
                    return;
                }
            }

            if (conn->closed)
                return;

            // 已有任务在处理，等它完成后由 HandleWorkerResults 继续
            if (conn->working)
                return;

            if (!conn->inbuffer.empty())
            {
                SubmitToWorker(conn);
                return;
            }

            // 对端已关，又没有数据要发，直接关闭
            if (conn->shoutClose && conn->outbuffer.empty() && conn->_excepter)
                conn->_excepter(conn);
        }

        // 写事件：只由 IO 线程执行
        static void Sender(Connection *conn)
        {
            // 没有数据可发时不要空转，也不要调用 send(..., 0, ...)
            if (conn->outbuffer.empty())
            {
                if (conn->shoutClose)
                {
                    if (conn->_excepter)
                        conn->_excepter(conn);
                }
                else
                {
                    EnableReadWrite(conn, true, false);
                }
                return;
            }

            while (true)
            {
                ssize_t s = send(conn->_fd, conn->outbuffer.data(),
                                 conn->outbuffer.size(), MSG_NOSIGNAL);
                if (s > 0)
                {
                    conn->outbuffer.erase(0, static_cast<size_t>(s));
                    if (conn->outbuffer.empty())
                        break;
                }
                else if (s == 0)
                {
                    if (conn->_excepter)
                        conn->_excepter(conn);
                    return;
                }
                else
                {
                    if (errno == EINTR)
                        continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        return;

                    if (conn->_excepter)
                        conn->_excepter(conn);
                    return;
                }
            }

            if (conn->shoutClose)
            {
                if (conn->_excepter)
                    conn->_excepter(conn);
                return;
            }

            EnableReadWrite(conn, true, false);
        }

        static void Excepter(Connection *conn)
        {
            if (conn == nullptr || conn->closed)
                return;

            conn->closed = true;

            // 注意：Close() 会把 _fd 置成 -1，所以先保存 fd
            int fd = conn->_fd;
            if (fd < 0)
                return;

            _epoller.Control(fd, 0, EPOLL_CTL_DEL);
            conn->Close();
            mp.erase(fd);

            logMessage(DEBUG, "关闭%d 文件描述符的所有的资源", fd);
        }

        void Accepter(Connection *)
        {
            for (;;)
            {
                std::string clientip;
                uint16_t clientport = 0;
                int err = 0;
                int sock = _sock.Accept(&clientip, &clientport, &err);
                if (sock >= 0)
                {
                    bool ok = AddConnection(
                        sock, EPOLLIN | EPOLLET,
                        std::bind(&TcpServer::Recver, std::placeholders::_1),
                        std::bind(&TcpServer::Sender, std::placeholders::_1),
                        std::bind(&TcpServer::Excepter, std::placeholders::_1));
                    if (ok)
                        logMessage(DEBUG, "get a new link, info: [%s:%d]", clientip.c_str(), clientport);
                }
                else
                {
                    if (err == EAGAIN || err == EWOULDBLOCK)
                        break;
                    if (err == EINTR)
                        continue;
                    break;
                }
            }
        }

        void Loop(int timeout)
        {
            int n = _epoller.Wait(evs, num, timeout);

            for (int i = 0; i < n; i++)
            {
                int sock = evs[i].data.fd;

                // eventfd：业务线程有结果回来了
                if (sock == _wakefd)
                {
                    HandleWorkerResults();
                    continue;
                }

                uint32_t events = evs[i].events;
                if (events & EPOLLERR)
                    events |= (EPOLLIN | EPOLLOUT);
                if (events & EPOLLHUP)
                    events |= (EPOLLIN | EPOLLOUT);

                auto it = mp.find(sock);
                if (it == mp.end() || it->second == nullptr)
                    continue;

                // 先持有一份 shared_ptr，防止回调里删除连接后对象提前析构
                std::shared_ptr<Connection> conn = it->second;

                if ((events & EPOLLIN) && conn->_recv)
                    conn->_recv(conn.get());

                if (events & EPOLLOUT)
                {
                    // recv 回调可能已经关掉连接，所以重新查一次
                    auto it2 = mp.find(sock);
                    if (it2 != mp.end() && it2->second && it2->second->_send)
                        it2->second->_send(it2->second.get());
                }
            }
        }

        bool AddConnection(int sock, uint32_t events, func_t recv, func_t send, func_t excepter)
        {
            util::setNonBlock(sock);

            auto conn = std::make_shared<Connection>(sock, this);
            conn->Register(std::move(recv), std::move(send), std::move(excepter));

            // 先放进连接表，再注册 epoll，避免事件先到但表里还没有连接
            mp[sock] = conn;

            if (!_epoller.AddEvent(sock, events))
            {
                logMessage(ERROR, "AddEvent failed, close sock %d", sock);
                mp.erase(sock);
                conn->Close();
                return false;
            }

            return true;
        }

        // 把 inbuffer 整体搬给业务线程池
        // 同一条连接同一时刻最多一个任务，保证 HTTP 流水线响应顺序
        static void SubmitToWorker(Connection *conn)
        {
            if (conn == nullptr || conn->closed || conn->working)
                return;
            if (conn->inbuffer.empty() || _pool == nullptr || !_business)
                return;

            auto it = mp.find(conn->_fd);
            if (it == mp.end() || it->second == nullptr)
                return;

            // 任务执行期间连接可能被关闭，用 shared_ptr 保住对象
            std::shared_ptr<Connection> holder = it->second;

            conn->working = true;
            std::string data = std::move(conn->inbuffer);
            conn->inbuffer.clear();
            size_t submitted = data.size();

            _pool->submit([holder, data = std::move(data), submitted]() mutable {
                BusinessResult result = _business(std::move(data));

                WorkerResult item;
                item.conn = holder;
                item.out = std::move(result.out);
                item.leftover = std::move(result.leftover);
                item.consumed = submitted >= result.leftover.size()
                                    ? submitted - result.leftover.size()
                                    : 0;
                item.closeAfterSend = result.closeAfterSend;

                pthread_mutex_lock(&_resultMutex);
                _results.push_back(std::move(item));
                pthread_mutex_unlock(&_resultMutex);

                WakeupIOThread();
            });
        }

        // IO 线程被 eventfd 唤醒后，把业务结果写回连接
        static void HandleWorkerResults()
        {
            // 把 eventfd 的计数读干净，否则会一直触发
            uint64_t value = 0;
            while (read(_wakefd, &value, sizeof(value)) > 0)
            {
            }

            std::vector<WorkerResult> results;

            pthread_mutex_lock(&_resultMutex);
            results.swap(_results);
            pthread_mutex_unlock(&_resultMutex);

            for (auto &item : results)
            {
                Connection *conn = item.conn.get();
                if (conn == nullptr || conn->closed)
                    continue;

                // 确认它还是连接表里的那条连接（防止 fd 被复用）
                auto it = mp.find(conn->_fd);
                if (it == mp.end() || it->second != item.conn)
                    continue;

                conn->working = false;

                // leftover 是更早收到的数据，放回 inbuffer 最前面
                if (!item.leftover.empty())
                    conn->inbuffer.insert(0, item.leftover);

                if (!item.out.empty())
                    conn->outbuffer += item.out;

                if (item.closeAfterSend)
                    conn->shoutClose = true;

                if (!conn->outbuffer.empty())
                    EnableReadWrite(conn, true, true);

                // 业务线程工作期间，IO 线程可能又收到了新数据
                bool hasNewData = conn->inbuffer.size() > item.leftover.size();

                bool dispatched = false;
                if (!item.closeAfterSend && !conn->inbuffer.empty() &&
                    (item.consumed > 0 || hasNewData))
                {
                    SubmitToWorker(conn);
                    dispatched = true;
                }

                if (!dispatched && conn->shoutClose &&
                    conn->outbuffer.empty() && conn->_excepter)
                {
                    conn->_excepter(conn);
                }
            }
        }

        static void WakeupIOThread()
        {
            if (_wakefd < 0)
                return;

            uint64_t one = 1;
            ssize_t n = write(_wakefd, &one, sizeof(one));
            (void)n;
        }

    public:
        TcpServer(uint16_t port = defaultport) : _port(port)
        {
        }

        ~TcpServer()
        {
            // 先停线程池，等业务线程全部退出，再关连接
            delete _pool;
            _pool = nullptr;

            int listenfd = _sock.fd();
            for (auto &p : mp)
            {
                if (p.second == nullptr)
                    continue;

                if (p.first == listenfd)
                    p.second->_fd = -1; // 监听 fd 交给 Sock 析构关闭
                else
                    p.second->Close();
            }
            mp.clear();

            if (_wakefd >= 0)
            {
                close(_wakefd);
                _wakefd = -1;
            }

            if (evs != nullptr)
            {
                delete[] evs;
                evs = nullptr;
            }
        }

        static void SetHandler(business_t handler)
        {
            _business = std::move(handler);
        }

    public:
        void initServer()
        {
            _sock.Socket();
            _sock.Bind(_port);
            _sock.Listen();

            _epoller.CreateEpoll();

            // eventfd 用来把业务线程的结果唤醒 IO 线程
            _wakefd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (_wakefd < 0)
            {
                logMessage(FATAL, "eventfd create error: %d,%s", errno, strerror(errno));
                exit(EPOLL_CREATE_ERR);
            }
            _epoller.AddEvent(_wakefd, EPOLLIN);

            int workers = 4;
            const char *env = getenv("TCP_WORKERS");
            if (env != nullptr)
            {
                int n = atoi(env);
                if (n > 0)
                    workers = n;
            }

            _pool = new ThreadPool(static_cast<size_t>(workers));
            logMessage(NORMAL, "worker threads: %d", workers);

            AddConnection(_sock.fd(), EPOLLIN | EPOLLET,
                          std::bind(&TcpServer::Accepter, this, std::placeholders::_1),
                          nullptr, nullptr);

            num = 64;
            evs = new struct epoll_event[num];
        }

        void Dispatcher()
        {
            while (true)
            {
                Loop(-1);
            }
        }

        static void EnableReadWrite(Connection *conn, bool readable, bool writeable)
        {
            uint32_t event = EPOLLET;
            if (readable)
                event |= EPOLLIN;
            if (writeable)
                event |= EPOLLOUT;

            _epoller.Control(conn->_fd, event, EPOLL_CTL_MOD);
        }

    private:
        uint16_t _port;
        Sock _sock;
        struct epoll_event *evs = nullptr;
        int num = 0;

        inline static Epoller _epoller;
        inline static std::unordered_map<int, std::shared_ptr<Connection>> mp;
        inline static business_t _business;

        // 业务线程 -> IO 线程
        inline static int _wakefd = -1;
        inline static ThreadPool *_pool = nullptr;
        inline static pthread_mutex_t _resultMutex = PTHREAD_MUTEX_INITIALIZER;
        inline static std::vector<WorkerResult> _results;
    };
}
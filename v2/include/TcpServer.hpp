#pragma once

#include <iostream>
#include <functional>
#include <unordered_map>
#include <string>
#include <cerrno>
#include <utility>
#include <unistd.h>
#include <sys/socket.h>
#include "log.hpp"
#include "sock.hpp"
#include "Epoller.hpp"
#include "Util.hpp"

static const uint16_t defaultport = 8080;

namespace tcpS_ns
{
    class Connection;
    class TcpServer;

    using func_t = std::function<void(Connection *)>;

    class Connection
    {
    public:
        int _fd;
        std::string inbuffer;
        std::string outbuffer;
        bool shoutClose;

        func_t _recv;
        func_t _send;
        func_t _excepter;

        TcpServer *tsp;

    public:
        Connection(int sock, TcpServer *p) : _fd(sock), shoutClose(false), tsp(p)
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
        static void Recver(Connection *conn)
        {
            char buffer[1024];
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
                    // 如果还有没处理完的请求，先处理掉，再等发送完关闭
                    if (!conn->inbuffer.empty() && _server)
                        _server(conn);
                    conn->shoutClose = true;
                    if (!conn->outbuffer.empty())
                        EnableReadWrite(conn, false, true);
                    else if (conn->_excepter)
                        conn->_excepter(conn);
                    return;
                }
                else
                {
                    if (errno == EINTR)
                        continue;
                    else if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    else if (conn->_excepter)
                        conn->_excepter(conn);
                    return;
                }
            }

            if (_server)
                _server(conn);
        }

        // 写事件回调
        static void Sender(Connection *conn)
        {
            while (true)
            {
                ssize_t s = send(conn->_fd, conn->outbuffer.c_str(), conn->outbuffer.size(), 0);
                if (s > 0)
                {
                    conn->outbuffer.erase(0, s);

                    if (conn->outbuffer.empty())
                    {
                        EnableReadWrite(conn, true, false);
                        break;
                    }
                }
                else
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    else if (errno == EINTR)
                        continue;
                    else
                    {
                        if (conn->_excepter)
                            conn->_excepter(conn);
                        return;
                    }
                }
            }
            if (conn->outbuffer.empty() && conn->shoutClose)
            {
                conn->_excepter(conn);
            }
        }

        static void Excepter(Connection *conn)
        {
            _epoller.Control(conn->_fd, 0, EPOLL_CTL_DEL);
            conn->Close();
            mp.erase(conn->_fd);

            logMessage(DEBUG, "关闭%d 文件描述符的所有的资源", conn->_fd);

            delete conn;
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
                    else if (err == EINTR)
                        continue;
                    else
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
                uint32_t events = evs[i].events;

                if (events & EPOLLERR)
                    events |= (EPOLLIN | EPOLLOUT);
                if (events & EPOLLHUP)
                    events |= (EPOLLIN | EPOLLOUT);

                auto it = mp.find(sock);
                if (it == mp.end())
                    continue;

                Connection *conn = it->second;
                if (conn == nullptr)
                    continue;

                if (events & EPOLLIN)
                    conn->_recv(conn);

                if (events & EPOLLOUT)
                {
                    // 重新查一次，确认 conn 还活着
                    it = mp.find(sock);
                    if (it != mp.end() && it->second != nullptr)
                    {
                        it->second->_send(it->second);
                    }
                }
            }
        }

        bool AddConnection(int sock, uint32_t events, func_t recv, func_t send, func_t excepter)
        {
            util::setNonBlock(sock);

            if (!_epoller.AddEvent(sock, events))
            {
                logMessage(ERROR, "AddEvent failed, close sock %d", sock);
                close(sock);
                return false;
            }

            Connection *connect = new Connection(sock, this);
            connect->Register(recv, send, excepter);
            mp[sock] = connect;
            return true;
        }

    public:
        TcpServer(uint16_t port = defaultport) : _port(port)
        {
        }

        ~TcpServer()
        {
            if (evs != nullptr)
            {
                delete[] evs;
                evs = nullptr;
            }
            if (!mp.empty())
            {
                for (auto &p : mp)
                {
                    close(p.first);
                    if (p.second)
                        delete p.second;
                }
            }
            mp.clear();
        }

        static void SetHandler(func_t handler)
        {
            _server = std::move(handler);
        }

    public:
        void initServer()
        {
            _sock.Socket();
            _sock.Bind(_port);
            _sock.Listen();

            _epoller.CreateEpoll();
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
        inline static std::unordered_map<int, Connection *> mp;
        inline static func_t _server;
    };
}

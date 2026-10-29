#pragma once

#include <iostream>
#include <functional>
#include <unordered_map>
#include <cassert>
#include "log.hpp"
#include "sock.hpp"
#include "Epoller.hpp"
#include "Util.hpp"
#include "protocal.hpp"

static const uint16_t defaultport = 8080;

namespace tcpS_ns
{
    class Connection;
    class TcpServer;

    using func_t = std::function<void(Connection *)>;
    // using handel_t = std::function<void(std::string)>;

    class Connection
    {
    public:
        int _fd;
        std::string inbuffer;
        std::string outbuffer;

        func_t _recv;
        func_t _send;
        func_t _excepter;

        TcpServer *tsp;

    public:
        Connection(int sock, TcpServer *p) : _fd(sock), tsp(p)
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
            close(_fd);
        }
    };

    class TcpServer
    {
    private:
        void Recver(Connection *conn)
        {
            // logMessage(DEBUG,"read...");
            char buffer[1024];
            while (true)
            {
                ssize_t s = recv(conn->_fd, buffer, sizeof(buffer) - 1, 0);
                if (s > 0)
                {
                    buffer[s] = 0;
                    conn->inbuffer += buffer;
                }
                else if (s == 0)
                {
                    if (conn->_excepter)
                        conn->_excepter(conn);
                    return;
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
                        {
                            conn->_excepter(conn);
                            return;
                        }
                    }
                }
            }
            _server(conn);
            // logMessage(DEBUG,"client# %s",conn->inbuffer.c_str());
        }

        void Sender(Connection *conn)
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
            if (!conn->outbuffer.empty())
                EnableReadWrite(conn, true, true);
        }

        void Excepter(Connection *conn)
        {
            _epoller.Control(conn->_fd, 0, EPOLL_CTL_DEL);
            conn->Close();
            mp.erase(conn->_fd);

            logMessage(DEBUG, "关闭%d 文件描述符的所有的资源", conn->_fd);

            delete conn;
        }

        void Accepter(Connection *p)
        {
            for (;;)
            {
                std::string clientip;
                uint16_t clientport;
                int err = 0;
                int sock = _sock.Accept(&clientip, &clientport, &err);
                if (sock > 0)
                {
                    AddConnection(
                        sock, EPOLLIN | EPOLLET,
                        std::bind(&TcpServer::Recver, this, std::placeholders::_1),
                        std::bind(&TcpServer::Sender, this, std::placeholders::_1),
                        std::bind(&TcpServer::Excepter, this, std::placeholders::_1));

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
                if (events & EPOLLIN)
                    mp[sock]->_recv(mp[sock]);
                if (events & EPOLLOUT)
                    mp[sock]->_send(mp[sock]);
            }
        }

        void AddConnection(int sock, uint32_t events, func_t recv, func_t send, func_t excepter)
        {
            if (events & EPOLLIN)
                util::setNonBlock(sock);

            bool r = _epoller.AddEvent(sock, events);
            assert(r);
            (void)r;
            Connection *connect = new Connection(sock, this);
            connect->Register(recv, send, excepter);

            mp[sock] = connect;
        }

    public:
        TcpServer(func_t func, uint16_t port = defaultport) : _server(func), _port(port)
        {
        }
        ~TcpServer()
        {
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
                // logMessage(DEBUG, "timeout...");
            }
        }

        void EnableReadWrite(Connection *conn, bool readable, bool writeable)
        {
            uint32_t event = (readable ? EPOLLIN : 0) | (writeable ? EPOLLOUT : 0) | EPOLLET;
            _epoller.Control(conn->_fd, event, EPOLL_CTL_MOD);
        }

    private:
        uint16_t _port;
        Sock _sock;
        Epoller _epoller;
        std::unordered_map<int, Connection *> mp;
        struct epoll_event *evs;
        int num;
        func_t _server;
    };
}
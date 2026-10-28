#pragma once

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "log.hpp"

enum{
    USAGE_ERR = 1,
    SOCKET_ERR,
    BIND_ERR,
    LISTEN_ERR,
    EPOLL_CREATE_ERR
};

static const int defualtsock = -1;
const static int backlog = 32;

class Sock
{
public:
    Sock() : _listensock(defualtsock)
    {}
    ~Sock()
    {
        if (_listensock != defualtsock) close(_listensock);
    }
public:
    void Socket()
    {
        _listensock = socket(AF_INET,SOCK_STREAM,0);
        if (_listensock < 0) 
        {
            logMessage(FATAL,"creat error");
            exit(SOCKET_ERR);
        }
        logMessage(NORMAL,"creat socket success: %d",_listensock);
        int opt = 1;
        setsockopt(_listensock,SOL_SOCKET,SO_REUSEADDR | SO_REUSEPORT,&opt,sizeof(opt));
    }

    void Bind(int port)
    {
        struct sockaddr_in local;
        memset(&local,0,sizeof(local));
        local.sin_addr.s_addr = INADDR_ANY;
        local.sin_family = AF_INET;
        local.sin_port = htons(port);
        if (bind(_listensock,(struct sockaddr*) &local,sizeof(local)) < 0)
        {
            logMessage(FATAL,"bind socket error");
            exit(BIND_ERR);
        }
        logMessage(NORMAL,"bind socket success!");
    }

    void Listen()
    {
        if (listen(_listensock,backlog) < 0) 
        {
            logMessage(FATAL,"listen sock error");
            exit(LISTEN_ERR);
        }
        logMessage(NORMAL,"listen sock success");
    }

    int Accept(std::string* clientip,uint16_t* clientport,int* err)
    {
        struct sockaddr_in peer;
        socklen_t len = sizeof(peer);
        int sock = accept(_listensock,(struct sockaddr*)& peer,&len);
        if (sock < 0) 
        {
            *err = errno;
            //logMessage(ERROR,"accept error,next");
        }
        else 
        {
            logMessage(NORMAL,"accept success,sock: %d",sock);
            *clientip = inet_ntoa(peer.sin_addr);
            *clientport = ntohs(peer.sin_port);
            *err = 0;
        }
        return sock;
    }

    int fd()
    {
        return _listensock;
    }
private:
    int _listensock;
};
#pragma once

#include <sys/epoll.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include "log.hpp"

static const int size = 128;

class Epoller
{
public:
    Epoller() : _epfd(-1)
    {

    }
    ~Epoller()
    {
        if (_epfd != -1) close(_epfd);
    }
public:
    bool CreateEpoll()
    {
        _epfd = epoll_create(size);
        if (_epfd < 0)
        {
            logMessage(FATAL,"epoll create error: %d,%s",errno,strerror(errno));
            return false;
        }
        logMessage(NORMAL,"epoll create success");
        return true;
    }

    bool AddEvent(int sock,uint32_t events)
    {  
        struct epoll_event ev;
        ev.data.fd = sock;
        ev.events = events;

        int n = epoll_ctl(_epfd,EPOLL_CTL_ADD,sock,&ev);
        if (n < 0) logMessage(ERROR,"add event error: %d,%s",errno,strerror(errno));
        return true;
    }

    int Wait(struct epoll_event* evs,int num,int timeout)
    {
        int n = epoll_wait(_epfd,evs,num,timeout);
        return n;
    }

    bool Control(int sock, uint32_t event, int action)
    {
        int n = 0;
        if(action == EPOLL_CTL_MOD)
        {
            struct epoll_event ev;
            ev.events = event;
            ev.data.fd = sock;
            n = epoll_ctl(_epfd, action, sock, &ev);
        }
        else if(action == EPOLL_CTL_DEL)
        {
            n = epoll_ctl(_epfd, action, sock, nullptr);
        }
        else n = -1;

        return n == 0;
    }
private:
    int _epfd;
};
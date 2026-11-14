#pragma once

#include <pthread.h>
#include <iostream>
#include <string>
#include <functional>

const int lenth = 1024;
typedef std::function<void* (void*)> func_t;

class Thread
{
private:
    static void* start_routine(void* args);

    void* callback();
public:
    Thread();

    void start(func_t func,void* args);

    void join();

    ~Thread();

private:
    std::string _name;
    func_t _func;
    void* _args;
    pthread_t _tid;

    inline static int id = 1;
};

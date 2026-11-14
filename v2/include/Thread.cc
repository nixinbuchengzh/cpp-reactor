#include "Thread.h"

void *Thread::start_routine(void *args)
{
    Thread *td = static_cast<Thread *>(args);
    return td->callback();
}

Thread::Thread()
{
    char name[lenth];
    snprintf(name, sizeof(name), "Thread-%d", id++);
    _name = name;
}

void Thread::start(func_t func, void *args)
{
    _func = func;
    _args = args;
    int n = pthread_create(&_tid, nullptr, start_routine, this);
    (void)n;
}

void *Thread::callback()
{
    return _func(_args);
}

void Thread::join()
{
    int n = pthread_join(_tid, nullptr);
    (void)n;
}

Thread::~Thread()
{
    //do nothing
}

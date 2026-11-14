#pragma once

#include "Thread.h"

#include <pthread.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t threadNum = 4)
    {
        if (threadNum == 0)
            threadNum = 1;

        pthread_mutex_init(&_mutex, nullptr);
        pthread_cond_init(&_cond, nullptr);

        _threads.reserve(threadNum);
        for (std::size_t i = 0; i < threadNum; ++i)
        {
            auto t = std::make_unique<Thread>();
            t->start(&ThreadPool::WorkerEntry, this);
            _threads.push_back(std::move(t));
        }
    }

    ~ThreadPool()
    {
        stop();
    }

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    void submit(std::function<void()> task)
    {
        pthread_mutex_lock(&_mutex);
        if (_stop)
        {
            pthread_mutex_unlock(&_mutex);
            return;
        }
        _tasks.push(std::move(task));
        pthread_cond_signal(&_cond);
        pthread_mutex_unlock(&_mutex);
    }

    std::size_t size() const
    {
        return _threads.size();
    }

private:
    static void *WorkerEntry(void *arg)
    {
        static_cast<ThreadPool *>(arg)->run();
        return nullptr;
    }

    void run()
    {
        for (;;)
        {
            std::function<void()> task;

            pthread_mutex_lock(&_mutex);
            while (!_stop && _tasks.empty())
                pthread_cond_wait(&_cond, &_mutex);

            // stop 之后仍把队列里已有的任务做完
            if (_stop && _tasks.empty())
            {
                pthread_mutex_unlock(&_mutex);
                return;
            }

            task = std::move(_tasks.front());
            _tasks.pop();
            pthread_mutex_unlock(&_mutex);

            task();
        }
    }

    void stop()
    {
        pthread_mutex_lock(&_mutex);
        if (_stop)
        {
            pthread_mutex_unlock(&_mutex);
            return;
        }
        _stop = true;
        pthread_cond_broadcast(&_cond);
        pthread_mutex_unlock(&_mutex);

        for (auto &t : _threads)
            t->join();
        _threads.clear();

        pthread_cond_destroy(&_cond);
        pthread_mutex_destroy(&_mutex);
    }

private:
    std::vector<std::unique_ptr<Thread>> _threads;
    std::queue<std::function<void()>> _tasks;
    pthread_mutex_t _mutex;
    pthread_cond_t _cond;
    bool _stop = false;
};

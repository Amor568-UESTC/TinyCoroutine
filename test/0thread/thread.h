#pragma once

#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>
#include <sys/syscall.h>
#include <iostream>
#include <unistd.h>

namespace sylar
{

class Semaphore
{
private:
    std::mutex mtx_;
    std::condition_variable cv_;
    int cnt_;

public:
    explicit Semaphore(int cnt = 0) : cnt_(cnt) {}

    void wait()     //P操作
    {
        std::unique_lock<std::mutex> lock(mtx_);
        while (cnt_ == 0)
            cv_.wait(lock);
        cnt_--;
    }

    void signal()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cnt_++;
        cv_.notify_one();
    }
};

class Thread
{
private:
    pid_t m_id = -1;
    pthread_t m_thread = 0;

    std::function<void()> m_cb;
    std::string m_name;

    Semaphore m_semaphore;

    static void* run(void* arg);

public:
    Thread(std::function<void()> cb, const std::string& name);
    ~Thread();

    pid_t getId() const { return m_id; }
    const std::string& getName() const { return m_name; }

    void join();

    static pid_t GetThreadId();
    static Thread* GetThis();
    static const std::string& GetName();
    static void SetName(const std::string& name);
};

}
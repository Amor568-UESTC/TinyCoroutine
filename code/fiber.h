#pragma once

#include <iostream>
#include <memory>
#include <atomic>
#include <assert.h>
#include <functional>
#include <cassert>
#include <ucontext.h>
#include <unistd.h>
#include <mutex>

namespace sylar
{

class Fiber : public std::enable_shared_from_this<Fiber>    //纤程
{
public:
    enum State      //状态机
    {
        READY,
        RUNNING,
        TERM,
    };

private:
    uint64_t m_id = 0;
    uint32_t m_stacksize = 0;
    State m_state = READY;
    ucontext_t m_ctx;       //用户级上下文切换
    void* m_stack = nullptr;
    std::function<void()> m_cb;
    bool m_runInScheduler;  //是否让出执行权给调度协程

    //GetThis()调用创建主协程
    Fiber();

public:
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = 1);
    ~Fiber();

    void reset(std::function<void()> cb);
    void resume();  //唤醒
    void yield();   //让出

    uint64_t getId() const { return m_id; }
    uint64_t getState() const { return m_state; }

    static void SetThis(Fiber* f);
    static std::shared_ptr<Fiber> GetThis();
    static void SetSchedulerFiber(Fiber* f);    //设置调度协程(默认为主协程)
    static uint64_t GetFiberId();
    static void MainFunc();

    std::mutex m_mtx;
};

}
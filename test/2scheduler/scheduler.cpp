#include "scheduler.h"

static bool debug = 0;

namespace sylar
{

static thread_local Scheduler* t_scheduler = nullptr; 

void Scheduler::SetThis()
{ t_scheduler = this; }

void Scheduler::tickle() {} //待重写

void Scheduler::run()
{
    int thread_id = Thread::GetThreadId();
    if (debug)
        std::cout << "Schedule::run() starts in thread: " << thread_id << std::endl;
    
    SetThis();

    if (thread_id != m_rootThread)
        Fiber::GetThis();   //设置了主线程和调度线程

    std::shared_ptr<Fiber> idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this)); //空闲线程
    ScheduleTask task;

    while (1)
    {
        task.reset();
        bool tickle_me = 0;

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            auto it = m_tasks.begin();
            while (it != m_tasks.end())
            {
                if (it->thread != -1 && it->thread != thread_id)
                {
                    ++it;
                    tickle_me = 1;
                    continue;
                }

                assert(it->fiber || it->cb);
                task = *it;
                m_tasks.erase(it);
                m_activeThreadCnt++;
                break;
            }
            tickle_me = tickle_me || (it != m_tasks.end());
        }

        if (tickle_me)
            tickle();

        if (task.fiber)
        {
            {
                std::lock_guard<std::mutex> lock(task.fiber->m_mtx);
                if (task.fiber->getState() != Fiber::TERM)
                    task.fiber->resume();
            }
            m_activeThreadCnt--;
            task.reset();
        }
        else if (task.cb)
        {
            std::shared_ptr<Fiber> cb_fiber = std::make_shared<Fiber>(task.cb);
            {
                std::lock_guard<std::mutex> lock(cb_fiber->m_mtx);
                cb_fiber->resume();
            }
            m_activeThreadCnt--;
            task.reset();
        }
        else 
        {
            if (idle_fiber->getState() == Fiber::TERM)
            {
                if (debug)
                    std::cout << "Schedule::run() ends in thread: " << thread_id << std::endl;
                break;
            }
            m_idleThreadCnt++;
            idle_fiber->resume();
            m_idleThreadCnt--;
        }
    }
}

void Scheduler::idle()
{
    while (!stopping())
    {
        if (debug)
            std:: cout << "Schedule::idle(), sleeping in thread: " << Thread::GetThreadId() << std::endl;
        sleep(1);
        Fiber::GetThis()->yield();
    }
}

bool Scheduler::stopping()
{
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_stopping && m_tasks.empty() && m_activeThreadCnt == 0;
}

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string& name) :
m_useCaller(use_caller), m_name(name)
{
    assert(threads > 0 && Scheduler::GetThis() == nullptr);
    SetThis();
    Thread::SetName(m_name);

    if (use_caller) //如果主线程用作了工作线程
    {
        threads--;
        Fiber::GetThis();
        m_schedulerFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, 0));
        Fiber::SetSchedulerFiber(m_schedulerFiber.get());

        m_rootThread = Thread::GetThreadId();
        m_threadIds.emplace_back(m_rootThread);
    }

    m_threadCnt = threads;
    if (debug)
        std::cout << "Scheduler::Scheduler() success" << std::endl;
}

Scheduler::~Scheduler()
{
    assert(stopping() == 1);
    if (GetThis() == this)
        t_scheduler = nullptr;
    if (debug)
        std::cout << "Scheduler::~Scheduler() success" << std::endl;
}

Scheduler* Scheduler::GetThis()
{ return t_scheduler; }

void Scheduler::start()
{
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_stopping)
    {
        std::cerr << "Scheduler is stopped" << std::endl;
        return ;
    }

    assert(m_threads.empty());
    m_threads.resize(m_threadCnt);
    for (size_t i = 0; i < m_threadCnt; ++i)
    {
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
        m_threadIds.emplace_back(m_threads[i]->getId());
    }
    if (debug)
        std::cout << "Scheduler::start() success " << std::endl;
}

void Scheduler::stop()
{
    if (debug)
        std::cout << "Schedule::stop() starts in thread: " << Thread::GetThreadId() << std::endl;

    if (stopping()) return ;
    m_stopping = 1;

    if (m_useCaller)
        assert(GetThis() == this);
    else 
        assert(GetThis() != this);

    for (size_t i = 0; i < m_threadCnt; ++i)
        tickle();

    if (m_schedulerFiber)
    {
        tickle();
        m_schedulerFiber->resume();
        if (debug)
            std::cout << "m_schedulerFiber ends in thread: " << Thread::GetThreadId() << std::endl;
    }

    std::vector<std::shared_ptr<Thread>> thrs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        thrs.swap(m_threads);
    }

    for (auto& i : thrs)
        i->join();

    if (debug)
        std::cout << "Schedule::stop() ends in thread: " << Thread::GetThreadId() << std::endl;
}

}
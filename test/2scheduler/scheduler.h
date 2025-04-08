#pragma once 

#include <mutex>
#include <vector>

#include "fiber.h"
#include "thread.h"

namespace sylar
{

class Scheduler     //调度器
{
private:
    struct ScheduleTask
    {
        std::shared_ptr<Fiber> fiber;
        std::function<void()> cb;
        int thread;

        ScheduleTask()
        {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }

        ScheduleTask(std::shared_ptr<Fiber> f, int thr)
        {
            fiber = f;
            thread = thr;
        }

        ScheduleTask(std::shared_ptr<Fiber>* f, int thr)
        {
            fiber.swap(*f);
            thread = thr;
        }

        ScheduleTask(std::function<void()> f, int thr)
        {
            cb = f;
            thread = thr;
        }

        ScheduleTask(std::function<void()>* f, int thr)
        {
            cb.swap(*f);
            thread = thr;
        }

        void reset()
        {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };
    
private:
    std::string m_name;
    std::mutex m_mtx;
    std::vector<std::shared_ptr<Thread>> m_threads; //线程池
    std::vector<ScheduleTask> m_tasks;  //任务队列
    std::vector<int> m_threadIds;       
    size_t m_threadCnt = 0;     //需要的额外线程数目
    std::atomic<size_t> m_activeThreadCnt = {0};
    std::atomic<size_t> m_idleThreadCnt = {0};

    bool m_useCaller;       //主线程是否用作工作线程,一般让主线程为调度线程
    std::shared_ptr<Fiber> m_schedulerFiber;        //若主线程不为调度线程，则需要的额外调度协程
    int m_rootThread = -1;      //主线程id
    bool m_stopping = 0;

protected:
    void SetThis();

    virtual void tickle();
    virtual void run();     //线程函数
    virtual void idle();    //空闲协程函数
    virtual bool stopping();    //是否可关闭

    bool hasIdleThreads() { return m_idleThreadCnt > 0; }

public:
    Scheduler(size_t threads = 1, bool use_caller = 1, const std::string& name = "Scheduler");
    virtual ~Scheduler();

    const std::string& getName() const { return m_name; }

    static Scheduler* GetThis();

public:
    template <class FiberOrCb>
    void scheduleLock(FiberOrCb fc, int thread = -1)
    {
        bool need_tickle;

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            need_tickle = m_tasks.empty();

            ScheduleTask task(fc, thread);
            if (task.fiber || task.cb)
                m_tasks.emplace_back(task);
        }

        if (need_tickle)
            tickle();
    }

    virtual void start();   //启动线程池
    virtual void stop();    //关闭线程池
};

}
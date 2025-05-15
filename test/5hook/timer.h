#pragma once

#include <memory>
#include <vector>
#include <set>
#include <shared_mutex>
#include <assert.h>
#include <functional>
#include <mutex>

namespace sylar
{

class TimerManager;

class Timer : public std::enable_shared_from_this<Timer>
{
    friend class TimerManager;

private:
    bool m_recurring = 0;   //是否循环
    uint64_t m_ms = 0;      //持续时间ms
    std::chrono::time_point<std::chrono::system_clock> m_next;  //下个过期的时间点
    std::function<void()> m_cb;
    TimerManager* m_manager = nullptr;      //管理的上级

private:
    Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);

private:
    struct Comparator
    { 
        bool operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const; 
    };

public:
    bool cancel();
    bool refresh();
    bool reset(uint64_t ms, bool from_now);

};

class TimerManager
{
    friend class Timer;

private:
    std::shared_mutex m_mtx;
    std::set<std::shared_ptr<Timer>, Timer::Comparator> m_timers;   //管理的时间点集合
    bool m_tickled = 0;     //在下次getNextTimer()执行前，onTimerInsertAtFront()是否已经被触发
    std::chrono::time_point<std::chrono::system_clock> m_preTime;   //前一次

    bool detectClockRollover();

protected:
    virtual void onTimerInsertedAtFront() {}  //在前面插入，在ioscheduler类中重写
    void addTimer(std::shared_ptr<Timer> timer);

public:
    TimerManager();
    virtual ~TimerManager();

    std::shared_ptr<Timer> addTimer(uint64_t ms, std::function<void()> cb, bool recurring = 0);
    std::shared_ptr<Timer> addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = 0);
    uint64_t getNextTimer();
    void listExpireCb(std::vector<std::function<void()>>& cbs);
    bool hasTimer();

};

}
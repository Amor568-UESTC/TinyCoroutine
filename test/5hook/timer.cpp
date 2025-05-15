#include "timer.h"

namespace sylar
{
Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager) :
m_ms(ms), m_cb(cb), m_recurring(recurring), m_manager(manager)
{
    auto now = std::chrono::system_clock::now();
    m_next = now + std::chrono::milliseconds(m_ms);
}

bool Timer::Comparator::operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const
{
    assert(lhs!=nullptr && rhs!=nullptr);
    return lhs->m_next < rhs->m_next;
}

bool Timer::cancel()    //将本timer从manager监管中移除
{
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mtx);   //写🔓

    if (!m_cb) return 0;
    else m_cb = nullptr;

    auto it = m_manager->m_timers.find(shared_from_this());
    if (it != m_manager->m_timers.end())
        m_manager->m_timers.erase(it);
    return 1;
}

bool Timer::refresh()   //更新下一时间点，重新放入
{
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mtx);

    if (!m_cb) return 0;

    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end())
        return 0;

    m_manager->m_timers.erase(it);
    m_next = std::chrono::system_clock::now() + std::chrono::milliseconds(m_ms);
    m_manager->m_timers.insert(shared_from_this());
    return 1;
}

bool Timer::reset(uint64_t ms, bool from_now)       //重新设置m_ms和m_next，设置m_next为
{
    if (ms == m_ms && !from_now)
        return 1;

    {
        std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mtx);
        if (!m_cb) return 0;

        auto it = m_manager->m_timers.find(shared_from_this());
        if (it == m_manager->m_timers.end())
            return 0;
        m_manager->m_timers.erase(it);
    }

    auto start = from_now ? std::chrono::system_clock::now() : m_next - std::chrono::milliseconds(m_ms); //时间节点上次设置时的时间
    m_ms = ms;
    m_next = start + std::chrono::milliseconds(ms);
    m_manager->addTimer(shared_from_this());
    return 1;
}

bool TimerManager::detectClockRollover()
{
    bool rollover = 0;
    auto now = std::chrono::system_clock::now();
    if (now < (m_preTime - std::chrono::milliseconds(60 * 60 * 1000)))
        rollover = 1;
    m_preTime = now;
    return rollover;
}

void TimerManager::addTimer(std::shared_ptr<Timer> timer)
{
    bool at_front = 0;
    {
        std::unique_lock<std::shared_mutex> write_lock(m_mtx);
        auto it = m_timers.insert(timer).first;
        at_front = (it == m_timers.begin()) && !m_tickled;

        if (at_front)
            m_tickled = 1;
    }

    if (at_front)
        onTimerInsertedAtFront();
}

TimerManager::TimerManager()
{ m_preTime = std::chrono::system_clock::now(); }

TimerManager::~TimerManager() {}

std::shared_ptr<Timer> TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring)
{
    std::shared_ptr<Timer> timer(new Timer(ms, cb, recurring, this));
    addTimer(timer);
    return timer;
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb)
{
    auto tmp = weak_cond.lock();
    if (tmp) cb();
}

std::shared_ptr<Timer> TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring)
{ return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring); }

uint64_t TimerManager::getNextTimer()
{
    std::shared_lock<std::shared_mutex> read_lock(m_mtx);       //读🔓
    m_tickled = 0;

    if (m_timers.empty())
        return ~0ull;   //对0ull取反即最大值

    auto now = std::chrono::system_clock::now();
    auto time = (*m_timers.begin())->m_next;

    if (now >= time)
        return 0;
    else 
    {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
        return static_cast<uint64_t>(duration.count());
    }
}

void TimerManager::listExpireCb(std::vector<std::function<void()>>& cbs)
{
    auto now = std::chrono::system_clock::now();
    std::unique_lock<std::shared_mutex> write_lock(m_mtx);
    bool rollover = detectClockRollover();

    while (!m_timers.empty() && rollover || !m_timers.empty() && (*m_timers.begin())->m_next <= now)
    {
        auto tmp = *m_timers.begin();
        m_timers.erase(m_timers.begin());
        cbs.emplace_back(tmp->m_cb);
        if (tmp->m_recurring)
        {
            tmp->m_next = now + std::chrono::milliseconds(tmp->m_ms);
            m_timers.insert(tmp);
        }
        else 
            tmp->m_cb = nullptr;
    }
}

bool TimerManager::hasTimer()
{ 
    std::shared_lock<std::shared_mutex> read_lock(m_mtx);
    return !m_timers.empty();
}

}
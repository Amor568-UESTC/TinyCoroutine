#pragma once

#include "scheduler.h"
#include "timer.h"

namespace sylar
{

//注册一个事件 -> 等待就绪 -> 调度回调函数 -> 删除事件 -> 执行回调函数
class IOManager : public Scheduler, public TimerManager
{
public:
    enum Event
    {
        NONE = 0x0,
        READ = 0x1,
        WRITE = 0x4
    };

private:
    struct FdContext
    {
        struct EventContext
        {
            Scheduler* scheduler = nullptr;  //本事件的调度器
            std::shared_ptr<Fiber> fiber;
            std::function<void()> cb;
        };

        EventContext read;
        EventContext write;
        int fd = 0;
        Event m_event = NONE;
        std::mutex mtx;

        EventContext& getEventContext(Event event);
        void resetEventContext(EventContext& ctx);
        void triggerEvent(Event event);     //事件触发，将事件放入调度器的任务队列中
    };

    int m_epfd = 0;
    int m_tickleFds[2]; //0读1写，管道
    std::atomic<size_t> m_pendingEventCnt = {0}; //悬而未决的事件
    std::shared_mutex m_mtx;
    std::vector<FdContext*> m_fdContexts;

protected:
    void tickle() override;
    bool stopping() override;
    void idle() override;
    void onTimerInsertedAtFront() override;
    void contextResize(size_t size);

public:
    IOManager(size_t threads = 1, bool user_caller = 1, const std::string& name = "IOManager");
    ~IOManager();

    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
    bool delEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event); //删除并触发回调
    bool cancelAll(int fd);

    static IOManager* GetThis();
};

} // namespace sylar

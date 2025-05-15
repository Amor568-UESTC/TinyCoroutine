#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cstring>

#include "ioscheduler.h"

static bool debug = 1;

namespace sylar
{

IOManager::FdContext::EventContext& IOManager::FdContext::getEventContext(Event event)
{
    assert(event == READ || event == WRITE);
    switch (event)
    {
    case READ:
        return read;    
    default:
        return write;
    }
    throw std::invalid_argument("Unsupported event type");
}

void IOManager::FdContext::resetEventContext(EventContext& ctx)
{
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

void IOManager::FdContext::triggerEvent(Event event) 
{
    assert(m_event & event);
    m_event = (Event)(m_event & ~event); //去除event

    EventContext& ctx = getEventContext(event);
    if (ctx.cb)
        ctx.scheduler->scheduleLock(&ctx.cb);
    else
        ctx.scheduler->scheduleLock(&ctx.fiber);

    resetEventContext(ctx);
    return ;
}

void IOManager::tickle()
{
    if (!hasIdleThreads())
        return ;
    int rt = write(m_tickleFds[1], "T", 1);  //往管道里写一个T
    assert(rt == 1);
}

bool IOManager::stopping()
{
    uint64_t timeout = getNextTimer();
    return timeout == ~0ull && m_pendingEventCnt == 0 && Scheduler::stopping();
}

void IOManager::idle() //空闲之前将事件完成
{
    static const uint64_t MAX_EVENTS = 256;
    std::unique_ptr<epoll_event[]> events(new epoll_event[MAX_EVENTS]);

    while (1)
    {
        if (debug)
            std::cout << "IOManager::idle(), run in thread " << Thread::GetThreadId() << std::endl;
        if (stopping())
        {
            if (debug)
                std::cout << "name = " << getName() << " idle exits in thread: " << Thread::GetThreadId() << std::endl;
                break;
        }

        int rt = 0;
        while (1)
        {
            static const uint64_t MAX_TIMEOUT = 5000;
            uint64_t next_timeout = getNextTimer();
            next_timeout = std::min(next_timeout, MAX_TIMEOUT);

            rt = epoll_wait(m_epfd, events.get(), MAX_EVENTS, (int)next_timeout);
            if (rt < 0 && errno == EINTR)
                continue;
            else
                break;
        }

        std::vector<std::function<void()>> cbs;
        listExpireCb(cbs);
        if (!cbs.empty())
        {
            for (const auto& cb : cbs)
                scheduleLock(cb);
            cbs.clear();
        }

        for (int i = 0; i < rt; ++i)
        {
            epoll_event& event = events[i];
            if (event.data.fd == m_tickleFds[0])
            {
                uint8_t dummy[256];
                while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0); //边缘触发-
                continue;
            }

            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            std::lock_guard<std::mutex> lock(fd_ctx->mtx);

            if (event.events & (EPOLLERR | EPOLLHUP))
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->m_event;

            int real_events = NONE;
            if (event.events & EPOLLIN)
                real_events |= READ;
            if (event.events & EPOLLOUT)
                real_events |= WRITE;
            if ((fd_ctx->m_event & real_events) == NONE)
                continue;

            int lft_events = (fd_ctx->m_event & ~real_events);
            int op         = lft_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events   = EPOLLET | lft_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2)
            {
                std::cerr << "idle::epoll_ctl failed: " << strerror(errno) << std::endl;
                continue;
            }

            if (real_events & READ)
            {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCnt;
            }
            if (real_events & WRITE)
            {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCnt;
            }
        }

        Fiber::GetThis()->yield();
    }
}

void IOManager::onTimerInsertedAtFront()
{ tickle(); }

void IOManager::contextResize(size_t size)
{
    m_fdContexts.resize(size); //resize并不改变已经有的值！

    for (size_t i = 0; i < m_fdContexts.size(); ++i)
        if (m_fdContexts[i] == nullptr)
        {
            m_fdContexts[i] = new FdContext();
            m_fdContexts[i]->fd = i;
        }
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string& name) :
Scheduler(threads, use_caller, name), TimerManager()
{
    m_epfd = epoll_create(5000);
    assert(m_epfd > 0);

    int rt = pipe(m_tickleFds);
    assert(!rt);

    epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = m_tickleFds[0];

    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    assert(!rt);

    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    assert(!rt);

    contextResize(32);
    start();
}

IOManager::~IOManager()
{
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for (size_t i = 0; i < m_fdContexts.size(); ++i)
        if (m_fdContexts[i])
            delete m_fdContexts[i];
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb)
{
    FdContext* fd_ctx = nullptr;

    std::shared_lock<std::shared_mutex> read_lock(m_mtx);
    if ((int)m_fdContexts.size() > fd)
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        std::unique_lock<std::shared_mutex> write_lock(m_mtx);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mtx);
    if (fd_ctx->m_event & event)
        return -1;

    int op = fd_ctx->m_event? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->m_event | event;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt)
    {
        std::cerr << "addEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
        return -1;
    }

    ++m_pendingEventCnt;

    fd_ctx->m_event = (Event)(fd_ctx->m_event | event);

    FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
    assert(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb)
        event_ctx.cb.swap(cb);
    else
    {
        event_ctx.fiber = Fiber::GetThis();
        assert(event_ctx.fiber->getState() == Fiber::RUNNING);
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event)
{
    FdContext* fd_ctx = nullptr;
    std::shared_lock<std::shared_mutex> read_lock(m_mtx);
    if ((int)m_fdContexts.size() > fd)
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else
    {
        read_lock.unlock();
        return 0;
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mtx);
    if (!(fd_ctx->m_event & event))
        return 0;

    Event new_events = (Event)(fd_ctx->m_event & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt)
    {
        std::cerr << "delEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
        return -1;
    }

    --m_pendingEventCnt;
    fd_ctx->m_event = new_events;
    FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return 1;
}

bool IOManager::cancelEvent(int fd, Event event)
{
    FdContext* fd_ctx = nullptr;

    std::shared_lock<std::shared_mutex> read_lock(m_mtx);
    if((int)m_fdContexts.size() > fd)
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        return 0;
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mtx);

    if (!(fd_ctx->m_event & event))
        return 0;

    Event new_events = (Event)(fd_ctx->m_event & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt)
    {
        std::cerr << "cancelEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
        return -1;
    }

    --m_pendingEventCnt;
    fd_ctx->triggerEvent(event);
    return 1;
}

bool IOManager::cancelAll(int fd)
{
    FdContext* fd_ctx = nullptr;

    std::shared_lock<std::shared_mutex> read_lock(m_mtx);
    if ((int)m_fdContexts.size() > fd)
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        return 0;
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mtx);

    if (!fd_ctx->m_event)
        return 0;

    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt)
    {
        std::cerr << "cancelAll::epoll_ctl failed: " << strerror(errno) << std::endl;
        return -1;
    }

    if (fd_ctx->m_event & READ)
    {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCnt;
    }
    if (fd_ctx->m_event & WRITE)
    {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCnt;
    }

    assert(fd_ctx->m_event == 0);
    return 1;
}

IOManager* IOManager::GetThis()
{
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

}
#include "fiber.h"

static bool debug = 0;  //简单debug，可后续升级为log

namespace sylar
{

static thread_local Fiber* t_fiber = nullptr;   // 运行的协程
static thread_local std::shared_ptr<Fiber> t_thread_fiber = nullptr;    // 主协程
static thread_local Fiber* t_scheduler_fiber = nullptr; //调度作用fiber
static std::atomic<uint64_t> s_fiber_id{0};
static std::atomic<uint64_t> s_fiber_cnt{0};

Fiber::Fiber()
{
    SetThis(this);
    m_state = RUNNING;

    if (getcontext(&m_ctx)) //保存当前上下文
    {
        std::cerr << "Fiber() failed" << std::endl;
        pthread_exit(NULL);
    }

    m_id = s_fiber_id++;
    s_fiber_cnt++;
    if(debug)   
        std::cout << "Fiber(): main id = " << m_id << std::endl;
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler):
m_cb(cb), m_runInScheduler(run_in_scheduler)
{
    m_state = READY;
    m_stacksize = stacksize ? stacksize : 128000;
    m_stack = malloc(m_stacksize);

    if (getcontext(&m_ctx))
    {
        std::cerr << "Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed" << std::endl;
        pthread_exit(NULL);
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    makecontext(&m_ctx, &Fiber::MainFunc, 0);   //创建新上下文

    m_id = s_fiber_id++;
    s_fiber_cnt++;
    if(debug)
        std::cout << "Fiber(): child id = " << m_id << std::endl;
}

Fiber::~Fiber()
{
    s_fiber_cnt--;
    if(m_stack)
        free(m_stack);
    if(debug)
        std::cout << "~Fiber() id = " << m_id << std::endl;
}

void Fiber::reset(std::function<void()> cb)
{
    assert(m_stack != nullptr && m_state == TERM);

    m_state = READY;
    m_cb = cb;

    if (getcontext(&m_ctx))
    {
        std::cerr << "reset() failed" << std::endl;
        pthread_exit(NULL);
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
}

void Fiber::resume()
{
    assert(m_state == READY);
    
    m_state = RUNNING;

    if (m_runInScheduler)
    {
        SetThis(this);
        if (swapcontext(&(t_scheduler_fiber->m_ctx), &m_ctx))
        {
            std::cerr << "resume() to t_scheduler_fiber failed" << std::endl;
            pthread_exit(NULL);
        }
    }
    else
    {
        SetThis(this);
        if (swapcontext(&(t_thread_fiber->m_ctx), &m_ctx))
        {
            std::cerr << "resume() to t_thread_fiber failed" << std::endl;
            pthread_exit(NULL);
        }
    }
}

void Fiber::yield()
{
    assert(m_state == RUNNING || m_state == TERM);

    if (m_state != TERM)
        m_state = READY;

    if (m_runInScheduler)
    {
        SetThis(t_scheduler_fiber);
        if (swapcontext(&m_ctx, &(t_scheduler_fiber->m_ctx)))
        {
            std::cerr << "yeild() to t_scheduler_fiber failed" << std::endl;
            pthread_exit(NULL);
        }
    }
    else
    {
        SetThis(t_thread_fiber.get());
        if (swapcontext(&m_ctx, &(t_thread_fiber->m_ctx)))
        {
            std::cerr << "yeild() to t_thread_fiber failed" << std::endl;
            pthread_exit(NULL);
        }
    }
}

void Fiber::SetThis(Fiber* f)
{ t_fiber = f; }

std::shared_ptr<Fiber> Fiber::GetThis()
{
    if (t_fiber)
        return t_fiber->shared_from_this();

    std::shared_ptr<Fiber> main_fiber(new Fiber());
    t_thread_fiber = main_fiber;
    t_scheduler_fiber = main_fiber.get();   // 默认为调度协程

    assert(t_fiber == main_fiber.get());
    return t_fiber->shared_from_this();
}

void Fiber::SetSchedulerFiber(Fiber* f)
{ t_scheduler_fiber = f; }

uint64_t Fiber::GetFiberId()
{
    if (t_fiber)
        return t_fiber->getId();
    return (uint64_t) - 1;
}

void Fiber::MainFunc()
{
    std::shared_ptr<Fiber> cur = GetThis();
    assert(cur != nullptr);

    cur->m_cb();
    cur->m_cb = nullptr;
    cur->m_state = TERM;

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->yield();
}

}
#include "thread.h"

namespace sylar
{

static thread_local Thread* t_thread          = nullptr;    //某个线程正在使用的Thread类
static thread_local std::string t_thread_name = "UNKNOWN";

void* Thread::run(void* arg)
{
    Thread* thread = (Thread*)arg;

    t_thread = thread;
    t_thread_name = thread->m_name;
    thread->m_id = GetThreadId();
    //设置目前线程名称，非posix标准函数
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> cb;
    cb.swap(thread->m_cb);

    thread->m_semaphore.signal();
    cb();
    return 0;
}

Thread::Thread(std::function<void()> cb, const std::string& name) :
m_cb(cb), m_name(name)
{
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt)
    {
        std::cerr << "pthread_create thread fail, rt = " << rt << " , name = " << name << std::endl;
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait();
}

Thread::~Thread()
{
    if (m_thread)
    {
        pthread_detach(m_thread);   //主线程不必等待子线程
        m_thread = 0;
    }
}

void Thread::join()
{
    if (m_thread)
    {
        int rt = pthread_join(m_thread, nullptr);
        if (rt)
        {
            std::cerr << "phtread_join failed, rt = " << rt << " , name = " << m_name << std::endl;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

//得到该线程的系统tid
pid_t Thread::GetThreadId()
{ return syscall(SYS_gettid); }

Thread* Thread::GetThis()
{ return t_thread; }

const std::string& Thread::GetName()
{ return t_thread_name; }

void Thread::SetName(const std::string& name)
{
    if (t_thread)
        t_thread->m_name = name;
    t_thread_name = name;
}

}
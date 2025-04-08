#include <dlfcn.h>
#include <iostream>
#include <cstdarg>
#include <string.h>

#include "hook.h"
#include "ioscheduler.h"
#include "fd_manager.h"

// apply XX to all functions
#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt) 

namespace sylar
{

static thread_local bool t_hook_enable = 0;

bool is_hook_enable()
{
    return t_hook_enable;
}

void set_hook_enable(bool flag)
{
    t_hook_enable = flag;
}

void hook_init()
{
    static bool is_inited = 0;
    if (is_inited)
        return ;
    
    is_inited = 1;

#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX)
#undef XX
}

struct HookIniter
{
    HookIniter() { hook_init(); }
};

static HookIniter s_hook_initer;
}

struct timer_info
{ int cancelled = 0; };

template<typename OriginFun, typename ... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name, uint32_t event, int timeout_so, Args&&... args)
{

}

extern "C"
{

#define XX(name) name ## _fun name ## _f = nullptr;
    HOOK_FUN(XX);
#undef XX

unsigned int sleep(unsigned int seconds)
{
    if (!sylar::t_hook_enable)
        return sleep_f(seconds);

    auto fiber = std::make_shared<sylar::Fiber>(sylar::Fiber::GetThis());
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    iom->addTimer(seconds * 1000, [fiber, iom] () { iom->scheduleLock(fiber, -1); });
    fiber->yield();
    return 0;
}

int usleep(useconds_t usec)
{
    if (!sylar::t_hook_enable)
        return usleep_f(usec);

    auto fiber = std::make_shared<sylar::Fiber>(sylar::Fiber::GetThis());
    auto iom = sylar::IOManager::GetThis();
    iom->addTimer(usec / 1000, [fiber, iom] () { iom->scheduleLock(fiber); });
    fiber->yield();
    return 0;
}

int nanosleep(){}

}
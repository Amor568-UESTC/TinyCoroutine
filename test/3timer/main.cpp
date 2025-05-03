#include <unistd.h>
#include <iostream>

#include "timer.h"

using namespace sylar;

void func(int i)
{ std::cout << "i: " << i << std::endl; }

int main()
{
    std::shared_ptr<TimerManager> manager(new TimerManager());
    std::vector<std::function<void()>> cbs;

    // 测试listExpiredCb
    {
        for (int i = 0; i < 10; ++i)
            manager->addTimer((i + 1) * 1000, std::bind(&func, i), false);
        
        std::cout << "all timers has been set up" << std::endl;

        sleep(5);
        manager->listExpireCb(cbs);
        while (!cbs.empty())
        {
            std::function<void()> cb = *cbs.begin();
            cbs.erase(cbs.begin());
            cb();
        }

        sleep(5);
        manager->listExpireCb(cbs);
        while (!cbs.empty())
        {
            std::function<void()> cb = *cbs.begin();
            cbs.erase(cbs.begin());
            cb();
        }
    }

    // 测试recurring
    {
        manager->addTimer(1000, std::bind(&func, 1000), true);
        int j = 10;
        while (j-- > 0)
        {
            sleep(1);
            manager->listExpireCb(cbs);
            std::function<void()> cb = *cbs.begin();
            cbs.erase(cbs.begin());
            cb();
        }
    }
}
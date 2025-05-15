#pragma once

#include <memory>
#include <shared_mutex>

#include "thread.h"

namespace sylar
{

// 记录Fd对应的内容
class FdCtx : public std::enable_shared_from_this<FdCtx>
{
private:
    bool m_isInit = 0;
    bool m_isSocket = 0;
    bool m_sysNonblock = 0;
    bool m_userNonblock = 0;
    bool m_isClosed = 0;
    int m_fd;

    uint64_t m_recvTimeout = (uint64_t) - 1;    // 读事件超时时间
    uint64_t m_sendTimeout = (uint64_t) - 1;    // 写事件超时时间

public:
    FdCtx(int fd);
    ~FdCtx();

    bool init();
    bool isInit() const { return m_isInit; }
    bool isSocket() const { return m_isSocket; }
    bool isClosed() const { return m_isClosed; }

    void setUserNonblock(bool v) { m_userNonblock = v; }
    bool getUserNonblock() const { return m_userNonblock; }

    void getSysNonblock(bool v) { m_sysNonblock = v; }
    bool getSysNonblock() const { return m_sysNonblock; }

    void setTimeout(int type, uint64_t v);
    uint64_t getTimeout(int type);
};

class FdManager
{
private:
    std::shared_mutex m_mtx;
    std::vector<std::shared_ptr<FdCtx>> m_datas;

public:
    FdManager();

    std::shared_ptr<FdCtx> get(int fd, bool auto_create = 0);
    void del(int fd);

};


// 单例模板类
template<typename T>
class Singleton
{
private:
    static T* instance;
    static std::mutex mtx;

protected:
    Singleton() {}

public:
    //删除拷贝构造和赋值重载
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    static T* GetInstance()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (instance == nullptr)
            instance = new T();
        return instance;
    }

    static void DestroyInstance()
    {
        std::lock_guard<std::mutex> lock(mtx);
        delete instance;
        instance = nullptr;
    }
};

typedef Singleton<FdManager> FdMgr;

}
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <stack>
#include <cstring>
#include <chrono>
#include <thread>

#include "ioscheduler.h"
#include "hook.h"

static int sock_listen_fd = -1;

void test_accept();

void error(const char* msg)
{
    perror(msg);
    std::cout << "erreur..." << std::endl;
    exit(1);
}

void watch_io_read()
{ sylar::IOManager::GetThis()->addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept); }

void test_accept()
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof addr;
    int fd = accept(sock_listen_fd, (sockaddr*)&addr, &len);
    if (fd < 0)
        std::cout << "accept failed, fd = " << fd << ", errno = " << errno << std::endl;
    else 
    {
        std::cout << "accept connection, fd = " << fd << std::endl;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        sylar::IOManager::GetThis()->addEvent(fd, sylar::IOManager::READ, [fd]()
        {
            char buffer[1024];
            memset(buffer, 0 ,sizeof(buffer));
            while (1)
            {
                int ret = recv(fd, buffer, sizeof(buffer), 0);
                if (ret > 0)
                {
                    std::cout << "received data, fd = " << fd << ", data = " << buffer << std::endl;
                    const char* response = "HTTP/1.1 200 OK\r\n"
                                            "Content-Type: text/plain\r\n"
                                            "Content-Length: 1\r\n"
                                            "Connection: keep-alive\r\n"
                                            "\r\n"
                                            "Hello, World!";
                    ret = send(fd, response, strlen(response), 0);

                    close(fd);
                    break;
                }
                else 
                {
                    if (ret == 0 || errno != EAGAIN)
                    {
                        std::cout << "closing connection, fd = " << fd << std::endl;
                        close(fd);
                        break;
                    }
                    else if (errno == EAGAIN)
                    {
                        std::cout << "recv returned EAGAIN, fd = " << fd << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50)); //延长睡眠时间，避免繁忙等待
                    }
                }
            }
        });
    }
    sylar::IOManager::GetThis()->addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}

void test_iomanager()
{
    int portno = 8080;
    sockaddr_in servAddr, clieAddr;
    socklen_t clieLen = sizeof clieAddr;

    sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_listen_fd < 0)
        error("Error creating socket...\n");

    int ys = 1;
    setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &ys, sizeof(ys));

    memset((char*)&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(portno);
    servAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_listen_fd, (sockaddr*)&servAddr, sizeof(servAddr)) < 0)
        error("Error binding socket...\n");

    if (listen(sock_listen_fd, 1024) < 0)
        error("Error listening...\n");

    std::cout << "epoll echo server listening for connections on port : " << portno << std::endl;
    fcntl(sock_listen_fd, F_SETFD, O_NONBLOCK);
    sylar::IOManager iom(9);
    iom.addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}

int main()
{
    test_iomanager();
    return 0;
}
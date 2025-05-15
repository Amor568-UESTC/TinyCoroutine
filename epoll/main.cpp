#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>


#define PORT 8080
#define MAX_EVENTS 5000
#define BUFSIZE 1024


using namespace std;

int main()
{
    int listenFd, connFd, epollFd;
    sockaddr_in servAddr, clieAddr;
    socklen_t addrLen = sizeof clieAddr;
    epoll_event events[MAX_EVENTS], event;

    if ((listenFd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket error!");
        return -1;
    }
    
    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    bzero(&servAddr, sizeof servAddr);
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(PORT);
    servAddr.sin_addr.s_addr = INADDR_ANY;
    
    if ((bind(listenFd, (sockaddr*)&servAddr, sizeof servAddr)) == -1)
    {
        perror("bind error!");
        return -1;
    }

    if ((listen(listenFd, 1024)) == -1)
    {
        perror("listen error!");
        return -1;
    }

    if ((epollFd = epoll_create1(0)) == -1)
    {
        perror("epoll_create error!");
        return -1;
    }

    event.data.fd = listenFd;
    event.events = EPOLLIN;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &event) == -1)
    {
        perror("epoll_ctl error!");
        return -1;
    }


    while (1)
    {
        int rt = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if (rt == -1) 
        {
            perror("epoll_wait error!");
            return -1;
        }

        for (int i = 0; i < rt; ++i)
        {
            if (events[i].data.fd == listenFd)
            {
                connFd = accept(listenFd, (sockaddr*)&clieAddr, &addrLen);
                if (connFd == -1)
                {
                    perror("accept error!");
                    continue;
                }

                event.data.fd = connFd;
                event.events = EPOLLIN;
                if (epoll_ctl(epollFd, EPOLL_CTL_ADD, connFd, &event) == -1)
                {
                    perror("epoll_ctl error!");
                    return -1;
                }
            }
            else 
            {
                char buf[BUFSIZE];
                int len = read(events[i].data.fd, buf, sizeof buf - 1);
                if (len <= 0)
                    close(events[i].data.fd);
                else 
                {
                    const char* response = "HTTP/1.1 200 OK\r\n"
                                           "Content-Type: text/plain\r\n"
                                           "Content-Length: 1\r\n"
                                           "Connection: keep-alive\r\n"
                                           "\r\n"
                                           "1";
                    write(events[i].data.fd, response, strlen(response));
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    close(events[i].data.fd);
                }
            }
        }
    }

    close(listenFd);
    close(epollFd);
    return 0;
}
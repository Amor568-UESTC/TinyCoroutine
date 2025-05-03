#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include <cerrno>

#include "ioscheduler.h"

using namespace sylar;

char recv_data[4096];

const char data[] = "GET / HTTP/1.0\r\n\r\n";

int sock;

void fun1()
{
    recv(sock, recv_data, 4096, 0);
    std::cout << recv_data << std::endl << std::endl;
}

void fun2()
{
    send(sock, data, sizeof(data), 0);
}

int main()
{
    IOManager manager(2);

    sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in servAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(80);
    servAddr.sin_addr.s_addr = inet_addr("222.197.166.2");

    fcntl(sock, F_SETFL, O_NONBLOCK);

    connect(sock, (sockaddr*)&servAddr, sizeof(servAddr));

    manager.addEvent(sock, IOManager::WRITE, &fun2);
    manager.addEvent(sock, IOManager::READ, &fun1);

    std::cout << "event has been posted\n\n";

    return 0;
}
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "threadpool.h"
#include "locker.h"
#include "http_conn.h"
using namespace std;
//g++ *.cpp -pthread
#define MAX_FD 65535 // 最大文件描述符数目
#define MAX_EVENT_NUMBER 10000 // 监听的最大事件数量
// 添加信号捕捉
void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);
// 从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
// 修改epoll文件描述符
extern void modfd(int epollfd, int fd, int ev);
int main(int argc, char const *argv[])
{
    if(argc < 2){
        cout<<"传参错误，请传递以下格式:"<<argv[0]<<"+ port_number";
        return -1;
    }

    int port = atoi(argv[1]);

    // 对SIGPIPE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化线程池
    threadpool<http_conn> * pool = nullptr;
    try{
        pool = new threadpool<http_conn>();
    } catch(...){
        exit(-1);
    }
    
    // 创建数组保存所有客户端信息
    http_conn * users = new http_conn[MAX_FD];

    // 创建监听套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    // 定义端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定ip端口
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    bind(listenfd, (struct sockaddr*)&address, sizeof(address));

    // 监听
    listen(listenfd, 5);

    // 创建epoll对象，事件数组,添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // 将监听文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true){
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((num < 0) && (errno != EINTR)){
            cout<<"epoll failed"<<endl;
            break;
        }

        // 循环遍历事件数组
        for(int i=0;i<num;i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                // 有客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);

                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);

                if(http_conn::m_user_count >= MAX_FD){
                    // 当前连接数已满
                    // 返回客户端信息，服务器正忙
                    close(connfd);
                    continue;
                }
                // 初始化客户的数据，放入数组中
                users[connfd].init(connfd, client_address);

            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                // 对方异常断开或错误等事件
                users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN){
                if(users[sockfd].read()){
                    // 一次性读完所有数据
                    pool->append(users + sockfd);
                }
                else{
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events | EPOLLOUT){
                if(!users[sockfd].write()){
                    users[sockfd].close_conn();
                }                
            }
        }
    }

    close(listenfd);
    close(epollfd);
    delete[] users;
    delete pool;
    return 0;
}

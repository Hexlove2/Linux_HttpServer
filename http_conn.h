#pragma once
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>
#include <iostream>
#include <string.h>
#include <regex>
#include <cstring>

class http_conn{
public:
    http_conn(){}

    ~http_conn(){}

    static int m_epollfd; // 所有socket上的事件都被注册到同一个epoll对象中
    static int m_user_count; // 统计用户数量

    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    // HTTP请求方法，当前只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /* 解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE //请求行
        CHECK_STATE_HEADER      //请求头
        CHECK_STATE_CONTENT     //请求体
    */
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};

    /* 服务器处理HTTP请求的可能结果，报文解析结果
        NO_REQUEST          // 请求不完整，需要继续读取客户数据
        GET_REQUEST         // 获得一个完成的客户请求
        BAD_REQUEST         // 用户请求语法错误
        NO_RESOURCE         // 服务器无资源
        FORBIDDEN_REQUEST   // 无访问权限
        FILE_REQUEST        // 请求获取文件成功
        INTERNAL_ERROR      // 服务器内部错误
        CLOSED_CONNECTION   // 客户端关闭连接
    */
    enum HTTP_CODE{NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST,INTERNAL_ERROR, CLOSED_CONNECTION};
    
    // 从状态机的三种状态
    // 1.读取到完整行 2.行出差 3.行数据不完整
    enum LINE_STATUS{LINE_OK = 0, LINE_BAD, LINE_OPEN };


    void init(int sockfd, const sockaddr_in & addr); // 初始化
    void close_conn();  // 关闭连接
    bool read();        // 非阻塞地读
    bool write();       // 非阻塞地写
    void unmap();

    // Handle requests from clients
    void process();

    HTTP_CODE process_read(); // 解析HTTP请求
    bool  process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char* text);    // 解析请求行
    HTTP_CODE parse_request_headers(char* text); // 解析请求头
    HTTP_CODE parse_content(char* text);         // 解析请求体
    HTTP_CODE do_request();

    // 这一组函数被process_write调用以填充HTTP应答。
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

    LINE_STATUS parse_line();

private:
    int m_sockfd;             // 该http连接的socket
    sockaddr_in m_address;    // socket地址

    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;           // 读缓冲区中以及读入的最后一个字节的位置
    int m_write_idx;
    int m_checked_index;      // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;         // 当前正在解析的行的起始位置
    int m_content_length;     // 请求体长度

    std::string m_url;
    std::string m_version;
    METHOD m_method;
    std::string m_host;
    bool m_linger;      //判断http请求是否要求保持连接
    std::string m_real_file;
    struct stat m_file_stat;
    char* m_file_address;


    CHECK_STATE m_check_state; // 主状态机当前的状态
    void init();               // 初始化连接其余的信息
    char m_write_buf[WRITE_BUFFER_SIZE];
    char * get_line(){ return m_read_buf + m_start_line;}

    struct iovec m_iv[2];                   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;
};
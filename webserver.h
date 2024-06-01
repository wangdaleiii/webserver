#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    // 初始化成员变量
    void init(int port, string user, string passWord, string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);
    void trig_mode();   // 指定触发方式标志位
    void thread_pool(); // 初始化 m_pool 线程池，为线程池的每个线程创建worker成员函数

    // 初始化 m_connPool 数据库连接池
    // 调用 http_conn::initmysql_result 初始化 用户名-密码对
    void sql_pool();
    
    void log_write(); // 初始化一个单例LOG对象

    // 1. 设置 m_listenfd
    // 2. 将 m_listenfd 添加到 m_epollfd 中
    // 3. 通过 utils 设置信号处理函数
    // 4. 定时 TIMESLOT 秒
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);

    // 将 timer 的到期时间往后推迟3个单位
    // 在 utils.m_timer_lst 中调整 timer 的位置，保持有序
    void adjust_timer(util_timer *timer);

    // 执行 timer 的回调函数，传入的用户参数为 users_timer[sockfd]
    // 删除 timer 定时器
    void deal_timer(util_timer *timer, int sockfd);

    // 从m_listenfd 中 accpet 一个连接到 connfd
    // 初始化 users[connfd](HTTP连接类)， users_timer[connfd](用户数据)
    // 初始化 users_timer[connfd].timer 对应的定时器，到期时间为3个TIMESLOT以后
    // 将该定时器添加到 utils.m_timer_lst 中
    // 如果是边缘触发，需要while(1)循环
    bool dealclientdata();

    // 从 m_pipefd[0] 中接收数据，主要是信号量
    // 如果接收到了SIGALRM，timeout = true
    // 如果接收到了SIGTERM，stop_server = true
    bool dealwithsignal(bool &timeout, bool &stop_server);

    // reactor模式：
    // 1.首先调整定时器，往后推迟3个单位
    // 2.将对应的 http_conn* 放入线程池的工作队列，标志m_state为读
    // 3.等待线程池的工作线程读取数据
        // 如果读取失败
        // 4.执行 timer 的回调函数，传入的用户参数为 users_timer[sockfd]
        // 5.删除 timer 定时器

    // proactor模式:
    // 1.读取网络数据，LT模式下只读取一次，ET模式下使用while循环读取
    // 如果读取成功:
        // 2.将该事件放入请求队列,调整定时器
    // 如果读取失败:
        // 2.删除定时器
    void dealwithread(int sockfd);

    // reactor模式：
        //1.调整定时器到期时间
        //2.将http_conn 添加到 m_workqueue队列中
        //3.设置 http_conn->m_state = 1
        //4.等待线程池写入完成，若写入失败，删除对应的定时器
    // proactor模式:
        //1.写入数据
        //若写入成功，调整定时器
        //若写入失败，删除定时器
    void dealwithwrite(int sockfd);

public:
    char *m_root; // root资源文件夹路径，构造函数中获取

    // init函数中初始化
    int m_port;
    string m_user;         // 登陆数据库用户名
    string m_passWord;     // 登陆数据库密码
    string m_databaseName; // 使用数据库名
    int m_sql_num;         // 数据库连接池最大数量
    int m_thread_num;
    int m_log_write; // 为1设置log异步,0为同步
    int m_OPT_LINGER;
    int m_TRIGMode;   // 触发方式选择
    int m_close_log;  // 为 1 则关闭 LOG 记录
    int m_actormodel; // 线程池对象的模型切换标志

    int m_pipefd[2]; // 双向管道，由eventListen()创建
    int m_epollfd;   // epoll事件表，由eventListen()赋值

    connection_pool *m_connPool;   // 数据库连接池，由sql_pool()创建
    threadpool<http_conn> *m_pool; // 线程池，由thread_pool()创建

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd; // 监听socket，由 eventListen() 创建并设置

    // trig_mode()函数中指定
    //     Listen / Connt
    // 0 :   LT      LT
    // 1 :   LT      ET
    // 2 :   ET      LT
    // 3 :   ET      ET
    int m_LISTENTrigmode; // Listen端口的触发方式,  1 为 ET, 0 为 LT
    int m_CONNTrigmode;   // HTTP的触发方式, 1 为 ET, 0 为 LT

    client_data *users_timer; // 构造函数中创建 MAX_FD 个client_data
    http_conn *users;         // 构造函数中创建 MAX_FD 个http_conn

    Utils utils; // 工具类
};
#endif

#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

class util_timer;

struct client_data
{
    sockaddr_in address; // 客户端的socket地址
    int sockfd;          // 本机与客户端通信的socket fd
    util_timer *timer;   // 该连接对应的定时器
};

class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire; // 存储定时器到期的时间

    void (*cb_func)(client_data *); // 定时器对应的回调函数
    client_data *user_data;         // 定时器对应的用户数据

    // 用于构建双向链表
    util_timer *prev;
    util_timer *next;
};

//--------------------------------------------------------------

class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst(); // 依次释放链表中的所有定时器

    void add_timer(util_timer *timer);    // 将timer插入到有序定时器链表中
    void adjust_timer(util_timer *timer); // 在某一个timer的到期时间修改后，调整timer位置，保持链表有序
    void del_timer(util_timer *timer);    // 删除指定的timer定时器
    void tick();                          // 触发已过期的定时器事件函数，并将已过期的定时器从链表中移除

private:
    // 将timer插入到定时器链表中，用户不可见
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head; // 定时器链表头部
    util_timer *tail; // 定时器链表尾部
};

//--------------------------------------------------------------

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    // 初始化每次定时的时间间隔
    void init(int timeslot);

    // 设置fd文件描述符为非阻塞模式
    int setnonblocking(int fd);

    // 将fd添加到 epollfd 中，监听读事件、对端关闭事件
    // 若one_shot = true，则指定epoll只监听一次
    // 若TRIGMode = 1 设置边缘触发，否则为电平触发
    // 设置fd文件描述符为非阻塞
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    // 信号处理函数，将 sig 参数通过管道u_pipefd传输
    static void sig_handler(int sig);

    // 设置sig信号的信号处理函数为 handler, 并将其注册
    // 如果 restart 为真，就设置 SA_RESTART 标志
    void addsig(int sig, void(handler)(int), bool restart = true);

    // 触发已过期的定时器事件函数，并将已过期的定时器从链表中移除
    // 使用alarm() 定时 m_TIMESLOT 后发送 SIGALRM 信号
    void timer_handler();

    // 向connfd发送info，并关闭connfd
    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    static int u_epollfd;

    sort_timer_lst m_timer_lst; // 定时器链表
    int m_TIMESLOT;             // 每次定时的间隔时间
};

// 将 user_data->sockfd 从 Utils::u_epollfd 中移除
// 关闭 user_data->sockfd
// http_conn::m_user_count--;
void cb_func(client_data *user_data);

#endif

#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

// 依次释放链表中的所有定时器
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

// 将timer插入到有序定时器链表中
void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }

    // 链表无数据，则添加
    if (!head)
    {
        head = tail = timer;
        return;
    }

    // 该定时器的到期时间最早，则将其设置为链表头
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }

    add_timer(timer, head);
}

// 调整timer位置，保持链表有序
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }

    // 如果timer的到期时间要早于下一个定时器，啥都不干
    util_timer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }

    // 如果是链表头
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    // 如果不是链表头
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

// 删除timer定时器
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    // 如果只有一个点，直接删除
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // 如果是链表头
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    // 如果是链表尾
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

// 触发已过期的定时器事件函数，并将已过期的定时器从链表中移除
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }

    time_t cur = time(NULL); // 返回1970年以来的时间
    util_timer *tmp = head;
    while (tmp)
    {
        // 访问到还没到时间的定时器后退出
        if (cur < tmp->expire)
        {
            break;
        }

        // 执行定时器的回调函数
        tmp->cb_func(tmp->user_data);

        // 更改链表头结点
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }

        delete tmp;
        tmp = head;
    }
}

// 将timer插入到定时器链表中，到期时间一定比链表头晚
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    while (tmp)
    {
        // 如果找到了合适的插入位置，那么就将其插入
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    // 要插入的定时器是最晚的
    if (!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

//--------------------------------------------------------------

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

// 设置fd文件描述符为非阻塞模式
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL); // 获取文件描述符的标志位
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT

// 将fd添加到 epollfd 中，监听读事件、对端关闭事件
// 若one_shot = true，则指定epoll只监听一次
// 若TRIGMode = 1 设置边缘触发，否则为电平触发
// 设置fd文件描述符为非阻塞
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    // EPOLLIN       对应文件描述符可读
    // EPOLLET       使用边缘触发模式
    // EPOLLRDHUP    表示对端关闭连接（也会被当做一种事件进行通知）
    // EPOLLONESHOT  事件发生后只监听一次，之后需要重新添加到epoll
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 信号处理函数，将 sig 参数通过管道u_pipefd[1]传输
void Utils::sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

// 设置sig信号的信号处理函数为 handler, 并将其注册
// 如果 restart 为真，就设置 SA_RESTART 标志
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler; // 要执行的信号处理函数

    if (restart)
        sa.sa_flags |= SA_RESTART; // 重启系统调用

    sigfillset(&sa.sa_mask);                 // 在执行信号处理函数期间会阻塞所有其他信号
    assert(sigaction(sig, &sa, NULL) != -1); // 注册信号处理器
}

// 触发已过期的定时器事件函数，并将已过期的定时器从链表中移除
// 使用alarm() 定时 m_TIMESLOT 后发送 SIGALRM 信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

// 向connfd发送info，并关闭connfd
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0; // 对应 WebServer 中的pipefd
int Utils::u_epollfd = 0; // 对应 WebServer 中的epollfd

// 将 user_data->sockfd 从 Utils::u_epollfd 中移除
// 关闭 user_data->sockfd
// http_conn::m_user_count--;
class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}

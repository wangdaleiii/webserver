#include "webserver.h"

// 分配 http_conn 类数组 到 users
// 分配 client_data 类数组 到 users_timer
// 资源文件夹路径记录到 m_root
WebServer::WebServer()
{
    // http_conn类对象
    users = new http_conn[MAX_FD];

    // 定时器
    users_timer = new client_data[MAX_FD];

    // root文件夹路径
    char server_path[200];
    // 获取当前工作目录的路径名
    getcwd(server_path, 200);

    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

// 初始化成员变量
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port; // socket监听端口

    m_user = user;                 // 登陆数据库用户名
    m_passWord = passWord;         // 登陆数据库密码
    m_databaseName = databaseName; // 使用数据库名

    m_sql_num = sql_num;       // 数据库连接池最大数量
    m_thread_num = thread_num; // 线程池最大数量

    m_log_write = log_write;    // 是否使用异步线程，1为异步
    m_OPT_LINGER = opt_linger;  // 是否优雅关闭连接，1为关闭时等待1s
    m_TRIGMode = trigmode;      // 指定触发模式，设置 m_LISTENTrigmode 和 m_CONNTrigmode
    m_close_log = close_log;    // 是否关闭日志，1为关闭
    m_actormodel = actor_model; // 网络模型，0:proactor 1:reactor
}

// 指定触发方式标志位
void WebServer::trig_mode()
{
    // LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

// 使用 Log::get_instance() 初始化一个单例LOG对象
// 使用 init() 初始化该LOG对象
void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        // 初始化日志
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

// 初始化 m_connPool 数据库连接池
// 调用 http_conn::initmysql_result 初始化`用户名-密码对`全局变量
void WebServer::sql_pool()
{
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initmysql_result(m_connPool);
}

// 初始化 m_pool 线程池，每个线程创建worker成员函数
void WebServer::thread_pool()
{
    // 线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

// 1. 创建 m_listenfd
// 2. 设置Socket属性: m_OPT_LINGER 选择关闭套接字时是否等待、允许端口复用、非阻塞
// 3. 命名Socket,绑定到本机端口
// 4. listen()监听Socket
// 5. utils.init(TIMESLOT) 设置
// 6. epoll_create()创建 m_epollfd，http_conn::m_epollfd = m_epollfd
// 7. 为m_pipefd[]创建双向管道，设置m_pipefd[1]为非阻塞
// 8. m_epollfd监听 m_listenfd 的读事件、对端关闭事件、多次监听、m_LISTENTrigmode = 1 设置边缘触发，否则为电平触发
// 9. m_epollfd监听 m_pipefd[0] 的读事件、对端关闭事件、多次监听、电平触发
// 10. 不处理SIGPIPE信号，设置重启系统调用
// 11. 接受 SIGALRM 由 utils.sig_handler 处理, 不重启系统调用
// 12. 接受 SIGTERM 由 utils.sig_handler 处理, 不重启系统调用
// 13. alarm() 定时 TIMESLOT 秒
// 14. Utils::u_pipefd = m_pipefd; Utils::u_epollfd = m_epollfd;


// SIGPIPE：进程尝试在被对端关闭的套接字上写数据 时触发
// SIGALRM: 定时器到期后产生
// SIGTERM：请求正常终止进程
// sig_handler信号处理函数，将 sig 参数通过管道u_pipefd[1]传输
void WebServer::eventListen()
{
    // 1. 创建TCP socket
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 2. 设置关闭套接字时是否等待
    if (0 == m_OPT_LINGER)
    {
        // 关闭套接字时不等待
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        // 若有数据要发送，则关闭套接字时等待1s
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 3. 允许端口复用
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    // 4. 命名socket
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;                // 地址族
    address.sin_addr.s_addr = htonl(INADDR_ANY); // IPv4地址，允许任何IP地址连接
    address.sin_port = htons(m_port);            // 端口号
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));

    // 5. 监听socket
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    // 设置最小超时时间
    utils.init(TIMESLOT);

    // 似乎没啥用？
    // epoll_event events[MAX_EVENT_NUMBER];

    // a. 创建内核事件表，返回事件表描述符
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    // b. 注册 m_listenfd 的读事件
    // 将 m_listenfd 添加到 m_epollfd 中
    // TRIGMode = 1 设置边缘触发，否则为电平触发
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    // 创建双向管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);

    // 将m_pipefd[0] 添加到 m_epollfd 中，注册读事件、电平触发、可多次使用
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);


    // SIGPIPE：进程尝试在被对端关闭的套接字上写数据 时触发
    // SIG_IGN表示不处理
    utils.addsig(SIGPIPE, SIG_IGN);

    // SIGALRM: 定时器到期后产生
    // SIGTERM：请求正常终止进程
    // utils.sig_handler: 向 m_pipefd[1] 发送 sig 参数
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    // 定时 TIMESLOT 秒
    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}


// 根据传入的参数初始化 users[connfd] 的 m_sockfd、m_address、doc_root、m_TRIGMode、m_close_log、sql_user、sql_user、sql_user
// 将 sockfd 添加到 epollfd 中，监听读事件、对端关闭事件、仅监听一次、非阻塞
// 若 m_CONNTrigmode = 1 设置边缘触发，否则为电平触发 

// 初始化 users_timer[connfd]的 address 和 sockfd
// 创建一个新的timer,到期时间 cur + 3 * TIMESLOT，赋值给 users_timer[connfd].timer
// 将这个定时器添加到 utils.m_timer_lst 中
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;

    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

// 将 timer 的到期时间往后推迟3个TIMESLOT
// 在 utils.m_timer_lst 中调整 timer 的位置，保持有序
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

// 执行 timer 的回调函数，传入的用户参数为 users_timer[sockfd]
// 删除 timer 定时器
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

// 处理客户端请求建立的连接
// 从 m_listenfd 中 accpet 一个连接到 connfd
// 若 http_conn::m_user_count >= MAX_FD，通过 connfd 发送错误信息后直接返回

// 根据传入的参数初始化 users[connfd] 的 m_sockfd、m_address、doc_root、m_TRIGMode、m_close_log、sql_user、sql_user、sql_user
// 将 sockfd 添加到 epollfd 中，监听读事件、对端关闭事件、仅监听一次、非阻塞
// 若 m_CONNTrigmode = 1 设置边缘触发，否则为电平触发 

// 初始化 users_timer[connfd] 的 address 和 sockfd 
// 创建一个新的 timer ,到期时间 cur + 3 * TIMESLOT ，赋值给 users_timer[connfd].timer 
// 将这个定时器添加到 utils.m_timer_lst 中
bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        // 初始化 users[connfd](HTTP连接类)， users_timer[connfd](用户数据)
        // 初始化 users_timer[connfd].timer 对应的定时器，到期时间为3个TIMESLOT以后
        // 将该定时器添加到 utils.m_timer_lst 中
        timer(connfd, client_address);
    }

    else
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

// 从 m_pipefd[0] 中接收数据，主要是信号量
// 如果接收到了SIGALRM，timeout = true
// 如果接收到了SIGTERM，stop_server = true
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

// reactor模式：
// 1.首先调整定时器，往后推迟3个单位
// 2.将对应的 http_conn* 放入线程池的工作队列，标志m_state为读
// 3.等待线程池的工作线程读取数据
// 如果读取失败:
    // 执行 timer 的回调函数，传入的用户参数为 users_timer[sockfd]
    // 删除 timer 定时器

// proactor模式:
// 读取网络数据，LT模式下只读取一次，ET模式下使用while循环读取
// 如果读取成功:
    // 将该事件放入请求队列,调整定时器
// 如果读取失败:
    // 执行 timer 的回调函数，传入的用户参数为 users_timer[sockfd]
    // 删除 timer 定时器
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // reactor
    if (1 == m_actormodel)
    {
        // 首先调整定时器，往后推迟3个单位
        if (timer)
        {
            adjust_timer(timer);
        }

        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        // proactor

        // 读取网络数据，LT模式下只读取一次，ET模式下使用while循环读取
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

// reactor模式：
// 1.调整定时器到期时间,往后推迟 3 个单位
// 2.将http_conn 添加到 m_workqueue队列中,设置 http_conn->m_state = 1
// 3.等待线程池写入完成，若写入失败，删除对应的定时器
    // 执行 timer 的回调函数，传入的用户参数为 users_timer[sockfd]
    // 删除 timer 定时器

// proactor模式:
// 写入数据
    // 若写入成功，定时器推迟3个单位
    // 若写入失败：
        // 执行 timer 的回调函数，传入的用户参数为 users_timer[sockfd]
        // 删除 timer 定时器  
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    // reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        // proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

// 1. 若 stop_server 为真，退出while循环
// 2. epoll_wait() 监听 m_epollfd
// 3. 若 m_listenfd 发生事件，处理新的客户连接
// 4. 若发生 EPOLLRDHUP | EPOLLHUP | EPOLLERR，服务器端关闭连接，移除对应的定时器
// 5. 若监听到 m_pipefd[0] 的 EPOLLIN，使用 dealwithsignal() 处理信号
// 6. 若监听到 通信SOCKET 的 EPOLLIN， 处理读事件
// 7. 若监听到 通信SOCKET 的 EPOLLOUT，处理写事件
// 8. 若监听到的是定时器到期信号：
    // 触发已过期的定时器事件函数，并将已过期的定时器从链表中移除
    // 使用alarm() 定时 m_TIMESLOT 后发送 SIGALRM 信号
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclientdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}

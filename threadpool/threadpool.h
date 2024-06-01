#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/

    // 1.初始化成员变量
    // 2.为 m_threads 动态分配线程
    // 3.pthread_create 创建线程运行worker成员函数，pthread_detach分离线程
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);

    // 回收m_threads分配的线程空间
    ~threadpool();

    // 将request 添加到 m_workqueue队列中
    // 设置 request->m_state = state;
    bool append(T *request, int state);

    // 将request 添加到 m_workqueue队列中
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/

    // 传递的是this指针，实际上执行的是run()成员函数
    static void *worker(void *arg);

    // 在while循环中，从请求队列中获取第一个任务，并进行处理
    void run();

private:
    // 这五个成员变量在构造函数中初始化
    int m_thread_number;         // 线程池中的线程数
    int m_max_requests;          // 请求队列中允许的最大请求数
    pthread_t *m_threads;        // 描述线程池的数组，其大小为m_thread_number
    connection_pool *m_connPool; // 数据库连接池
    int m_actor_model;           // 模型切换标志

    std::list<T *> m_workqueue; // 请求队列
    locker m_queuelocker;       // 保护请求队列的互斥锁
    sem m_queuestat;            // 是否有任务需要处理
};


// 1.初始化 m_actor_model、m_thread_number、m_max_requests、m_connPool 成员变量
// 2.为 m_threads 动态分配线程线程数组
// 3.pthread_create 创建线程，每一个线程都运行worker成员函数，并通过pthread_detach分离线程
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 回收m_threads分配的线程空间
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

// 将request 添加到 m_workqueue队列中
// 设置 request->m_state = state;
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 将request 添加到 m_workqueue队列中
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 传递的是this指针，实际上执行的是run()成员函数
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}


// 不断地从工作队列中获取第一个request
// 如果 m_actor_model = 1
    // 若 m_state = 0:
        //read_once()读取网络数据，process()组HTTP回复包，并映射到m_file_address处，设置 improv = 1
        //如果read_once()读取失败，设置 timer_flag = 1
    // 若 m_state = 1:
        //write()尝试写入数据
        //若不需要保持连接，timer_flag = 1
// 如果 m_actor_model = 0
    // process()组HTTP回复包，并映射到m_file_address处

template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }

        // 在while循环中，从请求队列中获取第一个任务，并进行处理
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;

        if (1 == m_actor_model)
        {
            if (0 == request->m_state)
            {
                // 读取网络数据，LT模式下只读取一次，ET模式下使用while循环读取
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);

                    // 从接收缓冲区读取数据，解析HTTP
                    // 根据m_url将需要显示的文件路径放在 m_real_file 中，并映射到m_file_address处
                    // 根据对应的HTTP状态码，组成HTTP数据包
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                // bytes_to_send为0，则将 m_sockfd 设置为 EPOLLIN ，调用init函数，返回
                // 否则调用 writev 持续发送数据，直到发送完成
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            // 从接收缓冲区读取数据，解析HTTP
            // 根据m_url将需要显示的文件路径放在 m_real_file 中，并映射到m_file_address处
            // 根据对应的HTTP状态码，组成HTTP数据包
            request->process();
        }
    }
}
#endif

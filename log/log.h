#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
    // C++11以后,创建静态变量，不需要加锁
    // C++保证静态变量初始化是线程安全的，不会有多个静态变量实例被创建
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    // 异步写线程的函数
    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }

    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    // 将文本写入log文件，分为 同步/异步两种方式
    void write_log(int level, const char *format, ...);

    // 强制刷新
    void flush(void);

private:
    Log();
    virtual ~Log();

    // 异步线程，从m_log_queue中去数据写入文件，如果数据不存在则休眠
    void *async_write_log()
    {
        string single_log;
        // 从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; // 路径名
    char log_name[128]; // log文件名

    long long m_count; // 日志行数记录
    int m_today;       // 因为按天分类,记录当前时间是那一天
    FILE *m_fp;        // 打开log的文件指针

    // init时初始化
    int m_log_buf_size; // 日志缓冲区大小
    char *m_buf;        // 日志缓冲区
    int m_split_lines;  // 日志最大行数

    block_queue<string> *m_log_queue; // 异步模式创建的阻塞队列
    bool m_is_async;                  // 是否同步标志位，初始化为false

    locker m_mutex;  // 保护所有的成员变量
    int m_close_log; // 关闭日志，为1则LOG宏不起作用
};

#define LOG_DEBUG(format, ...)                                    \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(0, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_INFO(format, ...)                                     \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(1, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_WARN(format, ...)                                     \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(2, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_ERROR(format, ...)                                    \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(3, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }

#endif

#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
    sem()
    {
        //sem_init初始化信号量，arg2=0 表示信号量是线程间共享
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception(); //抛出一个通用的异常
        }
    }
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    //阻塞当前线程，直到信号量的值大于0，并将其减1
    //返回是否等待成功
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }
    //增加信号量的值，表示释放资源
    //返回是否释放成功
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};


class locker
{
public:
    locker()
    {
        //arg2为NULL表示用默认参数初始化互斥锁
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};


class cond
{
public:
    cond()
    {
        //使用默认参数初始化条件变量
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }

    //pthread_cond_wait在等待期间自动释放互斥锁
    //在被唤醒后重新获取互斥锁
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    //pthread_cond_timedwait可以设定一个超时时间
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);

        //自动释放互斥锁，并在条件成立时试图上锁
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    //向等待在条件变量上的一个线程发送信号，这个线程是随机选择的
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }

    //向等待在条件变量上的所有线程发送信号    
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif

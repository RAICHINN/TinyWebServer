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
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state); //向请求队列中插入任务请求（Reactor）
    bool append_p(T *request); //向请求队列中插入任务请求（Reactor）

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //信号量，是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //事件处理模型 (reactor或proactor)
};

/* threadpool构造函数
actor_model: 事件处理模型: 1 reactor; 其他 proactor
connPppl: 
thread_number: 线程数量
 */
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0) 
        throw std::exception();
    //线程id初始化
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        //循环创建线程，并将工作线程按要求进行运行
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) //成功时返回0
        {
            delete[] m_threads;
            throw std::exception();
        }
        //将线程进行分离后，不用单独对工作线程进行回收
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}
/* 向工作队列添加任务请求
request: 要添加的请求
state: 请求的状态 0是读事件 1是写事件
 */
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    // 向请求队列加入请求，注意线程同步加锁
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 原子操作V，SV加1
    return true;
}
template <typename T>
bool threadpool<T>::append_p(T *request)
{// 向请求队列加入请求，注意线程同步加锁
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request); 
    m_queuelocker.unlock();
    m_queuestat.post(); // 原子操作V，SV加1
    return true;
}
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg; //？？？
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        //信号量P操作，SV减1，如果为0，将被阻塞（代表工作队列中没任务需要处理）
        m_queuestat.wait(); 
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        //从请求队列中取出第一个任务，并将任务从请求队列中删除
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        if (1 == m_actor_model) //m_actor_model为1，Reactor模式
        {
            //判断读写事件
            if (0 == request->m_state) //m_state为0，读事件
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else //m_state为1，写事件
            {
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
        else //另外一种actor model
        {
            //连接池RAII，从连接池中取出一个数据库连接
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process(); //process（模版类中的方法，这里是http类）进行处理
            //退出后由RAII类负责将数据库放回连接池
        }
    }
}
#endif

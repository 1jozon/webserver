#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template<typename T>
class threadpool
{
public:
    threadpool(int actor_model,connection_pool *connPool, int thread_number = 8,int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);
private:
    static void *worker(void *arg);
    void run();
    int m_thread_number;
    int m_max_requests;
    pthread_t *m_threads;
    std::list<T *>m_workerqueue;
    Locker mqueuelocker;
    connection_pool *m_connPool;
    sem m_queuestate;
    int m_actor_model;
};

template<typename T>
threadpool<T>::threadpool(int actor_model,connection_pool *connPool, int thhread_number, int max_requests):m_actor_model(actor_model),
m_thread_number(thread_number),m_max_requests(max_requests),m_thread(NULL),m_connPool(connPool)
{
    if(thhread_number <=0 || max_requests <= 0)
    {
        throw std::exception()
    }
    m_threads  = new pthread_t[m_thread_number];
    if(!m_threads)
    {
        throw std::exception();
    }
    for(int i = 0; i<m_thread_number; i++)
    {
        if(pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]) != 0)
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delate [] m_threads;

}

template<typename T>
bool threadpool<T>::append(T *request,int state)
{
    m_queuelocker.lock();
    if(m_workerqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestate.post();
    return true;
}

template<typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.losk();
    if(m_workerqueue.size() >= m_max_requests)
    {
        m_queuelock.unlock();
        return false;
    }
    m_workerqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestate.post();
    return true;
}

template<typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run()
{
    while(true)
    {
        m_queuestate.wait();
        m_queuelocker.lock();  
        if(m_workerqueue.empty())
        {
            m_queuestate.unlock();
            continue;
        }
        T *request = m_workerqueue.front();
        m_workerqueue.pop_front();
        m_queuelocker.unlock();
        if(!request)
        {
            continue;
        }
        if(1 == m_actor_model){
            if(0 == request->m_state)
            {
                if(request->read_once())
                {
                    request->mysql = m_connPool->GetConnection();
                    request->process();
                }
                else
                {
                 
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else{
                if(request->write())
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
            connectionRAII mysqlcon(&request->mysql,m_connPool);
            request->process();
        }
    }
}



#endif
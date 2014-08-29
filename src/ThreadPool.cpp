/*
 * =====================================================================================
 *
 *       Filename:  ThreadPool.cpp
 *
 *    Description:  jjjjjjjjj
 *
 *        Version:  1.0
 *        Created:  08/29/2014 10:43:36 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *        Company:  
 *
 * =====================================================================================
 */

#include <unistd.h>
#include <stdio.h>

#include "ThreadPool.h"

void* thread_ready(void *arg);


void ThreadPool::init(int min, int max)
{
    max = max == 0 ? 1 : max;
    max = max < min ? min : max;

    this->thread_func = NULL;

    this->minThreads = min;
    this->maxThreads = max;
    this->currThreads = 0;
    this->destroying = false;
    
    pthread_mutex_init(&this->lock, NULL);
    for (int i = 0; i < this->minThreads; ++i)
    {
        ThreadInfo * info = new ThreadInfo();
        if (!info)
        {
            printf("no more thread can be new\n");
            break;
        }
        //TODO:see if this init is ok
        pthread_cond_init(&(info->cond),NULL);
        info->pool = this;
        this->thread_list.push_back(info);
        this->currThreads++;
    }
}


void ThreadPool::destroy()
{
    if( thread_func )
    {
        this->destroying = true;
        while (1)
        {
            if (this->idle_list.size())
            {
                pthread_mutex_lock(&this->lock);
                while (this->idle_list.size())
                {
                    ThreadInfo * info = this->idle_list.front();
                    this->idle_list.pop_front();
                    //send signal to ThreadInfo?
                }
                pthread_mutex_unlock(&this->lock);
            }
            //still have thread? sleep for a while to wait for it to be idle
            if (this->currThreads)
            {
                usleep(100*1000);
            }
            else
            {
                break;
            }
        }//end of while
    }//end of if(thread_func)
    pthread_mutex_destroy(&this->lock);
}

void ThreadPool::start(ThreadFunc * func)
{
    if (!func)
        return;

    this->thread_func = func;
    //Do we need a lock here?
    for (int i=0; i < this->minThreads; ++i)
    {
        ThreadInfo * info = this->thread_list.at(i);
        
        if (pthread_create(&(info->th), NULL, thread_ready, (void*)info))
        {
            printf("pthread_create failed %df\n",i);
        }
    }
}

void ThreadPool::dispatch(void * arg)
{
    ThreadInfo * info;
    pthread_mutex_lock(&this->lock);
    
    if (this->idle_list.size())
    {
        if (this->currThreads < this->maxThreads)
            this->addWorker();
        pthread_mutex_unlock(&this->lock);
        return;
    }
    info = this->idle_list.front();
    this->idle_list.pop_front();
    info->arg = arg;
    pthread_cond_signal(&info->cond);
    pthread_mutex_unlock(&this->lock);


}


int ThreadPool::addWorker()
{
    ThreadInfo * info = new ThreadInfo();
    if (!info)
    {
        printf("new ThreadInfo failed\n");
        return -1;
    }
    pthread_cond_init(&info->cond,NULL);
    info->pool = this;
    
    pthread_mutex_lock(&this->lock);
    this->thread_list.push_back(info);
    pthread_mutex_unlock(&this->lock);

    if (pthread_create(&info->th, NULL, thread_ready ,(void*)info))
    {
        printf("addWorker,pthread_create failed\n");
        delete info;
        return -1;
    }
    return 0;
}

int ThreadPool::deleteWorker()
{
    pthread_mutex_lock(&this->lock);
    if (this->idle_list.size())
    {
        ThreadInfo * info = this->idle_list.front();
        this->idle_list.pop_front();
        info->existing = 1;
        pthread_cond_signal(&info->cond);
    }
    pthread_mutex_unlock(&this->lock);
    return 0;
}


void* thread_ready(void* arg)
{
    ThreadInfo * info = (ThreadInfo*) arg;
    ThreadPool * pool = info->pool;
    
    //MAYBE NEED TO GET REDIS CONNECTED
    //
    pthread_detach(pthread_self());

    pthread_mutex_lock(pool->get_lock());
    pool->currThreads++;
    pthread_mutex_unlock(pool->get_lock());
    
    while (!pool->isDestroying())
    {
        pthread_mutex_lock(pool->get_lock());
        pool->idle_list.push_back(info);
        pthread_cond_wait(&info->cond, pool->get_lock());
        pthread_mutex_unlock(pool->get_lock());

        if (pool->isDestroying() || info->existing)
        {
            break;
        }
        //Do the real job
        ThreadFunc* func = pool->thread_func;
        (*func)(arg);
    }
    //existing state
    pthread_mutex_lock(pool->get_lock());
    pool->currThreads--;
    //delete from thread_list
    for (int i=0; i < pool->thread_list.size(); ++i)
        if (pool->thread_list.at(i) == info)
            pool->thread_list.erase(pool->thread_list.begin()+i);

    pthread_mutex_unlock(pool->get_lock());

    return NULL;
}



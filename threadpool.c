#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include "threadpool.h"


typedef void *thread_func(thread_t *thread);


/* ============================ JOB QUEUE =========================== */



static struct job* jobqueue_pull(jobqueue_t *jobqueue_p);



/* Initialize queue */
static int jobqueue_init(jobqueue_t *jobqueue_p)
{
	jobqueue_p->len = 0;
	jobqueue_p->front = NULL;
	jobqueue_p->rear  = NULL;

    #if 0
	jobqueue_p->has_jobs = (struct bsem*)malloc(sizeof(struct bsem));
	if (jobqueue_p->has_jobs == NULL){
		return -1;
	}
    #endif

	pthread_mutex_init(&(jobqueue_p->rwmutex), NULL);
	//bsem_init(jobqueue_p->has_jobs, 0);

	return 0;
}


/* Clear the queue */
static void jobqueue_clear(jobqueue_t *jobqueue_p){

	while(jobqueue_p->len){
		free(jobqueue_pull(jobqueue_p));
	}

	jobqueue_p->front = NULL;
	jobqueue_p->rear  = NULL;
	//bsem_reset(jobqueue_p->has_jobs);
	jobqueue_p->len = 0;

}


/* Add (allocated) job to queue
 */
static void jobqueue_push(jobqueue_t *jobqueue_p, job_t *newjob)
{

	pthread_mutex_lock(&jobqueue_p->rwmutex);
	newjob->prev = NULL;

	switch(jobqueue_p->len){

		case 0:  /* if no jobs in queue */
					jobqueue_p->front = newjob;
					jobqueue_p->rear  = newjob;
					break;

		default: /* if jobs in queue */
					jobqueue_p->rear->prev = newjob;
					jobqueue_p->rear = newjob;

	}
	jobqueue_p->len++;

	//bsem_post(jobqueue_p->has_jobs);
	pthread_mutex_unlock(&jobqueue_p->rwmutex);
}


/* Get first job from queue(removes it from queue)
 * Notice: Caller MUST hold a mutex
 */
static struct job* jobqueue_pull(jobqueue_t *jobqueue_p)
{
    if(jobqueue_p == NULL)
    {
        fprintf(stderr, "invaild parameter\n");
        return NULL;
    }
    
    pthread_mutex_lock(&jobqueue_p->rwmutex);
    job_t *job_p = jobqueue_p->front;
    //printf("job len %d\n", jobqueue_p->len);
    switch(jobqueue_p->len){

		case 0:  /* if no jobs in queue */
		  			break;

		case 1:  /* if one job in queue */
					jobqueue_p->front = NULL;
					jobqueue_p->rear  = NULL;
					jobqueue_p->len = 0;
					break;

		default: /* if >1 jobs in queue */
					jobqueue_p->front = job_p->prev;
					jobqueue_p->len--;
					/* more than one job in queue -> post it */
					//bsem_post(jobqueue_p->has_jobs);

	}

    pthread_mutex_unlock(&jobqueue_p->rwmutex);
    //printf("%s end\n", __FUNCTION__);
    return job_p;
}


/* Free all queue resources back to the system */
static void jobqueue_destroy(jobqueue_t *jobqueue_p)
{
    jobqueue_clear(jobqueue_p);
}


static void jobqueue_free_job(job_t *job)
{
    //printf("%s begin\n", __FUNCTION__);
    if(job)
    {
        if(job->free_flag == 1)
        {
            free(job->arg);  // free user data pointer
        }
        
        free(job);
    }
    //printf("%s ends\n", __FUNCTION__);
}



void *thread_function(thread_t *thread)
{
    int ret = 0;
    sigset_t set;
    threadpool_t *tp = thread->tp;
    if(tp == NULL)
    {
        return NULL;
    }

    sigfillset(&set);
    sigdelset(&set, SIGILL);
    sigdelset(&set, SIGFPE);
    sigdelset(&set, SIGSEGV);
    sigdelset(&set, SIGBUS);
    ret = pthread_sigmask(SIG_BLOCK, &set, NULL);  // block many signals
    if(ret)
    {
        perror("sigmask fail");
        return NULL;
    }
    
    while(tp->terminated != THREADPOOL_TERMINATED)
    {
        pthread_mutex_lock(tp->mutex);
        while(tp->queue->len == 0)
        {
            //printf("begin wait %s at %d\n", __FUNCTION__, __LINE__);
            thread->status = THREAD_BLOCKED;
            pthread_cond_wait(tp->cond, tp->mutex);
            thread->status = THREAD_NORMAL; // set thread status to normal running
        }
        //printf("wake up %s at %d\n", __FUNCTION__, __LINE__);
        if(tp->terminated == THREADPOOL_TERMINATED)
        {
            break;
        }
        
        // get a job from queue
        job_t *new = jobqueue_pull(tp->queue);
        pthread_mutex_unlock(tp->mutex);

        if(new)
        {    
            void *user_data = new->arg;
            job_handler handler = new->func;
            if(handler != NULL)
            {
                handler(user_data);
                jobqueue_free_job(new);
                new = NULL;
            }
            
        }
    }

    pthread_mutex_unlock(tp->mutex);  // for break unlock

    // thread is terminated
    thread->status = THREAD_EXITED;

    // thread exit
    return NULL;
}

int thread_init(threadpool_t *tp, int index, thread_func function)
{
    static int id = 1;
    int ret = 0;
    if(tp == NULL || index < 0 || function == NULL)
    {
        return -1;
    }
    
    thread_t *thd = malloc(sizeof(*thd));
    memset(thd, 0, sizeof(*thd));

    thd->tp = tp;
    
    pthread_mutex_lock(tp->mutex);
    thd->id = id++;
    *(tp->threads + index) = thd;
    pthread_mutex_unlock(tp->mutex);
    
    // create pthread
    pthread_t pid;
    ret = pthread_create(&pid, NULL, (void *(*)(void *))thread_function, (void *)thd);
    if(ret == 0)
    {
        thd->pid = pid;
        pthread_mutex_lock(tp->mutex);
        tp->thread_cnt += 1;
        pthread_mutex_unlock(tp->mutex);
        
        //printf("pthread_create success\n");
        pthread_detach(pid);
        
        
    }
    else
    {
        perror("pthread create fail");
    }

    printf("%s end, index %d\n", __FUNCTION__, index);
    return ret;    
}

threadpool_t *threadpool_init(int thread_num)
{
    int i = 0;
    threadpool_t *tp = malloc(sizeof(*tp));
    CHECK_NULL_RETURN(tp, NULL);
    memset(tp, 0, sizeof(*tp));
    tp->thread_cnt = 0;

    tp->terminated = THREADPOOL_NORMAL;  // terminate command 
    tp->mutex = malloc(sizeof(pthread_mutex_t));
    if(tp->mutex == NULL)
    {
        goto fail;
    }
    pthread_mutex_init(tp->mutex, NULL);

    tp->cond = malloc(sizeof(pthread_cond_t));
    if(tp->cond == NULL)
    {
        goto fail;
    }
    pthread_cond_init(tp->cond, NULL);

    // queue init shoud before thread
    tp->queue = malloc(sizeof(jobqueue_t));
    if(tp->queue == NULL)
    {
        goto fail;
    }
    jobqueue_init(tp->queue);

    tp->threads = (thread_t **)malloc(sizeof(thread_t *) * thread_num);
    if(tp->threads == NULL)
    {
        goto fail;
    }
    // thread init
    for(i = 0; i < thread_num; i++)
    {
        thread_init(tp, i, thread_function);    
    }

    while(tp->thread_cnt != thread_num)
    {
        //printf("tp->thread_cnt %d\n", tp->thread_cnt);
        sleep(1);
    }

    return tp;

fail:
    if(tp->mutex != NULL)
    {
        free(tp->mutex);
    }

    if(tp->cond != NULL)
    {
        free(tp->cond);
    }

    if(tp->queue != NULL)
    {
        free(tp->queue);
    }

    if(tp->threads != NULL)
    {
        free(tp->threads);
    }

    if(tp != NULL)
    {
        free(tp);
    }
    return NULL;
}


int threadpool_add_job(threadpool_t *pool, job_t *job)
{
    if(pool == NULL || job == NULL)
    {
        return -1;   
    }

    jobqueue_push(pool->queue, job);
    // signal cond after add a job
    pthread_cond_signal(pool->cond);
    return 0;
}

void threadpool_wait(threadpool_t *pool)
{
    //TODO
}

void threadpool_stop(threadpool_t *pool)
{
    int i;
    int ret = 0;
    if(pool == NULL)
    {
        return;
    }
    
    
    pthread_mutex_lock(pool->mutex);
    pool->terminated = THREADPOOL_TERMINATED;
    int count = pool->thread_cnt;

    for(i = 0; i < count; i++)
    {
        thread_t *thd = *(pool->threads + i);
        //printf("thread status %d\n", thd->status);
        if(thd && thd->status != THREAD_EXITED)
        {
            //printf("before pthread_cancel\n");
            // there may happend segment fault
            ret = pthread_cancel(thd->pid);
            if(ret != 0)
            {
                perror("thread cancel fail\n");
            }
            
        }
        
    }
    
    pthread_mutex_unlock(pool->mutex);
    
}


void threadpool_destory(threadpool_t *pool)
{
    threadpool_stop(pool);
    printf("stop done\n");

    // must use trylock
    int ret = pthread_mutex_trylock(pool->mutex);
    if(ret != 0)
    {
        printf("lock fail ret %d at %s\n", ret, __FUNCTION__);
    }
    
    jobqueue_destroy(pool->queue);
    //printf("jobqueue_destroy end\n");
    pool->queue = NULL;
    pthread_mutex_unlock(pool->mutex);
    
    if(pool)
    {
        free(pool->mutex);
        free(pool->cond);
        free(pool->threads);
        free(pool);
        pool = NULL;
    }

    
}


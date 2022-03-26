#ifndef __THREAD_POOL_H
#define __THREAD_POOL_H
#include <pthread.h>

typedef enum
{
    THREADPOOL_NORMAL = 0,
    THREADPOOL_TERMINATED    
} tp_status_t;


typedef enum
{
    THREAD_NORMAL = 0,
    THREAD_BLOCKED,
    THREAD_EXITED
} thd_status_t;

#define CHECK_NULL_RETURN(ptr, ret) do \
{                                      \
    if(ptr == NULL) return ret;        \
} while(0)
// job function pointer type
typedef void (*job_handler)(void* arg);

typedef struct job
{
    unsigned int jid;                    // job id
    struct job*  prev;                   /* pointer to previous job   */
    job_handler func;       /* function pointer          */
    void*  arg;                          /* function's argument       */
    unsigned char free_flag;             // 0 dont need to free 1 free
} job_t;


/* Job queue */
typedef struct jobqueue
{
    pthread_mutex_t rwmutex;             /* used for queue r/w access */
    job_t  *front;                         /* pointer to front of queue */
    job_t  *rear;                          /* pointer to rear  of queue */
    //bsem *has_jobs;                      /* flag as binary semaphore  */
    volatile int len;                           /* number of jobs in queue   */
} jobqueue_t;


typedef struct threadpool threadpool_t;

typedef struct
{
    int id;
    pthread_t pid;
    threadpool_t *tp;
    thd_status_t status;
} thread_t;


struct threadpool
{
    volatile int thread_cnt; // thread number
    volatile int active_cnt; // the count of running thread
    volatile int terminated;  // the flag of exit thread
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    thread_t **threads;  // thread pointer array
    jobqueue_t *queue;     // job queue
};




/** @brief thread pool initialize
 *         
 *  @param thread_num, spelize the number of thread
 *  @return pool context, the pointer to threadpool_t
 */
threadpool_t *threadpool_init(int thread_num);

/** @brief adjust the number of worker threads
 *
 *  @param pool, the pointer to threadpool context
 *  @param thread_num, expect how many threads to work
 *  @return 0 if success, else -1
 *  @note it will stop all threads first, then start them, but job queue is reserved
 */
int threadpool_adjust_workers(threadpool_t *pool, int thread_num);


/** @brief add a job to thread pool
 *
 *  @param pool, the pointer to threadpool context
 *  @param job, a work job
 *  @return 0 if success, else -1
 */
int threadpool_add_job(threadpool_t *pool, job_t *job);

/** @brief cancel all work threads
 *
 *  @param pool, the pointer to threadpool context
 *  @return void
 *  @note only cancel threads, memory and queue are reserved
 */
void threadpool_stop(threadpool_t *pool);

/** @brief destory the thread pool
 *
 *  @param pool, the pointer to threadpool context
 *  @return void
 */
void threadpool_destory(threadpool_t *pool);
#endif
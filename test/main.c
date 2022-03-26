#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../threadpool.h"


#define TEST_JOB_NUMBER 10000
#define TEST_THREAD_NUMBER 6

void user_func(void *arg)
{
    printf("arg %d\n", (int)arg);
    //sleep(1);
    //printf("%s end\n", __FUNCTION__);
}

int main()
{

    threadpool_t *tp = threadpool_init(TEST_THREAD_NUMBER);

    printf("tp thread cnt %d\n", tp->thread_cnt);

    int i = 0;
    for(i = 0; i < TEST_JOB_NUMBER; i++)
    {
        job_t *job = malloc(sizeof(job_t));
        if(job)
        {
            job->arg = (void *)i;
            job->func = user_func;
            job->free_flag = 0;
            
            threadpool_add_job(tp, job);
        }
        
    }


    sleep(5);
    threadpool_destory(tp);
    

}

#include "app_task_lifecycle.h"

#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t s_taskThreadMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_taskThreadCondition = PTHREAD_COND_INITIALIZER;
static size_t s_activeTaskThreads = 0;

void taskThreadLifecycleBegin(void)
{
    pthread_mutex_lock(&s_taskThreadMutex);
    ++s_activeTaskThreads;
    pthread_mutex_unlock(&s_taskThreadMutex);
}

void taskThreadLifecycleCancelBegin(void)
{
    taskThreadLifecycleFinish();
}

void taskThreadLifecycleDetachCurrent(void)
{
    int result = pthread_detach(pthread_self());
    if (result != 0)
    {
        printf("task: pthread_detach failed: %d\n", result);
    }
}

void taskThreadLifecycleFinish(void)
{
    pthread_mutex_lock(&s_taskThreadMutex);
    if (s_activeTaskThreads > 0)
    {
        --s_activeTaskThreads;
    }
    if (s_activeTaskThreads == 0)
    {
        pthread_cond_broadcast(&s_taskThreadCondition);
    }
    pthread_mutex_unlock(&s_taskThreadMutex);
}

void taskThreadLifecycleWaitForAll(void)
{
    pthread_mutex_lock(&s_taskThreadMutex);
    while (s_activeTaskThreads != 0)
    {
        pthread_cond_wait(&s_taskThreadCondition, &s_taskThreadMutex);
    }
    pthread_mutex_unlock(&s_taskThreadMutex);
}

size_t taskThreadLifecycleActiveCount(void)
{
    pthread_mutex_lock(&s_taskThreadMutex);
    size_t count = s_activeTaskThreads;
    pthread_mutex_unlock(&s_taskThreadMutex);
    return count;
}

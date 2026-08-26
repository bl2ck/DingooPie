#include "shared/execution/thread_join.h"

#include <errno.h>
#include <time.h>

static timespec realtimeDeadline(uint32_t timeoutMs)
{
    timespec deadline = {};
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeoutMs / 1000;
    deadline.tv_nsec += (long)(timeoutMs % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}

void runtimeThreadCompletionReset(RuntimeThreadCompletion* completion)
{
    if (!completion)
    {
        return;
    }
    pthread_mutex_lock(&completion->mutex);
    completion->finished = false;
    pthread_mutex_unlock(&completion->mutex);
}

void runtimeThreadCompletionSignal(RuntimeThreadCompletion* completion)
{
    if (!completion)
    {
        return;
    }
    pthread_mutex_lock(&completion->mutex);
    completion->finished = true;
    pthread_cond_broadcast(&completion->condition);
    pthread_mutex_unlock(&completion->mutex);
}

RuntimeThreadJoinResult runtimeThreadJoinWithTimeout(
    pthread_t thread,
    RuntimeThreadCompletion* completion,
    uint32_t timeoutMs,
    int* errorCode)
{
    if (errorCode)
    {
        *errorCode = EINVAL;
    }
    if (!completion)
    {
        return RUNTIME_THREAD_JOIN_ERROR;
    }

    const timespec deadline = realtimeDeadline(timeoutMs);
    pthread_mutex_lock(&completion->mutex);
    int waitResult = 0;
    while (!completion->finished && waitResult == 0)
    {
        waitResult = pthread_cond_timedwait(
            &completion->condition, &completion->mutex, &deadline);
    }
    const bool finished = completion->finished;
    pthread_mutex_unlock(&completion->mutex);

    if (!finished)
    {
        if (errorCode)
        {
            *errorCode = waitResult;
        }
        if (waitResult == ETIMEDOUT)
        {
            return RUNTIME_THREAD_JOIN_TIMEOUT;
        }
        return RUNTIME_THREAD_JOIN_ERROR;
    }

    const int result = pthread_join(thread, NULL);
    if (errorCode)
    {
        *errorCode = result;
    }
    return result == 0 ? RUNTIME_THREAD_JOINED : RUNTIME_THREAD_JOIN_ERROR;
}

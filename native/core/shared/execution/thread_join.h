#ifndef DINGOO_PIE_SHARED_EXECUTION_THREAD_JOIN_H
#define DINGOO_PIE_SHARED_EXECUTION_THREAD_JOIN_H

#include <pthread.h>
#include <stdint.h>

struct RuntimeThreadCompletion
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool finished;
};

#define RUNTIME_THREAD_COMPLETION_INITIALIZER \
    { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, false }

enum RuntimeThreadJoinResult
{
    RUNTIME_THREAD_JOINED = 0,
    RUNTIME_THREAD_JOIN_TIMEOUT,
    RUNTIME_THREAD_JOIN_ERROR
};

void runtimeThreadCompletionReset(RuntimeThreadCompletion* completion);
void runtimeThreadCompletionSignal(RuntimeThreadCompletion* completion);
RuntimeThreadJoinResult runtimeThreadJoinWithTimeout(
    pthread_t thread,
    RuntimeThreadCompletion* completion,
    uint32_t timeoutMs,
    int* errorCode);

#endif

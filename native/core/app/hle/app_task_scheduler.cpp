#include "app/hle/app_task_scheduler.h"
#include <assert.h>
#include "app/hle/app_task_lifecycle.h"
#include "app/memory/app_memory.h"
#include <pthread.h>
#include <SDL2/SDL.h>
#include "app/cpu/mips_runtime.h"
#include "shared/execution/execution_backend.h"
#include "frontend/video/framebuffer.h"
#include "app/memory/app_framebuffer_mapping.h"
#include "shared/diagnostics/runtime_log.h"
#include "app/hle/app_hle.h"
#include "app/runtime/app_runtime_debug.h"
#include "shared/services/guest_package.h"
#include "app/runtime/app_runtime_context.h"
#include <cstdlib>
#include <cstring>
#include <vector>


static SDL_atomic_t s_taskShutdownRequested;
static pthread_mutex_t s_taskRuntimeMutex = PTHREAD_MUTEX_INITIALIZER;
static std::vector<NativeRuntime*> s_taskRuntimes;

struct TaskStruct
{
    pthread_t tid;
    uint32_t taskFuncAddr;
    uint32_t dataPtr;
    uint32_t stackPtr;
    uint32_t priority;
    uint64_t profileLastTicks;
    uint64_t profileInstructionCount;
};

static bool taskProfileEnabled()
{
    static const bool enabled = []() {
        const char* value = getenv("DINGOO_PIE_TASK_PROFILE");
        return value && value[0] && strcmp(value, "0") != 0;
    }();
    return enabled;
}

static ExecutionBackend subtaskBackendFromEnv()
{
    bool recognized = false;
    const char* value = getenv("DINGOO_PIE_SUBTASK_BACKEND");
    ExecutionBackend backend = executionBackendFromName(value, &recognized);
    if (!value || !value[0])
    {
        return EXECUTION_BACKEND_COMPATIBILITY;
    }
    if (!recognized)
    {
        printf("task: invalid DINGOO_PIE_SUBTASK_BACKEND='%s'; using compatibility mode\n",
            value);
        return EXECUTION_BACKEND_COMPATIBILITY;
    }
    if (backend == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        // The PPSSPP adapter still owns global CPU/JIT state, so running host
        // pthread-backed Dingoo tasks through it serializes or corrupts state.
        printf("task: ppsspp_irjit uses process-global state; using compatibility mode for subtask\n");
        return EXECUTION_BACKEND_COMPATIBILITY;
    }
    return backend;
}

void taskSchedulerResetShutdown(void)
{
    SDL_AtomicSet(&s_taskShutdownRequested, 0);
}

void taskSchedulerRequestShutdown(const char* reason)
{
    int wasRequested = SDL_AtomicSet(&s_taskShutdownRequested, 1);
    if (!wasRequested)
    {
        printf("task: shutdown requested by %s\n", reason ? reason : "<unknown>");
    }

    pthread_mutex_lock(&s_taskRuntimeMutex);
    const size_t runtimeCount = s_taskRuntimes.size();
    for (size_t i = 0; i < runtimeCount; ++i)
    {
        nativeRuntimeRequestStop(s_taskRuntimes[i]);
    }
    pthread_mutex_unlock(&s_taskRuntimeMutex);
    if (runtimeCount != 0)
    {
        printf("task: shutdown stop requested for %u subtask runtime(s)\n",
            (unsigned int)runtimeCount);
    }
}

bool taskSchedulerIsShutdownRequested(void)
{
    return SDL_AtomicGet(&s_taskShutdownRequested) != 0;
}

void taskSchedulerRegisterRuntime(NativeRuntime* runtime)
{
    if (!runtime)
    {
        return;
    }
    pthread_mutex_lock(&s_taskRuntimeMutex);
    for (size_t i = 0; i < s_taskRuntimes.size(); ++i)
    {
        if (s_taskRuntimes[i] == runtime)
        {
            pthread_mutex_unlock(&s_taskRuntimeMutex);
            return;
        }
    }
    s_taskRuntimes.push_back(runtime);
    if (taskSchedulerIsShutdownRequested())
    {
        nativeRuntimeRequestStop(runtime);
    }
    pthread_mutex_unlock(&s_taskRuntimeMutex);
}

void taskSchedulerUnregisterRuntime(NativeRuntime* runtime)
{
    pthread_mutex_lock(&s_taskRuntimeMutex);
    for (std::vector<NativeRuntime*>::iterator it = s_taskRuntimes.begin();
         it != s_taskRuntimes.end(); ++it)
    {
        if (*it == runtime)
        {
            s_taskRuntimes.erase(it);
            break;
        }
    }
    pthread_mutex_unlock(&s_taskRuntimeMutex);
}

size_t taskSchedulerRuntimeCount(void)
{
    pthread_mutex_lock(&s_taskRuntimeMutex);
    size_t count = s_taskRuntimes.size();
    pthread_mutex_unlock(&s_taskRuntimeMutex);
    return count;
}

void taskSchedulerSnapshotRuntimes(std::vector<NativeRuntime*>* out)
{
    if (!out)
    {
        return;
    }
    pthread_mutex_lock(&s_taskRuntimeMutex);
    *out = s_taskRuntimes;
    pthread_mutex_unlock(&s_taskRuntimeMutex);
}

static void hookTaskProfile(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)runtime;
    (void)address;
    (void)size;

    TaskStruct* taskStruct = (TaskStruct*)userData;
    if (!taskStruct)
    {
        return;
    }

    if (!taskStruct->profileLastTicks)
    {
        taskStruct->profileLastTicks = SDL_GetTicks64();
    }

    taskStruct->profileInstructionCount++;
    uint64_t now = SDL_GetTicks64();
    uint64_t elapsedMs = now - taskStruct->profileLastTicks;
    if (elapsedMs >= runtimeLogProfileIntervalMs())
    {
        printf("profile:task entry=0x%08x priority=%u instr=%llu/s\n",
            taskStruct->taskFuncAddr,
            taskStruct->priority,
            (unsigned long long)runtimeLogRatePerSecond(
                taskStruct->profileInstructionCount, elapsedMs));
        taskStruct->profileInstructionCount = 0;
        taskStruct->profileLastTicks = now;
    }
}

static bool hookInvalidMemory(NativeRuntime* runtime, RuntimeMemoryAccess type,
    uint64_t address, int size, int64_t value, void* userData)
{
    (void)userData;
    appRuntimeDebugReportInvalidMemory(runtime, type, address, size, value);
    return false;
}

void* subTaskRun(void* data)
{
    taskThreadLifecycleDetachCurrent();

    struct TaskCompletionGuard
    {
        ~TaskCompletionGuard()
        {
            taskThreadLifecycleFinish();
        }
    } completionGuard;
    TaskStruct* taskStruct = (TaskStruct*)data;

    NativeRuntime* runtime = NULL;
    struct TaskResourceGuard
    {
        TaskStruct* task;
        NativeRuntime* runtime;

        ~TaskResourceGuard()
        {
            if (runtime)
            {
                taskSchedulerUnregisterRuntime(runtime);
                nativeRuntimeDestroy(runtime);
            }
            free(task);
        }
    } resourceGuard = { taskStruct, NULL };

    uint32_t entry = taskStruct->taskFuncAddr;

    RuntimeError err;
    RuntimeHook trace;

    printf("task: subTaskRun start entry=0x%08x priority=%d\n", entry, taskStruct->priority);
    if (taskSchedulerIsShutdownRequested())
    {
        printf("task: subTaskRun ignored during shutdown entry=0x%08x priority=%d\n",
            entry, taskStruct->priority);
        return NULL;
    }

    err = nativeRuntimeCreate(&runtime);
    if (err)
    {
        printf("task: nativeRuntimeCreate failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return NULL;
    }
    resourceGuard.runtime = runtime;
    taskSchedulerRegisterRuntime(runtime);

    ExecutionBackend backend = subtaskBackendFromEnv();
    err = nativeRuntimeSetBackend(runtime, backend);
    if (err)
    {
        printf("task: nativeRuntimeSetBackend failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return NULL;
    }
    printf("task: subTaskRun backend=%s\n", executionBackendName(backend));

    const AppRuntimeProgramImage image = appRuntimeProgramImage();
    if (!image.data || !image.size || !image.package)
    {
        printf("task: APP program image is unavailable\n");
        return NULL;
    }
    err = nativeRuntimeMapMemory(runtime, image.address, image.size, RUNTIME_PROT_ALL, image.data);
    if (err)
    {
        printf("task: failed to map app memory: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return NULL;
    }

    uint32_t appAliasAddr = image.address & 0x1fffffff;
    err = nativeRuntimeMapMemory(runtime, appAliasAddr, image.size, RUNTIME_PROT_ALL, image.data);
    if (err)
    {
        printf("task: failed to map app alias: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return NULL;
    }

    if (appMemoryMapTaskRuntime(runtime))
    {
        printf("task: appMemoryMapTaskRuntime failed\n");
        return NULL;
    }

    if (appFramebufferInitialize(runtime))
    {
        printf("task: framebuffer initialization failed\n");
        return NULL;
    }

    err = bridge_init_task(runtime, image.package, false);
    if (err)
    {
        printf("task: bridge_init failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return NULL;
    }

    nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_MEM_INVALID,
        (void*)hookInvalidMemory, NULL, 1, 0);
    if (taskProfileEnabled())
    {
        nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookTaskProfile, taskStruct, 1, 0xffffffffu);
    }

    uint32_t sp = taskStruct->stackPtr;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_SP, &sp);

    uint32_t a0 = taskStruct->dataPtr;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A0, &a0);

    uint32_t t9 = entry;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_T9, &t9);

    err = nativeRuntimeStart(runtime, entry, 0xFFFFFFFF, 0, 0);
    if (err)
    {
        if (taskSchedulerIsShutdownRequested())
        {
            printf("task: subTaskRun stopped during shutdown entry=0x%08x error=%u (%s)\n",
                entry, err, nativeRuntimeErrorString(err));
        }
        else
        {
            printf("task: nativeRuntimeStart failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        }
        return NULL;
    }

    return 0;
}

uint32_t OSTaskCreate(uint32_t taskFuncAddr, uint32_t dataPtr, uint32_t stackPtr, uint32_t priority)
{
    if (taskSchedulerIsShutdownRequested())
    {
        printf("task: OSTaskCreate ignored during shutdown entry=0x%08x priority=%u\n",
            taskFuncAddr, priority);
        return OS_NO_ERR;
    }

    taskThreadLifecycleBegin();

    TaskStruct* taskStruct = (TaskStruct*)malloc(sizeof(TaskStruct));
    if (taskStruct == NULL)
    {
        printf("task: OSTaskCreate malloc failed\n");
        taskThreadLifecycleCancelBegin();
        return -1;
    }
    taskStruct->dataPtr = dataPtr;
    taskStruct->taskFuncAddr = taskFuncAddr;
    taskStruct->stackPtr = stackPtr;
    taskStruct->priority = priority;
    taskStruct->profileLastTicks = 0;
    taskStruct->profileInstructionCount = 0;

    int ret = pthread_create(&taskStruct->tid, NULL, subTaskRun, taskStruct);
    if (ret)
    {
        printf("task: pthread_create subTaskRun failed: %d\n", ret);
        free(taskStruct);
        taskThreadLifecycleCancelBegin();
        assert(0);
        return (uint32_t)-1;
    }

    return OS_NO_ERR;
}

void taskSchedulerWaitForTasks(void)
{
    taskThreadLifecycleWaitForAll();
}

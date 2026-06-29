#include "task_scheduler.h"
#include <assert.h>
#include "emulated_memory.h"
#include <pthread.h>
#include <SDL2/SDL.h>
#include "native_runtime.h"
#include "execution_backend.h"
#include "framebuffer.h"
#include "sdk_hle.h"
#include "app_loader.h"
#include <cstdlib>
#include <cstring>
#include <vector>

extern uint32_t s_AppDataAddr;
extern uint32_t s_AppDataBuffSize;
extern void* s_AppDataBuff;
extern app* s_app;

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
};

static bool taskProfileEnabled()
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_TASK_PROFILE");
        enabled = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    }
    return enabled != 0;
}

static ExecutionBackend subtaskBackendFromEnv()
{
    bool recognized = false;
    const char* value = getenv("DINGOO_PIE_SUBTASK_BACKEND");
    ExecutionBackend backend = executionBackendFromName(value, &recognized);
    if (!value || !value[0])
    {
        return EXECUTION_BACKEND_INTERPRETER;
    }
    if (!recognized)
    {
        printf("task: unknown DINGOO_PIE_SUBTASK_BACKEND, using interpreter\n");
        return EXECUTION_BACKEND_INTERPRETER;
    }
    if (backend == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        // The PPSSPP adapter still owns global CPU/JIT state, so running host
        // pthread-backed Dingoo tasks through it serializes or corrupts state.
        printf("task: ppsspp_irjit is not thread-local; using interpreter for subtask\n");
        return EXECUTION_BACKEND_INTERPRETER;
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
    out->clear();
    pthread_mutex_lock(&s_taskRuntimeMutex);
    *out = s_taskRuntimes;
    pthread_mutex_unlock(&s_taskRuntimeMutex);
}

static void hook_task_profile(NativeRuntime* runtime, uint64_t address, uint32_t size, void* user_data)
{
    (void)runtime;
    (void)address;
    (void)size;

    TaskStruct* taskStruct = (TaskStruct*)user_data;
    static uint64_t lastTicks = 0;
    static uint64_t instructionCount = 0;

    if (!lastTicks)
    {
        lastTicks = SDL_GetTicks64();
    }

    instructionCount++;
    uint64_t now = SDL_GetTicks64();
    if (now - lastTicks >= 1000)
    {
        printf("profile task: entry=0x%08x priority=%u instr=%llu/s\n",
            taskStruct ? taskStruct->taskFuncAddr : 0,
            taskStruct ? taskStruct->priority : 0,
            (unsigned long long)instructionCount);
        instructionCount = 0;
        lastTicks = now;
    }
}

static bool hook_mem_invalid(NativeRuntime* runtime, RuntimeMemoryAccess type, uint64_t address, int size, int64_t value, void* user_data)
{
    printf(">>> Tracing mem_invalid mem_type:%s at 0x%" PRIx64 ", size:0x%x, value:0x%" PRIx64 "\n",
        memTypeStr(type), address, size, value);
    dumpREG(runtime);
    dumpAsm(runtime);
    return false;
}

void* subTaskRun(void* data)
{
    struct TaskStruct* taskStruct = (struct TaskStruct*)data;

    uint32_t entry = taskStruct->taskFuncAddr;

    NativeRuntime* runtime;
    RuntimeError err;
    RuntimeHook trace;

    printf("subTaskRun start:entry 0x%08x, priority %d\n", entry, taskStruct->priority);
    if (taskSchedulerIsShutdownRequested())
    {
        printf("subTaskRun ignored during shutdown: entry 0x%08x, priority %d\n",
            entry, taskStruct->priority);
        free(taskStruct);
        return NULL;
    }

    err = nativeRuntimeCreate(&runtime);
    if (err)
    {
        printf("Failed on nativeRuntimeCreate() with error returned: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return NULL;
    }
    taskSchedulerRegisterRuntime(runtime);

    ExecutionBackend backend = subtaskBackendFromEnv();
    err = nativeRuntimeSetBackend(runtime, backend);
    if (err)
    {
        printf("Failed on nativeRuntimeSetBackend() with error returned: %u (%s)\n", err, nativeRuntimeErrorString(err));
        taskSchedulerUnregisterRuntime(runtime);
        nativeRuntimeDestroy(runtime);
        free(taskStruct);
        return NULL;
    }
    printf("subTaskRun backend: %s\n", executionBackendName(backend));

    err = nativeRuntimeMapMemory(runtime, s_AppDataAddr, s_AppDataBuffSize, RUNTIME_PROT_ALL, s_AppDataBuff);
    if (err)
    {
        printf("Failed mem map app: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    uint32_t appAliasAddr = s_AppDataAddr & 0x1fffffff;
    err = nativeRuntimeMapMemory(runtime, appAliasAddr, s_AppDataBuffSize, RUNTIME_PROT_ALL, s_AppDataBuff);
    if (err)
    {
        printf("Failed alias mem map: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    if (InitVmMemSubTask(runtime))
    {
        printf("Failed on InitVmMemSubTask\n");
        exit(1);
    }

    if (InitFb(runtime))
    {
        printf("Failed on InitFb\n");
        exit(1);
    }

    err = bridge_init_task(runtime, s_app, false);
    if (err)
    {
        printf("Failed bridge_init(): %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_MEM_INVALID, (void*)hook_mem_invalid, NULL, 1, 0);
    if (taskProfileEnabled())
    {
        nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hook_task_profile, taskStruct, 1, 0xffffffffu);
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
            printf("subTaskRun stopped during shutdown: entry 0x%08x, error %u (%s)\n",
                entry, err, nativeRuntimeErrorString(err));
        }
        else
        {
            printf("Failed on nativeRuntimeStart() with error returned: %u (%s)\n", err, nativeRuntimeErrorString(err));
        }
        taskSchedulerUnregisterRuntime(runtime);
        nativeRuntimeDestroy(runtime);
        free(taskStruct);
        return NULL;
    }

    taskSchedulerUnregisterRuntime(runtime);
    nativeRuntimeDestroy(runtime);
    free(taskStruct);
    return 0;
}

uint32_t OSTaskCreate(uint32_t taskFuncAddr, uint32_t dataPtr, uint32_t stackPtr, uint32_t priority)
{
    if (taskSchedulerIsShutdownRequested())
    {
        printf("OSTaskCreate ignored during shutdown: entry 0x%08x, priority %u\n",
            taskFuncAddr, priority);
        return OS_NO_ERR;
    }

    struct TaskStruct* taskStruct =(struct TaskStruct*)malloc(sizeof(struct TaskStruct));
    if (taskStruct == NULL)
    {
        printf("OSTaskCreate malloc failed\n");
        return -1;
    }
    taskStruct->dataPtr = dataPtr;
    taskStruct->taskFuncAddr = taskFuncAddr;
    taskStruct->stackPtr = stackPtr;
    taskStruct->priority = priority;

    int ret = pthread_create(&taskStruct->tid, NULL, subTaskRun, taskStruct);
    if (ret)
    {
        printf("pthread_create subTaskRun failed\n");
        assert(0);
    }

    return OS_NO_ERR;
}


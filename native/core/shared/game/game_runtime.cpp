#include "shared/game/game_runtime.h"

#include "app/runtime/app_runtime.h"
#include "app/hle/app_hle.h"
#include "cc/runtime/cc_runtime.h"
#include "cc/save/cc_save_state.h"
#include "app/save/app_save_state.h"
#include "shared/execution/thread_join.h"
#include "shared/services/audio_output.h"
#include "runtime_resource_monitor.h"
#include "sdl_frontend.h"

#include <limits>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t g_gameRuntimeMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_ccRuntimeThread;
static bool g_ccRuntimeThreadStarted = false;
static GameFormat g_activeGameFormat = GAME_FORMAT_UNKNOWN;
static std::string g_ccGamePath;
static std::vector<std::string> g_ccCheatFeatureKeys;
static RuntimeThreadCompletion g_ccRuntimeThreadCompletion =
    RUNTIME_THREAD_COMPLETION_INITIALIZER;

static const uint32_t kRuntimeStopTimeoutMs = 5000;

static void* runCcGame(void*)
{
    struct RuntimeThreadCompletionGuard
    {
        ~RuntimeThreadCompletionGuard()
        {
            runtimeThreadCompletionSignal(&g_ccRuntimeThreadCompletion);
        }
    } completionGuard;
    CcRuntimeStats stats = {};
    bool ok = ccRuntimeRunFile(g_ccGamePath.c_str(), g_ccCheatFeatureKeys, &stats);
    if (!ok)
    {
        printf("cc-runtime: execution failed: %s\n",
            stats.error[0] ? stats.error : "unknown error");
        frontendRequestQuit();
    }
    else
    {
        printf("cc-runtime: execution stopped instructions=%llu frames=%u tasks=%u\n",
            (unsigned long long)stats.instructions,
            stats.framesSubmitted,
            stats.tasksCreated);
        if (stats.guestCompleted)
        {
            printf("cc-runtime: guest completed; exiting emulator\n");
            frontendRequestQuit();
        }
    }
    return NULL;
}

bool gameRuntimeStart(const char* gamePath, const EmulatorOptions& options,
    bool requireOptimizedBackend, bool resourceMonitorAutoOpen,
    const std::vector<std::string>& enabledCheatFeatureKeys)
{
    const std::string normalizedPath = gamePathNormalize(gamePath);
    const GameFormat format = gameFormatFromPath(normalizedPath);
    pthread_mutex_lock(&g_gameRuntimeMutex);
    const bool runtimeStarted = g_ccRuntimeThreadStarted;
    pthread_mutex_unlock(&g_gameRuntimeMutex);
    if (gameRuntimeActiveFormat() != GAME_FORMAT_UNKNOWN || runtimeStarted)
    {
        if (!gameRuntimeStop())
        {
            return false;
        }
    }
    if (format == GAME_FORMAT_APP)
    {
        bool started = appRuntimeStart(normalizedPath.c_str(), options,
            requireOptimizedBackend, resourceMonitorAutoOpen,
            enabledCheatFeatureKeys);
        if (started)
        {
            pthread_mutex_lock(&g_gameRuntimeMutex);
            g_activeGameFormat = GAME_FORMAT_APP;
            pthread_mutex_unlock(&g_gameRuntimeMutex);
        }
        return started;
    }
    if (format != GAME_FORMAT_CC)
    {
        printf("game-runtime: unsupported game path: %s\n",
            normalizedPath.empty() ? "(empty)" : normalizedPath.c_str());
        return false;
    }

    ccRuntimePrepareRun();
    runtimeResourceMonitorReset(normalizedPath.c_str(), NULL);
    runtimeResourceMonitorSetActive(resourceMonitorAutoOpen);
    g_ccGamePath = normalizedPath;
    g_ccCheatFeatureKeys = enabledCheatFeatureKeys;
    runtimeThreadCompletionReset(&g_ccRuntimeThreadCompletion);
    pthread_t thread;
    const int createResult = pthread_create(&thread, NULL, runCcGame, NULL);
    if (createResult != 0)
    {
        printf("game-runtime: failed to create CC runtime thread: %d\n", createResult);
        return false;
    }
    pthread_mutex_lock(&g_gameRuntimeMutex);
    g_ccRuntimeThread = thread;
    g_ccRuntimeThreadStarted = true;
    g_activeGameFormat = GAME_FORMAT_CC;
    pthread_mutex_unlock(&g_gameRuntimeMutex);
    printf("game-runtime: starting CC game: %s\n", normalizedPath.c_str());
    return true;
}

bool gameRuntimeStop(void)
{
    pthread_t ccThread = {};
    bool joinCcThread = false;
    pthread_mutex_lock(&g_gameRuntimeMutex);
    const GameFormat format = g_activeGameFormat;
    if (g_ccRuntimeThreadStarted)
    {
        ccThread = g_ccRuntimeThread;
        joinCcThread = true;
    }
    pthread_mutex_unlock(&g_gameRuntimeMutex);
    if (format == GAME_FORMAT_APP)
    {
        const bool stopped = appRuntimeStop();
        if (stopped)
        {
            pthread_mutex_lock(&g_gameRuntimeMutex);
            g_activeGameFormat = GAME_FORMAT_UNKNOWN;
            pthread_mutex_unlock(&g_gameRuntimeMutex);
            audioOutputResetAfterRuntimeStop();
        }
        return stopped;
    }
    if (!joinCcThread)
    {
        return true;
    }
    ccRuntimeRequestStop();
    int joinError = 0;
    const RuntimeThreadJoinResult joinResult = runtimeThreadJoinWithTimeout(
        ccThread, &g_ccRuntimeThreadCompletion, kRuntimeStopTimeoutMs, &joinError);
    if (joinResult != RUNTIME_THREAD_JOINED)
    {
        if (joinResult == RUNTIME_THREAD_JOIN_TIMEOUT)
        {
            printf("game-runtime: CC runtime thread did not stop within %u ms\n",
                kRuntimeStopTimeoutMs);
        }
        else
        {
            printf("game-runtime: CC runtime thread join failed: %d\n", joinError);
        }
        return false;
    }
    pthread_mutex_lock(&g_gameRuntimeMutex);
    g_ccRuntimeThreadStarted = false;
    g_activeGameFormat = GAME_FORMAT_UNKNOWN;
    pthread_mutex_unlock(&g_gameRuntimeMutex);
    audioOutputResetAfterRuntimeStop();
    return true;
}

void gameRuntimeApplySettings(void)
{
    GameFormat format = gameRuntimeActiveFormat();
    if (format == GAME_FORMAT_APP)
    {
        appRuntimeApplySettings();
    }
    else if (format == GAME_FORMAT_CC)
    {
        ccRuntimeApplySettings();
    }
}

void gameRuntimeCopyDiagnostics(char* identity, size_t identitySize,
    char* lastTask, size_t lastTaskSize, char* lastHle, size_t lastHleSize)
{
    if (identity && identitySize)
    {
        snprintf(identity, identitySize, "%s",
            gameRuntimeActiveFormat() == GAME_FORMAT_APP ?
                bridge_get_game_identity() : "");
    }
    if (gameRuntimeActiveFormat() == GAME_FORMAT_APP)
    {
        bridge_copy_last_task_stop_summary(lastTask, lastTaskSize);
        bridge_copy_last_hle_summary(lastHle, lastHleSize);
        return;
    }
    if (lastTask && lastTaskSize)
    {
        lastTask[0] = 0;
    }
    if (lastHle && lastHleSize)
    {
        lastHle[0] = 0;
    }
}

GameFormat gameRuntimeActiveFormat(void)
{
    pthread_mutex_lock(&g_gameRuntimeMutex);
    const GameFormat format = g_activeGameFormat;
    pthread_mutex_unlock(&g_gameRuntimeMutex);
    return format;
}

uint32_t gameRuntimeActiveUnitCount(void)
{
    GameFormat format = gameRuntimeActiveFormat();
    if (format == GAME_FORMAT_CC)
    {
        // CC schedules all guest tasks on one host runtime thread, so the
        // pause gate observes one waiter regardless of guest task count.
        return ccRuntimeIsRunning() ? 1u : 0u;
    }
    return appRuntimeActiveThreadCount();
}

void gameRuntimeNotifyPauseRequested(void)
{
    if (gameRuntimeActiveFormat() == GAME_FORMAT_APP)
    {
        appRuntimeNotifyPauseRequested();
    }
}

bool gameRuntimeReadMemory(uint32_t address, void* out, size_t size)
{
    return gameRuntimeActiveFormat() == GAME_FORMAT_CC ?
        ccRuntimeReadMemory(address, out, size) :
        appRuntimeReadMemory(address, out, size);
}

bool gameRuntimeWriteMemory(uint32_t address, const void* in, size_t size)
{
    return gameRuntimeActiveFormat() == GAME_FORMAT_CC ?
        ccRuntimeWriteMemory(address, in, size) :
        appRuntimeWriteMemory(address, in, size);
}

bool gameRuntimeGetRegisterSnapshot(
    AppRuntimeRegisterSnapshot* out, bool* arm32)
{
    if (arm32)
    {
        *arm32 = false;
    }
    if (gameRuntimeActiveFormat() != GAME_FORMAT_CC)
    {
        return appRuntimeGetRegisterSnapshot(out);
    }
    if (!out)
    {
        return false;
    }
    CcRuntimeRegisterSnapshot cc = {};
    if (!ccRuntimeGetRegisterSnapshot(&cc))
    {
        return false;
    }
    *out = AppRuntimeRegisterSnapshot();
    out->running = cc.running;
    memcpy(out->gpr, cc.r, sizeof(cc.r));
    out->pc = cc.r[15];
    out->fcr31 = cc.cpsr;
    if (arm32)
    {
        *arm32 = true;
    }
    return true;
}

bool gameRuntimeDisassemble(uint32_t address, uint32_t instructionCount,
    std::vector<AppRuntimeDisassemblyLine>* out)
{
    return gameRuntimeActiveFormat() != GAME_FORMAT_CC &&
        appRuntimeDisassemble(address, instructionCount, out);
}

bool gameRuntimeGetGameInfo(AppRuntimeInfo* out)
{
    if (gameRuntimeActiveFormat() != GAME_FORMAT_CC)
    {
        return appRuntimeGetInfo(out);
    }
    if (!out)
    {
        return false;
    }
    CcRuntimeGameInfo cc;
    if (!ccRuntimeGetGameInfo(&cc))
    {
        return false;
    }
    *out = AppRuntimeInfo();
    out->running = cc.running;
    out->path = cc.path;
    out->fileName = gameFileNameFromPath(cc.path);
    out->sha256 = cc.sha256;
    out->backend = "arm32_interpreter";
    out->fileSize = cc.fileSize;
    out->origin = cc.origin;
    out->progSize = cc.programSize;
    out->bootEntry = cc.origin;
    out->appMainEntry = cc.origin;
    out->importCount = cc.importCount;
    out->exportCount = cc.exportCount;
    out->resourceCount = cc.resourceCount;
    return true;
}

struct CcMemorySearchContext
{
    uint32_t begin;
    uint32_t end;
    int width;
    uint32_t target;
    size_t maxCandidates;
    std::vector<AppRuntimeMemorySearchCandidate>* out;
    bool* capped;
};

static uint32_t readLeValue(const uint8_t* bytes, int width)
{
    uint32_t value = 0;
    for (int i = 0; i < width; ++i)
    {
        value |= (uint32_t)bytes[i] << (i * 8);
    }
    return value;
}

static bool searchCcMemoryRegion(uint32_t start, uint32_t size, void* userData)
{
    CcMemorySearchContext* context = (CcMemorySearchContext*)userData;
    uint64_t regionEnd = (uint64_t)start + size;
    uint64_t scanStart = start > context->begin ? start : context->begin;
    uint64_t scanEnd = regionEnd < (uint64_t)context->end + 1u ?
        regionEnd : (uint64_t)context->end + 1u;
    if (scanEnd <= scanStart || scanEnd - scanStart < (uint64_t)context->width)
    {
        return true;
    }
    scanStart = (scanStart + (uint32_t)context->width - 1u) &
        ~((uint64_t)context->width - 1u);
    uint8_t bytes[4] = {};
    for (uint64_t address = scanStart;
         address + (uint32_t)context->width <= scanEnd;
         address += (uint32_t)context->width)
    {
        if (ccRuntimeReadMemory((uint32_t)address, bytes, context->width) &&
            readLeValue(bytes, context->width) == context->target)
        {
            if (context->out->size() >= context->maxCandidates)
            {
                *context->capped = true;
                return false;
            }
            AppRuntimeMemorySearchCandidate candidate = {};
            candidate.address = (uint32_t)address;
            candidate.previous = context->target;
            context->out->push_back(candidate);
        }
    }
    return true;
}

bool gameRuntimeSearchMemoryValue(uint32_t begin, uint32_t end, int width,
    uint32_t target, size_t maxCandidates,
    std::vector<AppRuntimeMemorySearchCandidate>* out, bool* capped)
{
    if (gameRuntimeActiveFormat() != GAME_FORMAT_CC)
    {
        return appRuntimeSearchMemoryValue(begin, end, width, target,
            maxCandidates, out, capped);
    }
    if (!out || !capped || begin > end ||
        (width != 1 && width != 2 && width != 4) || maxCandidates == 0)
    {
        return false;
    }
    out->clear();
    *capped = false;
    CcMemorySearchContext context = {
        begin, end, width, target, maxCandidates, out, capped
    };
    bool completed = ccRuntimeForEachReadableRegion(searchCcMemoryRegion, &context);
    return completed || *capped;
}

bool gameRuntimeFilterMemorySearchCandidates(int width, uint32_t target,
    AppRuntimeMemorySearchFilter filter,
    std::vector<AppRuntimeMemorySearchCandidate>* candidates)
{
    if (gameRuntimeActiveFormat() != GAME_FORMAT_CC)
    {
        return appRuntimeFilterMemorySearchCandidates(
            width, target, filter, candidates);
    }
    if (!candidates || (width != 1 && width != 2 && width != 4))
    {
        return false;
    }
    std::vector<AppRuntimeMemorySearchCandidate> filtered;
    filtered.reserve(candidates->size());
    uint8_t bytes[4] = {};
    for (size_t i = 0; i < candidates->size(); ++i)
    {
        AppRuntimeMemorySearchCandidate candidate = (*candidates)[i];
        if (!ccRuntimeReadMemory(candidate.address, bytes, width))
        {
            continue;
        }
        uint32_t value = readLeValue(bytes, width);
        bool keep = filter == APP_RUNTIME_MEMORY_SEARCH_EQUAL ? value == target :
            filter == APP_RUNTIME_MEMORY_SEARCH_INCREASED ? value > candidate.previous :
            filter == APP_RUNTIME_MEMORY_SEARCH_DECREASED ? value < candidate.previous :
            value == candidate.previous;
        if (keep)
        {
            candidate.previous = value;
            filtered.push_back(candidate);
        }
    }
    candidates->swap(filtered);
    return true;
}

bool gameRuntimeSupportsBreakpoints(void)
{
    return gameRuntimeActiveFormat() == GAME_FORMAT_APP;
}

bool gameRuntimeEnableResourceMonitor(void)
{
    if (gameRuntimeActiveFormat() == GAME_FORMAT_CC)
    {
        runtimeResourceMonitorSetActive(true);
        return ccRuntimeIsRunning();
    }
    return appRuntimeEnableResourceMonitor();
}

bool gameRuntimeWriteState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback,
    void* progressUserData)
{
    const SaveStateGameFormat format = saveStateFormatForPath(gamePath);
    if (format == SAVE_STATE_FORMAT_CC)
    {
        CcRuntimeState state;
        return ccRuntimeCaptureState(&state, error) &&
            saveStateWriteCcSlot(gamePath, slot, state, error,
                progressCallback, progressUserData);
    }
    AppRuntimeState state;
    return appRuntimeCaptureState(&state, error) &&
        saveStateWriteSlot(gamePath, format, slot, state, error,
            progressCallback, progressUserData);
}

bool gameRuntimeReadState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback,
    void* progressUserData)
{
    const SaveStateGameFormat format = saveStateFormatForPath(gamePath);
    if (format == SAVE_STATE_FORMAT_CC)
    {
        CcRuntimeState state;
        return saveStateReadCcSlot(gamePath, slot, &state, error,
            progressCallback, progressUserData) &&
            ccRuntimeRestoreState(state, error);
    }
    AppRuntimeState state;
    return saveStateReadSlot(gamePath, format, slot, &state, error,
        progressCallback, progressUserData) &&
        appRuntimeRestoreState(state, error);
}

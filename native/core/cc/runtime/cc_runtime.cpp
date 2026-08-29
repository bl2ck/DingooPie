#include "cc/runtime/cc_runtime.h"
#include "cc/save/cc_save_state.h"
#include "cc/memory/cc_memory_layout.h"

#include "shared/services/guest_package.h"
#include "shared/game/game_paths.h"
#include "cc/cpu/arm32_interpreter.h"
#if defined(DINGOO_PIE_ARM32_DYNARMIC)
#include "cc/cpu/arm32_dynarmic.h"
#endif
#include "cc/runtime/cc_timing.h"
#include "cc/hle/cc_input_mapping.h"
#include "config/cheats/cheat_runtime.h"
#include "shared/config/runtime_constants.h"
#include "shared/execution/execution_backend.h"
#include "cc/runtime/cc_crash_report.h"
#include "frontend/video/framebuffer.h"
#include "shared/services/guest_audio.h"
#include "shared/services/guest_filesystem.h"
#include "frontend/input/input_controls.h"
#include "frontend/input/input_state.h"
#include "shared/execution/pause_gate.h"
#include "shared/platform/storage_services.h"
#include "shared/diagnostics/runtime_log.h"
#include "runtime_resource_monitor.h"
#include "Common/Crypto/sha256.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

static const uint32_t kCcRetailRamStart = 0x10000000u;
static const uint32_t kCcRetailRamSize = 0x04000000u;
static const uint32_t kCcRetailHeapStart = 0x21000000u;
static const uint32_t kCcRetailHeapSize = 0x02000000u;
static const uint32_t kCcHomebrewHeapStart = 0x09000000u;
static const uint32_t kCcHomebrewHeapSize = 0x02000000u;
static const uint32_t kCcHomebrewSystemRamStart = 0x10000000u;
static const uint32_t kCcHomebrewSystemRamSize = 0x03800000u;
// The CC1800 SDK linker script defines one 16 MiB application window.
static const uint32_t kCcHomebrewRamStart = kCcHomebrewProgramOrigin;
static const uint32_t kCcHomebrewRamSize = 0x01000000u;
static const uint32_t kStackStart = 0x1ff00000u;
static const uint32_t kStackSize = 0x00100000u;
static const uint32_t kLegacyLowMemorySize = 0x00010000u;
static const uint32_t kLoaderHandle = kStackStart + 0x100u;
static const uint32_t kAppPathWideString = kStackStart + 0x200u;
static const uint32_t kLocaleString = kStackStart + 0x600u;
static const uint32_t kDynamicThunkStart = kStackStart + 0x1000u;
static const uint32_t kExitAddress = kStackStart + kStackSize - 4u;
static const uint32_t kFramebufferAddress = 0x80000000u;
static const uint32_t kFramebufferWidth = 320u;
static const uint32_t kFramebufferHeight = 240u;
static const uint32_t kFramebuffer16Size =
    kFramebufferWidth * kFramebufferHeight * sizeof(uint16_t);
static const uint32_t kFramebuffer32Size =
    kFramebufferWidth * kFramebufferHeight * sizeof(uint32_t);
static const uint32_t kCcFramebufferStride = 0x0004c000u;
static const uint32_t kCcFramebufferCount = 4u;
static const uint32_t kCcFramebufferSize =
    kCcFramebufferStride * kCcFramebufferCount;
static const uint32_t kCcVideoMemorySize = 0x00800000u;
static const uint32_t kLegacyMmioStart = 0x04000000u;
static const uint32_t kLegacyMmioSize = 0x00100000u;
static const uint32_t kLegacyAudioMmioStart = 0x08a00000u;
static const uint32_t kLegacyAudioMmioSize = 0x00010000u;
static const uint32_t kDvcAudioHandle = 1u;
static const uint32_t kDvcAudioDefaultSampleRate = 44100u;
static const uint32_t kDvcAudioMaxVolume = 30u;
static const uint32_t kDvcAudioSetSampleRate = 0x0du;
static const uint32_t kDvcAudioStartPlayback = 0x0bu;
static const char kDvcAudioDeviceName[] = "ROOT\\DVC\\IIS\\IIS0";
static const uint32_t kLegacySystemMmioStart = 0x09300000u;
static const uint32_t kLegacySystemMmioSize = 0x00010000u;
static const uint32_t kLegacyFramebufferAddress = 0x11800000u;
static const uint32_t kLegacyGraphicsSurface = 0x0930201cu;
static const uint32_t kLegacyGraphicsStride = 0x09302020u;
static const uint32_t kLegacyGraphicsStatus = 0x09303054u;
static const uint32_t kLegacyGraphicsReady = 1u << 2;
static const uint64_t kSliceInstructions = 50000u;
static const uint64_t kNonAudioSliceInstructions = 5000u;
static const double kAutoRuntimeSpeedScale = 0.65;
static const uint64_t kReferenceCpuClockHz = 336000000u;
static const uint64_t kReferenceInterpreterIps = 15000000u;
static const uint64_t kInstructionPacingMaxLagMicros = 100000u;

static std::atomic<bool> s_stopRequested(false);
static std::atomic<bool> s_running(false);
static std::atomic<double> s_runtimeSpeedScale(kAutoRuntimeSpeedScale);
static std::atomic<double> s_hostDelayScale(1.0);
static std::atomic<uint64_t> s_targetInstructionsPerSecond(0);
static std::atomic<bool> s_useOptimizedBackend(false);
static std::mutex s_runtimeMutex;
struct CcRuntimeContext;
static CcRuntimeContext* s_activeRuntime = NULL;
static std::string sha256Hex(const uint8_t* data, uint32_t size);

static bool environmentFlagEnabled(const char* name)
{
    const char* value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static bool runtimeCollectsInterpreterProfileSamples()
{
    return runtimeLogProfileEnabled() &&
        !environmentFlagEnabled("DINGOO_PIE_CC_DYNARMIC_PROFILE");
}

struct CcRuntimeContext
{
    struct HeapBlock
    {
        uint32_t address;
        uint32_t size;
        bool free;
    };

    struct Task
    {
        Arm32State state;
        uint32_t entry;
        uint32_t argument;
        uint32_t stack;
        uint32_t priority;
        uint32_t delayTicks;
        uint64_t hostDelayUntilMicros;
        bool started;
        bool finished;
        bool audioProducer;
    };

    struct ResourceHandle
    {
        uint32_t address;
        GuestResourceEntry* entry;
        uint32_t position;
        uint32_t dataAddress;
    };

    struct FileHandle
    {
        uint32_t address;
        uint32_t stream;
    };

    struct Semaphore
    {
        uint32_t address;
        uint32_t count;
    };

    GuestPackage* package;
    std::vector<uint8_t> ram;
    std::vector<uint8_t> systemMemory;
    std::vector<uint8_t> stack;
    std::vector<uint8_t> heapMemory;
    std::vector<uint8_t> legacyLowMemory;
    std::vector<uint8_t> framebuffer;
    std::vector<uint8_t> legacyMmio;
    std::vector<uint8_t> legacyAudioMmio;
    std::vector<uint8_t> legacySystemMmio;
    std::vector<Arm32InstructionCacheEntry> instructionCache;
    std::vector<uint32_t> profilePcSamples;
    std::vector<uint32_t> profileLrSamples;
    std::vector<HeapBlock> heap;
    std::vector<Task> tasks;
    std::vector<ResourceHandle> resources;
    std::vector<FileHandle> files;
    std::vector<Semaphore> semaphores;
    std::vector<std::string> dynamicImports;
    std::vector<std::string> unknownImportNames;
    std::vector<uint32_t> openStreams;
    uint32_t ramStart;
    uint32_t heapStart;
    uint32_t heapCursor;
    uint32_t currentTaskIndex;
    uint32_t currentDelayTicks;
    uint64_t currentHostDelayMicros;
    uint32_t dvcAudioHandle;
    uint32_t dvcAudioSampleRate;
    uint32_t dvcAudioVolume;
    uint32_t framebufferAddress;
    uint32_t framebufferBits;
    uint32_t framebufferWriteHighWater[kCcFramebufferCount];
    bool framebufferBitsExplicit;
    uint32_t faultAddress;
    uint32_t faultSize;
    bool faultWrite;
    bool faultFetch;
    bool cheatCodeCacheFlushPending;
    bool yielded;
    bool dvcAudioStarted;
    Arm32Bus bus;
    CcRuntimeStats* stats;
    std::string gamePath;
    std::string gameSha256;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point instructionPacingStartTime;
    uint64_t instructionPacingStartInstructions;
    uint64_t profileLastMillis;
    uint64_t profileLastInstructions;
};

static uint64_t currentGuestMicros(const CcRuntimeContext* runtime);

static void presentFramebuffer(CcRuntimeContext* runtime, uint32_t requestedAddress);

static void setCcStateError(std::string* error, const char* text)
{
    if (error) *error = text;
}

static GuestResourceEntry* findCcResourceByName(CcRuntimeContext* runtime,
    const std::string& name)
{
    return runtime && runtime->package ? guestPackageFindResource(
        runtime->package, name.c_str()) : NULL;
}

uint32_t ccRuntimeActiveTaskCount(void)
{
    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    return s_activeRuntime ? (uint32_t)s_activeRuntime->tasks.size() : 0;
}

bool ccRuntimeCaptureState(CcRuntimeState* out, std::string* error)
{
    if (!out)
    {
        setCcStateError(error, "runtime state output is invalid");
        return false;
    }
    if (!pauseGateWaitForPausedWaiters(2000, 1))
    {
        setCcStateError(error, "runtime did not pause in time");
        return false;
    }
    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    CcRuntimeContext* runtime = s_activeRuntime;
    if (!runtime || !runtime->package)
    {
        setCcStateError(error, "runtime state is not available");
        return false;
    }
    for (size_t i = 0; i < runtime->openStreams.size(); ++i)
    {
        if (fsys_stream_is_external_file(runtime->openStreams[i]))
        {
            setCcStateError(error, "CC state has unsupported external file stream");
            return false;
        }
    }
    *out = CcRuntimeState();
    out->gameSha256 = sha256Hex(runtime->package->file_data, runtime->package->file_size);
    out->ram = runtime->ram;
    out->systemMemory = runtime->systemMemory;
    out->stack = runtime->stack;
    out->heapMemory = runtime->heapMemory;
    out->legacyLowMemory = runtime->legacyLowMemory;
    out->framebuffer = runtime->framebuffer;
    out->legacyMmio = runtime->legacyMmio;
    out->legacyAudioMmio = runtime->legacyAudioMmio;
    out->legacySystemMmio = runtime->legacySystemMmio;
    for (size_t i = 0; i < runtime->heap.size(); ++i)
    {
        out->heap.push_back({ runtime->heap[i].address,
            runtime->heap[i].size, runtime->heap[i].free });
    }
    for (size_t i = 0; i < runtime->tasks.size(); ++i)
    {
        const CcRuntimeContext::Task& task = runtime->tasks[i];
        CcSaveTask savedTask = {};
        savedTask.state = task.state;
        savedTask.entry = task.entry;
        savedTask.argument = task.argument;
        savedTask.stack = task.stack;
        savedTask.priority = task.priority;
        savedTask.delayTicks = task.delayTicks;
        savedTask.started = task.started;
        savedTask.finished = task.finished;
        savedTask.audioProducer = task.audioProducer;
        out->tasks.push_back(savedTask);
    }
    for (size_t i = 0; i < runtime->resources.size(); ++i)
    {
        const CcRuntimeContext::ResourceHandle& resource = runtime->resources[i];
        if (!resource.entry)
        {
            continue;
        }
        out->resources.push_back({ resource.address,
            resource.entry->name ? resource.entry->name : "",
            resource.position, resource.dataAddress });
    }
    for (size_t i = 0; i < runtime->openStreams.size(); ++i)
    {
        uint32_t stream = runtime->openStreams[i];
        out->streams.push_back({ stream, fsys_stream_request_name(stream),
            fsys_stream_position(stream) });
    }
    for (size_t i = 0; i < runtime->files.size(); ++i)
    {
        out->files.push_back({ runtime->files[i].address, runtime->files[i].stream });
    }
    for (size_t i = 0; i < runtime->semaphores.size(); ++i)
    {
        out->semaphores.push_back({ runtime->semaphores[i].address,
            runtime->semaphores[i].count });
    }
    out->dynamicImports = runtime->dynamicImports;
    out->unknownImportNames = runtime->unknownImportNames;
    out->elapsedGuestMicros = currentGuestMicros(runtime);
    out->runtimeInstructions = runtime->stats->instructions;
    out->heapStart = runtime->heapStart;
    out->heapCursor = runtime->heapCursor;
    out->dvcAudioHandle = runtime->dvcAudioHandle;
    out->dvcAudioSampleRate = runtime->dvcAudioSampleRate;
    out->dvcAudioVolume = runtime->dvcAudioVolume;
    out->framebufferAddress = runtime->framebufferAddress;
    out->framebufferBits = runtime->framebufferBits;
    memcpy(out->framebufferWriteHighWater, runtime->framebufferWriteHighWater,
        sizeof(out->framebufferWriteHighWater));
    out->framebufferBitsExplicit = runtime->framebufferBitsExplicit;
    out->dvcAudioStarted = runtime->dvcAudioStarted;
    return !out->tasks.empty();
}

bool ccRuntimeRestoreState(const CcRuntimeState& state, std::string* error)
{
    if (!state.runtimeInstructions || state.tasks.empty() || state.tasks.size() > 32)
    {
        setCcStateError(error, "saved runtime state is invalid");
        return false;
    }
    if (!pauseGateWaitForPausedWaiters(2000, 1))
    {
        setCcStateError(error, "runtime did not pause in time");
        return false;
    }

    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    CcRuntimeContext* runtime = s_activeRuntime;
    if (!runtime || !runtime->package ||
        runtime->ram.size() != state.ram.size() ||
        runtime->systemMemory.size() != state.systemMemory.size() ||
        runtime->stack.size() != state.stack.size() ||
        runtime->heapMemory.size() != state.heapMemory.size() ||
        state.heapStart != runtime->heapStart ||
        state.heapCursor < state.heapStart ||
        (uint64_t)state.heapCursor >
            (uint64_t)state.heapStart + state.heapMemory.size() ||
        runtime->framebuffer.size() != state.framebuffer.size())
    {
        setCcStateError(error, "runtime memory layout does not match save state");
        return false;
    }
    if (sha256Hex(runtime->package->file_data, runtime->package->file_size) !=
        state.gameSha256)
    {
        setCcStateError(error, "save-state belongs to a different game");
        return false;
    }

    std::vector<GuestResourceEntry*> restoredResourceEntries;
    restoredResourceEntries.reserve(state.resources.size());
    for (size_t i = 0; i < state.resources.size(); ++i)
    {
        GuestResourceEntry* entry = findCcResourceByName(runtime, state.resources[i].name);
        if (!entry)
        {
            setCcStateError(error, "saved resource is not available");
            return false;
        }
        restoredResourceEntries.push_back(entry);
    }

    std::vector<uint32_t> restoredStreams;
    restoredStreams.reserve(state.streams.size());
    auto closeRestoredStreams = [&restoredStreams]()
    {
        for (size_t i = 0; i < restoredStreams.size(); ++i)
        {
            fsys_fclose(restoredStreams[i]);
        }
    };
    for (size_t i = 0; i < state.streams.size(); ++i)
    {
        uint32_t stream = fsys_fopen(state.streams[i].requestName.c_str(), "rb");
        if (!stream || fsys_fseek(stream, state.streams[i].position, SEEK_SET) != 0)
        {
            if (stream) fsys_fclose(stream);
            closeRestoredStreams();
            setCcStateError(error, "saved file stream is not available");
            return false;
        }
        restoredStreams.push_back(stream);
    }

    auto findRestoredStream = [&state, &restoredStreams](uint32_t savedStream)
    {
        for (size_t i = 0; i < state.streams.size(); ++i)
        {
            if (state.streams[i].stream == savedStream)
            {
                return restoredStreams[i];
            }
        }
        return 0u;
    };

    std::vector<CcRuntimeContext::Task> restoredTasks;
    restoredTasks.reserve(state.tasks.size());
    for (size_t i = 0; i < state.tasks.size(); ++i)
    {
        const CcSaveTask& savedTask = state.tasks[i];
        CcRuntimeContext::Task task = {};
        task.state = savedTask.state;
        task.entry = savedTask.entry;
        task.argument = savedTask.argument;
        task.stack = savedTask.stack;
        task.priority = savedTask.priority;
        task.delayTicks = savedTask.delayTicks;
        task.started = savedTask.started;
        task.finished = savedTask.finished;
        task.audioProducer = savedTask.audioProducer;
        restoredTasks.push_back(task);
    }

    std::vector<CcRuntimeContext::ResourceHandle> restoredResources;
    restoredResources.reserve(state.resources.size());
    for (size_t i = 0; i < state.resources.size(); ++i)
    {
        restoredResources.push_back({ state.resources[i].address,
            restoredResourceEntries[i], state.resources[i].position,
            state.resources[i].dataAddress });
    }

    std::vector<CcRuntimeContext::FileHandle> restoredFiles;
    restoredFiles.reserve(state.files.size());
    for (size_t i = 0; i < state.files.size(); ++i)
    {
        uint32_t stream = findRestoredStream(state.files[i].stream);
        if (!stream)
        {
            closeRestoredStreams();
            setCcStateError(error, "saved file handle stream is not available");
            return false;
        }
        restoredFiles.push_back({ state.files[i].address, stream });
    }

    for (size_t i = 0; i < runtime->openStreams.size(); ++i)
    {
        fsys_fclose(runtime->openStreams[i]);
    }
    runtime->ram = state.ram;
    runtime->systemMemory = state.systemMemory;
    runtime->stack = state.stack;
    runtime->heapMemory = state.heapMemory;
    runtime->legacyLowMemory = state.legacyLowMemory;
    runtime->framebuffer = state.framebuffer;
    runtime->legacyMmio = state.legacyMmio;
    runtime->legacyAudioMmio = state.legacyAudioMmio;
    runtime->legacySystemMmio = state.legacySystemMmio;
    runtime->heap.clear();
    for (size_t i = 0; i < state.heap.size(); ++i)
    {
        runtime->heap.push_back({ state.heap[i].address,
            state.heap[i].size, state.heap[i].free });
    }
    runtime->tasks.swap(restoredTasks);
    runtime->resources.swap(restoredResources);
    runtime->files.swap(restoredFiles);
    runtime->openStreams.swap(restoredStreams);
    runtime->semaphores.clear();
    for (size_t i = 0; i < state.semaphores.size(); ++i)
    {
        runtime->semaphores.push_back({ state.semaphores[i].address,
            state.semaphores[i].count });
    }
    runtime->dynamicImports = state.dynamicImports;
    runtime->unknownImportNames = state.unknownImportNames;
    runtime->currentTaskIndex = UINT32_MAX;

    runtime->heapStart = state.heapStart;
    runtime->heapCursor = state.heapCursor;
    runtime->dvcAudioHandle = state.dvcAudioHandle;
    runtime->dvcAudioSampleRate = state.dvcAudioSampleRate;
    runtime->dvcAudioVolume = state.dvcAudioVolume;
    runtime->framebufferAddress = state.framebufferAddress;
    runtime->framebufferBits = state.framebufferBits;
    memcpy(runtime->framebufferWriteHighWater, state.framebufferWriteHighWater,
        sizeof(runtime->framebufferWriteHighWater));
    runtime->framebufferBitsExplicit = state.framebufferBitsExplicit;
    runtime->dvcAudioStarted = state.dvcAudioStarted;
    runtime->stats->instructions = state.runtimeInstructions;
    runtime->startTime = std::chrono::steady_clock::now() -
        std::chrono::microseconds((uint64_t)(state.elapsedGuestMicros /
            (s_runtimeSpeedScale.load() > 0 ? s_runtimeSpeedScale.load() : 1.0)));
    runtime->instructionPacingStartTime = std::chrono::steady_clock::now();
    runtime->instructionPacingStartInstructions = runtime->stats->instructions;
    runtime->instructionCache.assign(
        runtime->instructionCache.size(), Arm32InstructionCacheEntry());
    runtime->bus.userData = runtime;
    runtime->bus.directSystemRam = runtime->systemMemory.data();
    runtime->bus.directSystemRamBase = kCcHomebrewSystemRamStart;
    runtime->bus.directSystemRamSize =
        (uint32_t)runtime->systemMemory.size();
    runtime->bus.directRam = runtime->ram.data();
    runtime->bus.directRamBase = runtime->ramStart;
    runtime->bus.directRamSize = (uint32_t)runtime->ram.size();
    runtime->bus.directStack = runtime->stack.data();
    runtime->bus.directStackBase = kStackStart;
    runtime->bus.directStackSize = (uint32_t)runtime->stack.size();
    runtime->bus.directHeap = runtime->heapMemory.data();
    runtime->bus.directHeapBase = runtime->heapStart;
    runtime->bus.directHeapSize = runtime->legacySystemMmio.empty() ?
        (uint32_t)runtime->heapMemory.size() :
        kLegacySystemMmioStart - runtime->heapStart;
    runtime->bus.directFramebuffer = runtime->framebufferBitsExplicit ?
        runtime->framebuffer.data() : NULL;
    runtime->bus.directFramebufferBase = kFramebufferAddress;
    runtime->bus.directFramebufferSize = (uint32_t)runtime->framebuffer.size();
    runtime->bus.directProgram = runtime->ram.data() +
        (runtime->package->origin - runtime->ramStart);
    runtime->bus.instructionCache = runtime->instructionCache.data();
    runtime->bus.instructionCacheCount =
        (uint32_t)runtime->instructionCache.size();
#if defined(DINGOO_PIE_ARM32_DYNARMIC)
    arm32DynarmicReset();
#endif
    printf("cc-arm: save-state restored tasks=%u streams=%u resources=%u\n",
        (uint32_t)runtime->tasks.size(), (uint32_t)runtime->openStreams.size(),
        (uint32_t)runtime->resources.size());
    return true;
}

static uint8_t* resolveMemorySpan(CcRuntimeContext* runtime, uint32_t address,
    uint32_t* available)
{
    if (address < runtime->legacyLowMemory.size())
    {
        if (available) *available =
            (uint32_t)runtime->legacyLowMemory.size() - address;
        return runtime->legacyLowMemory.data() + address;
    }
    uint32_t offset = address - kCcHomebrewSystemRamStart;
    if (address >= kCcHomebrewSystemRamStart &&
        offset < runtime->systemMemory.size())
    {
        if (available) *available =
            (uint32_t)runtime->systemMemory.size() - offset;
        return runtime->systemMemory.data() + offset;
    }
    offset = address - runtime->ramStart;
    if (address >= runtime->ramStart && offset < runtime->ram.size())
    {
        if (available) *available = (uint32_t)runtime->ram.size() - offset;
        return runtime->ram.data() + offset;
    }
    offset = address - kStackStart;
    if (address >= kStackStart && offset < kStackSize)
    {
        if (available) *available = kStackSize - offset;
        return runtime->stack.data() + offset;
    }
    offset = address - kLegacySystemMmioStart;
    if (address >= kLegacySystemMmioStart &&
        offset < runtime->legacySystemMmio.size())
    {
        if (available) *available =
            (uint32_t)runtime->legacySystemMmio.size() - offset;
        return runtime->legacySystemMmio.data() + offset;
    }
    offset = address - kLegacyAudioMmioStart;
    if (address >= kLegacyAudioMmioStart &&
        offset < runtime->legacyAudioMmio.size())
    {
        if (available) *available =
            (uint32_t)runtime->legacyAudioMmio.size() - offset;
        return runtime->legacyAudioMmio.data() + offset;
    }
    offset = address - runtime->heapStart;
    if (address >= runtime->heapStart &&
        offset < runtime->heapMemory.size())
    {
        if (available) *available =
            (uint32_t)runtime->heapMemory.size() - offset;
        return runtime->heapMemory.data() + offset;
    }
    offset = address - kFramebufferAddress;
    if (address >= kFramebufferAddress && offset < runtime->framebuffer.size())
    {
        if (available) *available = (uint32_t)runtime->framebuffer.size() - offset;
        return runtime->framebuffer.data() + offset;
    }
    offset = address - kLegacyMmioStart;
    if (address >= kLegacyMmioStart && offset < runtime->legacyMmio.size())
    {
        if (available) *available = (uint32_t)runtime->legacyMmio.size() - offset;
        return runtime->legacyMmio.data() + offset;
    }
    if (available) *available = 0;
    return NULL;
}

static uint8_t* resolveMemory(CcRuntimeContext* runtime, uint32_t address, size_t size)
{
    uint32_t available = 0;
    uint8_t* pointer = resolveMemorySpan(runtime, address, &available);
    return pointer && size <= available ? pointer : NULL;
}

static bool memoryRangesOverlap(uint32_t address, size_t size,
    uint32_t rangeStart, uint32_t rangeSize)
{
    if (!size || !rangeSize)
    {
        return false;
    }
    uint64_t end = (uint64_t)address + size;
    uint64_t rangeEnd = (uint64_t)rangeStart + rangeSize;
    return address < rangeEnd && end > rangeStart;
}

static void noteFramebufferWrite(CcRuntimeContext* runtime, uint32_t address, size_t size)
{
    if (address < kFramebufferAddress ||
        address >= kFramebufferAddress + kCcFramebufferSize)
    {
        return;
    }
    uint32_t offset = address - kFramebufferAddress;
    size_t remaining = std::min<size_t>(size, kCcFramebufferSize - offset);
    while (remaining)
    {
        uint32_t bufferIndex = offset / kCcFramebufferStride;
        uint32_t bufferOffset = offset % kCcFramebufferStride;
        uint32_t chunkSize = (uint32_t)std::min<size_t>(
            remaining, kCcFramebufferStride - bufferOffset);
        runtime->framebufferWriteHighWater[bufferIndex] = std::max(
            runtime->framebufferWriteHighWater[bufferIndex], bufferOffset + chunkSize);
        offset += chunkSize;
        remaining -= chunkSize;
    }
    framebufferTrackWrite(address, (uint32_t)std::min<size_t>(size, UINT32_MAX));
}

static bool busRead(void* userData, uint32_t address, void* output, size_t size)
{
    CcRuntimeContext* runtime = (CcRuntimeContext*)userData;
    uint8_t* source = resolveMemory(runtime, address, size);
    if (!source)
    {
        runtime->faultAddress = address;
        runtime->faultSize = (uint32_t)size;
        runtime->faultWrite = false;
        runtime->faultFetch = false;
    }
    if (!source) return false;
    memcpy(output, source, size);
    return true;
}

static bool busFetch(void* userData, uint32_t address, void* output, size_t size)
{
    CcRuntimeContext* runtime = (CcRuntimeContext*)userData;
    uint32_t programOffset = address - runtime->package->origin;
    bool program = address >= runtime->package->origin &&
        programOffset < runtime->package->prog_size &&
        size <= runtime->package->prog_size - programOffset;
    uint32_t thunkOffset = address - kDynamicThunkStart;
    bool thunk = address >= kDynamicThunkStart && thunkOffset < 0x10000u &&
        size <= 0x10000u - thunkOffset;
    if (!program && !thunk)
    {
        runtime->faultAddress = address;
        runtime->faultSize = (uint32_t)size;
        runtime->faultWrite = false;
        runtime->faultFetch = true;
        return false;
    }
    return busRead(userData, address, output, size);
}

static bool busWrite(void* userData, uint32_t address, const void* input, size_t size)
{
    CcRuntimeContext* runtime = (CcRuntimeContext*)userData;
    uint8_t* destination = resolveMemory(runtime, address, size);
    if (!destination)
    {
        runtime->faultAddress = address;
        runtime->faultSize = (uint32_t)size;
        runtime->faultWrite = true;
        runtime->faultFetch = false;
    }
    if (!destination) return false;
    memcpy(destination, input, size);
    uint64_t writeEnd = (uint64_t)address + size;
    uint64_t programStart = runtime->package->origin;
    uint64_t programEnd = programStart + runtime->package->prog_size;
    if (!runtime->instructionCache.empty() &&
        memoryRangesOverlap(address, size, runtime->package->origin,
            runtime->package->prog_size))
    {
        uint32_t firstAddress = std::max<uint32_t>(address,
            runtime->package->origin) & ~3u;
        uint32_t lastAddress = (uint32_t)(std::min<uint64_t>(writeEnd,
            programEnd) - 1u) & ~3u;
        for (uint32_t instructionAddress = firstAddress;
             instructionAddress <= lastAddress; instructionAddress += 4u)
        {
            size_t index = (instructionAddress - runtime->package->origin) / 4u;
            if (index < runtime->instructionCache.size())
            {
                runtime->instructionCache[index] = Arm32InstructionCacheEntry{};
            }
        }
    }
    if (size == sizeof(uint32_t) && address == kLegacyGraphicsStride)
    {
        uint32_t value = 0;
        memcpy(&value, input, sizeof(value));
        if (value == kFramebufferWidth * sizeof(uint16_t) ||
            value == kFramebufferWidth * sizeof(uint32_t))
        {
            runtime->framebufferBits = value * 8u / kFramebufferWidth;
            runtime->framebufferBitsExplicit = true;
            runtime->bus.directFramebuffer = runtime->framebuffer.data();
        }
    }
    else if (size == sizeof(uint32_t) && address == kLegacyGraphicsSurface)
    {
        presentFramebuffer(runtime, kLegacyFramebufferAddress);
    }
    noteFramebufferWrite(runtime, address, size);
    return true;
}

static bool cheatReadCallback(void* userData, uint32_t address, void* output, size_t size)
{
    return busRead(userData, address, output, size);
}

static bool cheatWriteTouchesExecutableMemory(const CcRuntimeContext* runtime,
    uint32_t address, size_t size)
{
    return memoryRangesOverlap(address, size, runtime->package->origin,
            runtime->package->prog_size) ||
        memoryRangesOverlap(address, size, kDynamicThunkStart, 0x10000u);
}

static bool cheatWriteCallback(void* userData, uint32_t address, const void* input, size_t size)
{
    CcRuntimeContext* runtime = (CcRuntimeContext*)userData;
    if (!busWrite(userData, address, input, size))
    {
        return false;
    }
    if (cheatWriteTouchesExecutableMemory(runtime, address, size))
    {
        runtime->cheatCodeCacheFlushPending = true;
    }
    return true;
}

static void cheatFlushCallback(void* userData)
{
    CcRuntimeContext* runtime = (CcRuntimeContext*)userData;
    if (!runtime->cheatCodeCacheFlushPending)
    {
        return;
    }
    std::fill(runtime->instructionCache.begin(), runtime->instructionCache.end(),
        Arm32InstructionCacheEntry{});
}

static void processPendingCheatCodeCacheFlush(CcRuntimeContext* runtime)
{
    if (!runtime->cheatCodeCacheFlushPending)
    {
        return;
    }
#if defined(DINGOO_PIE_ARM32_DYNARMIC)
    arm32DynarmicReset();
#endif
    runtime->cheatCodeCacheFlushPending = false;
}

static std::string sha256Hex(const uint8_t* data, uint32_t size)
{
    static const char kHex[] = "0123456789ABCDEF";
    uint8_t digest[32];
    sha256_context context;
    sha256_starts(&context);
    sha256_update(&context, data, size);
    sha256_finish(&context, digest);
    std::string output(64, '0');
    for (size_t i = 0; i < sizeof(digest); ++i)
    {
        output[i * 2] = kHex[digest[i] >> 4];
        output[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    return output;
}

static void presentFramebuffer(CcRuntimeContext* runtime, uint32_t requestedAddress)
{
    uint32_t frameAddress = runtime->framebufferAddress;
    if (resolveMemory(runtime, requestedAddress, kFramebuffer16Size))
    {
        frameAddress = requestedAddress;
    }
    if (!runtime->framebufferBitsExplicit && frameAddress >= kFramebufferAddress &&
        frameAddress - kFramebufferAddress < kCcFramebufferSize &&
        ccPackageUsesHomebrewLayout(runtime->package->origin) &&
        runtime->framebufferWriteHighWater[
            (frameAddress - kFramebufferAddress) / kCcFramebufferStride] >
            kFramebuffer16Size)
    {
        runtime->framebufferBits = 32u;
    }
    uint32_t frameSize = runtime->framebufferBits == 32u ?
        kFramebuffer32Size : kFramebuffer16Size;
    const uint8_t* frame = resolveMemory(runtime, frameAddress, frameSize);
    if (!frame)
    {
        frameAddress = kFramebufferAddress;
        frame = resolveMemory(runtime, frameAddress, frameSize);
    }
    runtime->framebufferAddress = frameAddress;
    uint16_t* output = (uint16_t*)framebufferPixels();
    if (runtime->framebufferBits == 32u)
    {
        const uint32_t* input = (const uint32_t*)frame;
        for (uint32_t i = 0; i < kFramebufferWidth * kFramebufferHeight; ++i)
        {
            uint32_t pixel = input[i];
            uint32_t red = (pixel >> 16) & 0xffu;
            uint32_t green = (pixel >> 8) & 0xffu;
            uint32_t blue = pixel & 0xffu;
            output[i] = (uint16_t)(((red >> 3) << 11) |
                ((green >> 2) << 5) | (blue >> 3));
        }
    }
    else
    {
        memcpy(output, frame, kFramebuffer16Size);
    }
    framebufferRequestUpdate();
    runtime->stats->framesSubmitted++;
}

static bool readGuestString(CcRuntimeContext* runtime, uint32_t address,
    char* output, size_t outputSize)
{
    if (!address || !output || outputSize < 2) return false;
    for (size_t i = 0; i < outputSize - 1; ++i)
    {
        uint8_t* value = resolveMemory(runtime, address + (uint32_t)i, 1);
        if (!value) return false;
        output[i] = (char)*value;
        if (!output[i]) return true;
    }
    output[outputSize - 1] = '\0';
    return true;
}

static void appendUtf8(std::string* output, uint32_t codePoint)
{
    if (codePoint <= 0x7fu) output->push_back((char)codePoint);
    else if (codePoint <= 0x7ffu)
    {
        output->push_back((char)(0xc0u | (codePoint >> 6)));
        output->push_back((char)(0x80u | (codePoint & 0x3fu)));
    }
    else if (codePoint <= 0xffffu)
    {
        output->push_back((char)(0xe0u | (codePoint >> 12)));
        output->push_back((char)(0x80u | ((codePoint >> 6) & 0x3fu)));
        output->push_back((char)(0x80u | (codePoint & 0x3fu)));
    }
    else
    {
        output->push_back((char)(0xf0u | (codePoint >> 18)));
        output->push_back((char)(0x80u | ((codePoint >> 12) & 0x3fu)));
        output->push_back((char)(0x80u | ((codePoint >> 6) & 0x3fu)));
        output->push_back((char)(0x80u | (codePoint & 0x3fu)));
    }
}

static bool readGuestWideString(CcRuntimeContext* runtime, uint32_t address,
    std::string* output, size_t maxCharacters)
{
    if (!address || !output) return false;
    output->clear();
    for (size_t i = 0; i < maxCharacters; ++i)
    {
        uint16_t value = 0;
        uint8_t* source = resolveMemory(runtime, address + (uint32_t)i * 2u, 2u);
        if (!source) return false;
        memcpy(&value, source, sizeof(value));
        if (!value) return true;
        uint32_t codePoint = value;
        if (value >= 0xd800u && value <= 0xdbffu && i + 1u < maxCharacters)
        {
            uint16_t low = 0;
            source = resolveMemory(runtime, address + (uint32_t)(i + 1u) * 2u, 2u);
            if (!source) return false;
            memcpy(&low, source, sizeof(low));
            if (low >= 0xdc00u && low <= 0xdfffu)
            {
                codePoint = 0x10000u + ((value - 0xd800u) << 10) + low - 0xdc00u;
                ++i;
            }
            else codePoint = 0xfffdu;
        }
        else if (value >= 0xdc00u && value <= 0xdfffu) codePoint = 0xfffdu;
        appendUtf8(output, codePoint);
    }
    return false;
}

static bool writeGuestString(CcRuntimeContext* runtime, uint32_t address,
    uint32_t capacity, const char* value)
{
    if (!address || !capacity || !value) return false;
    size_t length = std::min<size_t>(strlen(value), capacity - 1u);
    if (!busWrite(runtime, address, value, length)) return false;
    const uint8_t zero = 0;
    return busWrite(runtime, address + (uint32_t)length, &zero, 1u);
}

static int compareGuestStringsIgnoreCase(CcRuntimeContext* runtime,
    uint32_t leftAddress, uint32_t rightAddress)
{
    char left[512] = {};
    char right[512] = {};
    if (!readGuestString(runtime, leftAddress, left, sizeof(left)) ||
        !readGuestString(runtime, rightAddress, right, sizeof(right)))
    {
        return leftAddress == rightAddress ? 0 : (leftAddress < rightAddress ? -1 : 1);
    }
    for (size_t i = 0; ; ++i)
    {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 'a' - 'A');
        if (a != b || !a || !b) return (int)a - (int)b;
    }
}

static uint32_t mapInputForRuntime(const CcRuntimeContext* runtime, uint32_t input)
{
    if (ccUsesRetailInputMapping(runtime->package->origin))
    {
        return ccMapInputToRetailLayout(input);
    }
    return ccMapInputToHomebrewLayout(input);
}

static uint32_t findExport(const GuestPackage* package, const char* name)
{
    for (uint32_t i = 0; i < package->export_count; ++i)
    {
        if (package->export_data[i] && package->export_data[i]->name &&
            strcmp(package->export_data[i]->name, name) == 0)
        {
            return package->export_data[i]->offset;
        }
    }
    return 0;
}

static uint32_t findImport(const GuestPackage* package, const char* name)
{
    for (uint32_t i = 0; i < package->import_count; ++i)
    {
        if (package->import_data[i] && package->import_data[i]->name &&
            strcmp(package->import_data[i]->name, name) == 0)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

static uint32_t allocateMemory(CcRuntimeContext* runtime, uint32_t size)
{
    if (!size) return 0;
    uint32_t aligned = (size + 15u) & ~15u;
    if (aligned < size) return 0;
    for (size_t i = 0; i < runtime->heap.size(); ++i)
    {
        if (!runtime->heap[i].free || runtime->heap[i].size < aligned) continue;
        uint32_t address = runtime->heap[i].address;
        uint32_t oldSize = runtime->heap[i].size;
        runtime->heap[i].size = aligned;
        runtime->heap[i].free = false;
        if (oldSize >= aligned + 32u)
        {
            CcRuntimeContext::HeapBlock rest = { address + aligned, oldSize - aligned, true };
            runtime->heap.insert(runtime->heap.begin() + i + 1, rest);
        }
        memset(resolveMemory(runtime, address, aligned), 0, aligned);
        return address;
    }
    uint64_t allocationEnd = (uint64_t)runtime->heapCursor + aligned;
    if (!runtime->legacySystemMmio.empty() &&
        runtime->heapCursor < kLegacySystemMmioStart + kLegacySystemMmioSize &&
        allocationEnd > kLegacySystemMmioStart)
    {
        runtime->heapCursor = kLegacySystemMmioStart + kLegacySystemMmioSize;
    }
    if ((uint64_t)runtime->heapCursor + aligned >
        (uint64_t)runtime->heapStart + runtime->heapMemory.size()) return 0;
    uint32_t address = runtime->heapCursor;
    runtime->heapCursor += aligned;
    runtime->heap.push_back({ address, aligned, false });
    memset(resolveMemory(runtime, address, aligned), 0, aligned);
    return address;
}

static CcRuntimeContext::HeapBlock* findHeapBlock(CcRuntimeContext* runtime, uint32_t address)
{
    for (size_t i = 0; i < runtime->heap.size(); ++i)
    {
        if (runtime->heap[i].address == address) return &runtime->heap[i];
    }
    return NULL;
}

static void freeMemory(CcRuntimeContext* runtime, uint32_t address)
{
    for (size_t i = 0; i < runtime->heap.size(); ++i)
    {
        if (runtime->heap[i].address != address || runtime->heap[i].free) continue;
        runtime->heap[i].free = true;
        if (i + 1u < runtime->heap.size() && runtime->heap[i + 1u].free &&
            runtime->heap[i].address + runtime->heap[i].size ==
                runtime->heap[i + 1u].address)
        {
            runtime->heap[i].size += runtime->heap[i + 1u].size;
            runtime->heap.erase(runtime->heap.begin() + i + 1u);
        }
        if (i > 0 && runtime->heap[i - 1u].free &&
            runtime->heap[i - 1u].address + runtime->heap[i - 1u].size ==
                runtime->heap[i].address)
        {
            runtime->heap[i - 1u].size += runtime->heap[i].size;
            runtime->heap.erase(runtime->heap.begin() + i);
            --i;
        }
        while (!runtime->heap.empty() && runtime->heap.back().free &&
            runtime->heap.back().address + runtime->heap.back().size ==
                runtime->heapCursor)
        {
            runtime->heapCursor = runtime->heap.back().address;
            runtime->heap.pop_back();
        }
        return;
    }
    if (address) printf("cc-arm: ignored free address=0x%08x\n", address);
}

static CcRuntimeContext::ResourceHandle* findResource(CcRuntimeContext* runtime,
    uint32_t address)
{
    for (size_t i = 0; i < runtime->resources.size(); ++i)
    {
        if (runtime->resources[i].address == address) return &runtime->resources[i];
    }
    return NULL;
}

static CcRuntimeContext::FileHandle* findFile(CcRuntimeContext* runtime, uint32_t address)
{
    for (size_t i = 0; i < runtime->files.size(); ++i)
    {
        if (runtime->files[i].address == address) return &runtime->files[i];
    }
    return NULL;
}

static uint32_t fileStream(CcRuntimeContext* runtime, uint32_t handle)
{
    CcRuntimeContext::FileHandle* file = findFile(runtime, handle);
    return file ? file->stream : handle;
}

static uint32_t openFile(CcRuntimeContext* runtime, const char* name, const char* mode)
{
    uint32_t stream = fsys_fopen(name, mode);
    if (!stream) return 0;
    runtime->openStreams.push_back(stream);
    if (ccPackageUsesRetailLayout(runtime->package->origin)) return stream;

    uint32_t address = allocateMemory(runtime, 16u);
    const uint32_t magic = 0x46535953u;
    if (!address || !busWrite(runtime, address, &magic, sizeof(magic)))
    {
        if (address) freeMemory(runtime, address);
        fsys_fclose(stream);
        runtime->openStreams.pop_back();
        return 0;
    }
    runtime->files.push_back({ address, stream });
    return address;
}

static uint32_t closeFile(CcRuntimeContext* runtime, uint32_t handle)
{
    uint32_t stream = fileStream(runtime, handle);
    uint32_t result = fsys_fclose(stream);
    runtime->openStreams.erase(std::remove(runtime->openStreams.begin(),
        runtime->openStreams.end(), stream), runtime->openStreams.end());
    for (size_t i = 0; i < runtime->files.size(); ++i)
    {
        if (runtime->files[i].address != handle) continue;
        freeMemory(runtime, handle);
        runtime->files.erase(runtime->files.begin() + i);
        break;
    }
    return result;
}

static CcRuntimeContext::Semaphore* findSemaphore(CcRuntimeContext* runtime,
    uint32_t address)
{
    for (size_t i = 0; i < runtime->semaphores.size(); ++i)
    {
        if (runtime->semaphores[i].address == address) return &runtime->semaphores[i];
    }
    return NULL;
}

static size_t findActiveTaskIndex(const CcRuntimeContext* runtime,
    uint32_t priority)
{
    for (size_t i = 0; i < runtime->tasks.size(); ++i)
    {
        if (!runtime->tasks[i].finished && runtime->tasks[i].priority == priority)
        {
            return i;
        }
    }
    return runtime->tasks.size();
}

static uint32_t createDynamicImport(CcRuntimeContext* runtime, const char* name)
{
    for (size_t i = 0; i < runtime->dynamicImports.size(); ++i)
    {
        if (runtime->dynamicImports[i] == name) return kDynamicThunkStart + (uint32_t)i * 8u;
    }
    uint32_t slot = (uint32_t)runtime->dynamicImports.size();
    if (slot >= 0x1ff0u) return 0;
    uint32_t index = runtime->package->import_count + slot;
    uint32_t stub[2] = { 0xef000000u | index, 0xe12fff1eu };
    uint32_t address = kDynamicThunkStart + slot * 8u;
    if (!busWrite(runtime, address, stub, sizeof(stub))) return 0;
    runtime->dynamicImports.push_back(name);
    return address;
}

static void recordUnknownImport(CcRuntimeContext* runtime, const char* name)
{
    runtime->stats->unknownImports++;
    std::string value = name ? name : "(invalid)";
    if (std::find(runtime->unknownImportNames.begin(), runtime->unknownImportNames.end(), value) ==
        runtime->unknownImportNames.end())
    {
        runtime->unknownImportNames.push_back(value);
        printf("cc-arm: unknown import %s\n", value.c_str());
    }
}

static void handleMemoryImport(CcRuntimeContext* runtime, Arm32State* state,
    const char* name)
{
    if (!strcmp(name, "malloc") || !strcmp(name, "OSMalloc") || !strcmp(name, "jmalloc"))
    {
        uint32_t requested = state->r[0];
        state->r[0] = allocateMemory(runtime, requested);
        if (requested && !state->r[0])
        {
            printf("cc-arm: allocation failed api=%s size=%u heap=0x%08x blocks=%u\n",
                name, requested, runtime->heapCursor, (unsigned int)runtime->heap.size());
        }
    }
    else if (!strcmp(name, "calloc"))
    {
        uint64_t requested = (uint64_t)state->r[0] * state->r[1];
        uint32_t address = requested <= UINT32_MAX ?
            allocateMemory(runtime, (uint32_t)requested) : 0;
        if (address && requested) memset(resolveMemory(runtime, address, (size_t)requested), 0, (size_t)requested);
        state->r[0] = address;
    }
    else if (!strcmp(name, "free") || !strcmp(name, "OSFree") || !strcmp(name, "jfree"))
    {
        freeMemory(runtime, state->r[0]);
        state->r[0] = 0;
    }
    else if (!strcmp(name, "realloc"))
    {
        uint32_t oldAddress = state->r[0];
        uint32_t newSize = state->r[1];
        CcRuntimeContext::HeapBlock* oldBlock = findHeapBlock(runtime, oldAddress);
        if (!oldAddress) state->r[0] = allocateMemory(runtime, newSize);
        else if (!newSize) { freeMemory(runtime, oldAddress); state->r[0] = 0; }
        else if (oldBlock && oldBlock->size >= newSize) state->r[0] = oldAddress;
        else
        {
            uint32_t oldSize = oldBlock ? oldBlock->size : 0;
            uint32_t address = allocateMemory(runtime, newSize);
            if (address && oldSize)
            {
                memcpy(resolveMemory(runtime, address, std::min(oldSize, newSize)),
                    resolveMemory(runtime, oldAddress, std::min(oldSize, newSize)),
                    std::min(oldSize, newSize));
                freeMemory(runtime, oldAddress);
            }
            state->r[0] = address;
        }
    }
    else if (!strcmp(name, "memset"))
    {
        uint8_t* destination = resolveMemory(runtime, state->r[0], state->r[2]);
        if (destination)
        {
            memset(destination, state->r[1] & 0xffu, state->r[2]);
            noteFramebufferWrite(runtime, state->r[0], state->r[2]);
        }
    }
    else if (!strcmp(name, "memcpy") || !strcmp(name, "memmove"))
    {
        uint8_t* destination = resolveMemory(runtime, state->r[0], state->r[2]);
        uint8_t* source = resolveMemory(runtime, state->r[1], state->r[2]);
        if (destination && source)
        {
            memmove(destination, source, state->r[2]);
            noteFramebufferWrite(runtime, state->r[0], state->r[2]);
        }
    }
    else if (!strcmp(name, "heap_get_block_size"))
    {
        CcRuntimeContext::HeapBlock* block = findHeapBlock(runtime, state->r[0]);
        state->r[0] = block ? block->size : 0;
    }
}

static uint32_t currentOsTick(const CcRuntimeContext* runtime);

static uint64_t currentHostMicros(const CcRuntimeContext* runtime)
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - runtime->startTime).count();
}

static uint64_t currentGuestMicros(const CcRuntimeContext* runtime)
{
    return ccScaleElapsedMicros(currentHostMicros(runtime),
        s_runtimeSpeedScale.load());
}

static uint32_t currentTaskSchedulerTick(const CcRuntimeContext* runtime)
{
    return (uint32_t)(currentGuestMicros(runtime) / 10000u);
}

static void markCurrentTaskAsAudioProducer(CcRuntimeContext* runtime)
{
    if (runtime->currentTaskIndex < runtime->tasks.size())
    {
        runtime->tasks[runtime->currentTaskIndex].audioProducer = true;
    }
}

static void scheduleCurrentTaskDelay(CcRuntimeContext* runtime, uint32_t ticks)
{
    runtime->currentDelayTicks = std::max<uint32_t>(ticks, 1u);
    runtime->yielded = true;
}

static void scheduleCurrentTaskHostDelayMicros(CcRuntimeContext* runtime,
    uint64_t micros)
{
    runtime->currentHostDelayMicros = std::max<uint64_t>(micros, 1u);
    runtime->yielded = true;
}

static void finishCurrentTask(Arm32State* state)
{
    state->r[0] = 0;
    state->r[15] = kExitAddress;
}

static bool handleLegacyAudioImport(CcRuntimeContext* runtime, Arm32State* state,
    const char* name, bool* completed)
{
    *completed = true;
    if (!strcmp(name, "DVCOpenDevice"))
    {
        char deviceName[128] = {};
        if (!readGuestString(runtime, state->r[0], deviceName, sizeof(deviceName)) ||
            strcmp(deviceName, kDvcAudioDeviceName))
        {
            state->r[0] = 0;
            return true;
        }
        runtime->dvcAudioHandle = kDvcAudioHandle;
        runtime->dvcAudioSampleRate = kDvcAudioDefaultSampleRate;
        runtime->dvcAudioVolume = kDvcAudioMaxVolume;
        runtime->dvcAudioStarted = false;
        markCurrentTaskAsAudioProducer(runtime);
        state->r[0] = runtime->dvcAudioHandle;
        return true;
    }
    if (!strcmp(name, "DVCControlDevice"))
    {
        if (state->r[0] != runtime->dvcAudioHandle || !runtime->dvcAudioHandle)
        {
            state->r[0] = UINT32_MAX;
            return true;
        }
        uint32_t* argument = (uint32_t*)resolveMemory(runtime, state->r[3],
            sizeof(uint32_t));
        if (state->r[2] == kDvcAudioSetSampleRate && argument && *argument)
        {
            runtime->dvcAudioSampleRate = *argument;
        }
        else if (state->r[2] == kDvcAudioStartPlayback && argument && *argument &&
            !runtime->dvcAudioStarted)
        {
            waveout_args* args = (waveout_args*)malloc(sizeof(*args));
            if (!args)
            {
                state->r[0] = UINT32_MAX;
                return true;
            }
            args->sample_rate = runtime->dvcAudioSampleRate;
            args->format = AFMT_S16_LE;
            args->channel = 2;
            args->volume = 255;
            runtime->dvcAudioStarted = waveout_open(args) != 0;
        }
        state->r[0] = runtime->dvcAudioStarted ||
            state->r[2] != kDvcAudioStartPlayback ? 0u : UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "DVCWriteDevice"))
    {
        markCurrentTaskAsAudioProducer(runtime);
        if (state->r[2] != runtime->dvcAudioHandle || !runtime->dvcAudioStarted)
        {
            state->r[0] = UINT32_MAX;
            return true;
        }
        const bool skipsAudioOutput = waveout_skips_audio_output();
        if (!skipsAudioOutput && !waveout_can_write_nonblocking())
        {
            state->r[15] -= 4u;
            scheduleCurrentTaskDelay(runtime, 1u);
            *completed = false;
            return true;
        }
        char* data = (char*)resolveMemory(runtime, state->r[0], state->r[1]);
        char* copy = data && state->r[1] ? (char*)malloc(state->r[1]) : NULL;
        if (copy) memcpy(copy, data, state->r[1]);
        if (!copy)
        {
            state->r[0] = UINT32_MAX;
            return true;
        }
        uint32_t written = skipsAudioOutput ?
            waveout_write(runtime->dvcAudioHandle, copy, (int)state->r[1]) :
            waveout_try_write(runtime->dvcAudioHandle, copy, (int)state->r[1]);
        state->r[0] = written ? state->r[1] : UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "DVCCloseDevice"))
    {
        if (runtime->dvcAudioStarted) waveout_close(runtime->dvcAudioHandle);
        runtime->dvcAudioHandle = 0;
        runtime->dvcAudioStarted = false;
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "SYSSetVolume"))
    {
        runtime->dvcAudioVolume = std::min<uint32_t>(state->r[0],
            kDvcAudioMaxVolume);
        waveout_set_volume((runtime->dvcAudioVolume * 255u +
            kDvcAudioMaxVolume / 2u) / kDvcAudioMaxVolume);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "SYSGetVolume") || !strcmp(name, "get_game_vol"))
    {
        state->r[0] = runtime->dvcAudioVolume ? runtime->dvcAudioVolume :
            kDvcAudioMaxVolume;
        return true;
    }
    if (!strcmp(name, "HP_Mute_sw"))
    {
        state->r[0] = waveout_mute(state->r[0]);
        return true;
    }
    if (!strcmp(name, "wavaopen") || !strcmp(name, "waveioc") ||
        !strcmp(name, "waveclose"))
    {
        state->r[0] = 0;
        return true;
    }
    return false;
}

static void requestCcGuestExit(CcRuntimeContext* runtime, Arm32State* state, const char* reason)
{
    printf("cc-arm: guest exit requested by %s task=%u lr=0x%08x\n",
        reason ? reason : "<unknown>", runtime->currentTaskIndex, state->r[14]);
    finishCurrentTask(state);
    runtime->yielded = false;
    runtime->currentDelayTicks = 0;
    runtime->stats->guestCompleted = true;
    s_stopRequested.store(true);
}

static bool parsePositiveScaleEnv(const char* name, double* output)
{
    const char* value = getenv(name);
    if (!value || !value[0] || !output)
    {
        return false;
    }
    char* end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || *end || parsed <= 0.0)
    {
        return false;
    }
    *output = parsed;
    return true;
}

static bool handleSvc(void* userData, Arm32State* state, uint32_t immediate)
{
    CcRuntimeContext* runtime = (CcRuntimeContext*)userData;
    const char* name = NULL;
    uint32_t svcAddress = state->r[15] - 4u;
    if (immediate < runtime->package->import_count && runtime->package->import_data[immediate])
    {
        name = runtime->package->import_data[immediate]->name;
        if (ccPackageUsesHomebrewLayout(runtime->package->origin) &&
            runtime->package->import_data[immediate]->offset == svcAddress)
        {
            state->r[15] = state->r[14] & ~1u;
            if (state->r[14] & 1u) state->cpsr |= 1u << 5;
            else state->cpsr &= ~(1u << 5);
        }
    }
    else if (immediate >= runtime->package->import_count)
    {
        uint32_t dynamic = immediate - runtime->package->import_count;
        if (dynamic < runtime->dynamicImports.size())
        {
            name = runtime->dynamicImports[dynamic].c_str();
        }
    }
    runtime->stats->importCalls++;
    snprintf(runtime->stats->lastImport, sizeof(runtime->stats->lastImport), "%s",
        name ? name : "(invalid)");
    runtime->stats->lastImportPc = svcAddress;
    runtime->stats->lastImportReturnAddress = state->r[14];
    if (!name)
    {
        recordUnknownImport(runtime, NULL);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "consoleEnable") || !strcmp(name, "consoleDisable") ||
        !strcmp(name, "PMSetMode"))
    {
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "TaskMediaFunStop"))
    {
        if (runtime->stats->framesSubmitted > 0)
        {
            requestCcGuestExit(runtime, state, name);
        }
        else
        {
            printf("cc-arm: ignored startup TaskMediaFunStop before first submitted frame\n");
            state->r[0] = 0;
        }
        return true;
    }
    if (!strcmp(name, "vxGoHome") || !strcmp(name, "abort") ||
        !strcmp(name, "av_end_thread") || !strcmp(name, "av_queue_abort"))
    {
        requestCcGuestExit(runtime, state, name);
        return true;
    }
    if (!strcmp(name, "cmGetSysModel"))
    {
        state->r[0] = writeGuestString(runtime, state->r[0], state->r[1], "CC1800") ? 0u : UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "cmGetSysVersion"))
    {
        state->r[0] = writeGuestString(runtime, state->r[0], state->r[1], "1.0") ? 0u : UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "get_current_language"))
    {
        state->r[0] = 0;
        return true;
    }

    if (!strcmp(name, "dl_get_proc"))
    {
        char requested[128] = {};
        if (!readGuestString(runtime, state->r[1], requested, sizeof(requested))) { state->r[0] = 0; return true; }
        uint32_t address = findExport(runtime->package, requested);
        uint32_t index = findImport(runtime->package, requested);
        if (!address && index != UINT32_MAX) address = runtime->package->import_data[index]->offset;
        if (!address) address = createDynamicImport(runtime, requested);
        state->r[0] = address;
        snprintf(runtime->stats->lastImport, sizeof(runtime->stats->lastImport), "dl_get_proc:%s", requested);
        return true;
    }
    if (!strcmp(name, "GetDLHandle") || !strcmp(name, "get_dl_handle")) { state->r[0] = kLoaderHandle; return true; }
    if (!strcmp(name, "__to_locale_ansi") || !strcmp(name, "_to_locale_ansi")) { state->r[0] = kLocaleString; return true; }
    if (!strcmp(name, "malloc") || !strcmp(name, "calloc") || !strcmp(name, "realloc") ||
        !strcmp(name, "free") || !strcmp(name, "OSMalloc") || !strcmp(name, "OSFree") ||
        !strcmp(name, "jmalloc") || !strcmp(name, "jfree") || !strcmp(name, "memset") ||
        !strcmp(name, "memcpy") || !strcmp(name, "memmove") || !strcmp(name, "heap_get_block_size"))
    {
        handleMemoryImport(runtime, state, name);
        return true;
    }
    if (!strcmp(name, "OSTaskCreate"))
    {
        CcRuntimeContext::Task task = {};
        task.entry = state->r[0];
        task.argument = state->r[1];
        task.stack = state->r[2];
        task.priority = state->r[3];
        if (task.entry && task.stack) runtime->tasks.push_back(task);
        runtime->stats->tasksCreated = (uint32_t)runtime->tasks.size();
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "OSTaskDel"))
    {
        const uint32_t priority = state->r[0] & 0xffu;
        size_t targetTaskIndex = findActiveTaskIndex(runtime, priority);
        if (targetTaskIndex == runtime->tasks.size())
        {
            state->r[0] = 41u;
        }
        else if (targetTaskIndex == 0)
        {
            requestCcGuestExit(runtime, state, name);
        }
        else if (targetTaskIndex == runtime->currentTaskIndex)
        {
            printf("cc-arm: task exit requested by OSTaskDel task=%u priority=%u\n",
                runtime->currentTaskIndex, priority);
            finishCurrentTask(state);
        }
        else
        {
            printf("cc-arm: task delete requested by OSTaskDel task=%u priority=%u\n",
                (uint32_t)targetTaskIndex, priority);
            runtime->tasks[targetTaskIndex].finished = true;
            state->r[0] = 0;
        }
        return true;
    }
    if (!strcmp(name, "OSTaskQuery"))
    {
        const uint32_t priority = state->r[0] & 0xffu;
        state->r[0] = findActiveTaskIndex(runtime, priority) <
            runtime->tasks.size() ? 0u : 41u;
        return true;
    }
    if (!strcmp(name, "OSTimeDlyHMSM"))
    {
        scheduleCurrentTaskDelay(runtime, ccHmsmToOsTicks(state->r[0], state->r[1],
            state->r[2], state->r[3], OS_TICKS_PER_SEC));
        return false;
    }
    if (!strcmp(name, "delay_ms"))
    {
        scheduleCurrentTaskDelay(runtime,
            ccMillisecondsToOsTicks(state->r[0], OS_TICKS_PER_SEC));
        return false;
    }
    if (!strcmp(name, "OSTimeDly") || !strcmp(name, "delay"))
    {
        scheduleCurrentTaskDelay(runtime, state->r[0]);
        return false;
    }
    if (!strcmp(name, "OSTimeGet") || !strcmp(name, "GetTickCount") || !strcmp(name, "OSTimerGetTickTimeus"))
    {
        uint64_t micros = currentGuestMicros(runtime);
        if (!strcmp(name, "OSTimerGetTickTimeus")) state->r[0] = (uint32_t)micros;
        else if (!strcmp(name, "GetTickCount")) state->r[0] = (uint32_t)(micros / 1000u);
        else state->r[0] = currentOsTick(runtime);
        return true;
    }
    if (!strcmp(name, "OSSemCreate"))
    {
        uint32_t address = allocateMemory(runtime, 16);
        if (address) runtime->semaphores.push_back({ address, state->r[0] });
        state->r[0] = address;
        return true;
    }
    if (!strcmp(name, "OSSemPend"))
    {
        CcRuntimeContext::Semaphore* semaphore = findSemaphore(runtime, state->r[0]);
        if (semaphore && semaphore->count)
        {
            semaphore->count--;
            if (state->r[2]) { uint8_t zero = 0; busWrite(runtime, state->r[2], &zero, 1); }
            state->r[0] = 0;
            return true;
        }
        state->r[15] -= 4u;
        runtime->currentDelayTicks = 1;
        runtime->yielded = true;
        return false;
    }
    if (!strcmp(name, "OSSemPost"))
    {
        CcRuntimeContext::Semaphore* semaphore = findSemaphore(runtime, state->r[0]);
        if (semaphore && semaphore->count != UINT32_MAX) semaphore->count++;
        state->r[0] = semaphore ? 0 : 1;
        return true;
    }
    if (!strcmp(name, "OSSemAccept"))
    {
        CcRuntimeContext::Semaphore* semaphore = findSemaphore(runtime, state->r[0]);
        state->r[0] = semaphore && semaphore->count ? semaphore->count-- : 0;
        return true;
    }

    if (!strcmp(name, "fsys_fopen"))
    {
        char fileName[512] = {};
        char mode[16] = {};
        if (!readGuestString(runtime, state->r[0], fileName, sizeof(fileName)) ||
            !readGuestString(runtime, state->r[1], mode, sizeof(mode)))
        {
            state->r[0] = 0;
            return true;
        }
        state->r[0] = openFile(runtime, fileName, mode);
        return true;
    }
    if (!strcmp(name, "fsys_fopenW"))
    {
        std::string fileName;
        char mode[16] = {};
        if (!readGuestWideString(runtime, state->r[0], &fileName, 512u) ||
            !readGuestString(runtime, state->r[1], mode, sizeof(mode)))
        {
            state->r[0] = 0;
            return true;
        }
        state->r[0] = openFile(runtime, fileName.c_str(), mode[0] ? mode : "rb");
        return true;
    }
    if (!strcmp(name, "fsys_fclose"))
    {
        state->r[0] = closeFile(runtime, state->r[0]);
        return true;
    }
    if (!strcmp(name, "fsys_fread") || !strcmp(name, "fsys_fwrite"))
    {
        uint64_t requested = (uint64_t)state->r[1] * state->r[2];
        void* data = requested <= UINT32_MAX ?
            resolveMemory(runtime, state->r[0], (uint32_t)requested) : NULL;
        uint32_t stream = fileStream(runtime, state->r[3]);
        state->r[0] = data ? (!strcmp(name, "fsys_fread") ?
            vm_fread(data, state->r[1], state->r[2], stream) :
            fsys_fwrite(data, state->r[1], state->r[2], stream)) : 0;
        return true;
    }
    if (!strcmp(name, "fsys_fseek"))
    {
        state->r[0] = fsys_fseek(fileStream(runtime, state->r[0]),
            state->r[1], state->r[2]);
        return true;
    }
    if (!strcmp(name, "fsys_ftell"))
    {
        state->r[0] = fsys_ftell(fileStream(runtime, state->r[0]));
        return true;
    }
    if (!strcmp(name, "fsys_feof"))
    {
        state->r[0] = fsys_feof(fileStream(runtime, state->r[0]));
        return true;
    }
    if (!strcmp(name, "fsys_ferror")) { state->r[0] = 0; return true; }

    if (!strcmp(name, "lcd_get_frame") || !strcmp(name, "_lcd_get_frame") || !strcmp(name, "LCDGetFB"))
    {
        state->r[0] = kFramebufferAddress;
        return true;
    }
    if (!strcmp(name, "LCDGetWidth") || !strcmp(name, "get_lcd_width"))
    {
        state->r[0] = kFramebufferWidth;
        return true;
    }
    if (!strcmp(name, "LCDGetHeight") || !strcmp(name, "get_lcd_height"))
    {
        state->r[0] = kFramebufferHeight;
        return true;
    }
    if (!strcmp(name, "lcd_set_frame") || !strcmp(name, "_lcd_set_frame"))
    {
        presentFramebuffer(runtime, runtime->framebufferAddress);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "LCDFlushFB") || !strcmp(name, "LCDFlushFBZoom"))
    {
        presentFramebuffer(runtime, runtime->framebufferAddress);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "BMF_SetLcdFramePtr"))
    {
        if (resolveMemory(runtime, state->r[0], kFramebuffer16Size))
        {
            runtime->framebufferAddress = state->r[0];
        }
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "SysLcdClear"))
    {
        memset(runtime->framebuffer.data(), 0, runtime->framebuffer.size());
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "LCDSetFBBit"))
    {
        if (state->r[0] == 16u || state->r[0] == 32u)
        {
            runtime->framebufferBits = state->r[0];
            runtime->framebufferBitsExplicit = true;
            runtime->bus.directFramebuffer = runtime->framebuffer.data();
        }
        printf("cc-arm: framebuffer bits=%u\n", state->r[0]);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "LCDIsDoubleFBEnabled"))
    {
        state->r[0] = 1;
        return true;
    }
    if (!strcmp(name, "LCDGetFBFormat") || !strcmp(name, "LCDEnableDoubleFB") ||
        !strcmp(name, "LCDDisableDoubleFB") || !strcmp(name, "LCDSetFBFormat") ||
        !strcmp(name, "LCDInit") || !strcmp(name, "LCDSetRefreshRate") ||
        !strcmp(name, "LCDSetBrightness") || !strcmp(name, "FlushDCache") ||
        !strcmp(name, "InvalidICache") || !strcmp(name, "fsys_RefreshCache") ||
        !strcmp(name, "BMF_SelectPixelFunc"))
    {
        state->r[0] = 0;
        return true;
    }

    if (!strcmp(name, "kbd_get_status") || !strcmp(name, "_kbd_get_status") ||
        !strcmp(name, "rmt_get_status"))
    {
        GuestKeyStatus status = {};
        _kbd_get_status(&status);
        status.pressed = mapInputForRuntime(runtime, status.pressed);
        status.released = mapInputForRuntime(runtime, status.released);
        status.status = mapInputForRuntime(runtime, status.status);
        busWrite(runtime, state->r[0], &status, sizeof(status));
        state->r[0] = 0; return true;
    }
    if (!strcmp(name, "_kbd_get_key") || !strcmp(name, "kbd_get_key") ||
        !strcmp(name, "sys_get_key") || !strcmp(name, "KBDGetSKey") ||
        !strcmp(name, "KBDGetSKeyStatus") || !strcmp(name, "RMTGetSKey"))
    {
        uint32_t key = _kbd_get_key();
        state->r[0] = mapInputForRuntime(runtime, key);
        return true;
    }
    if (!strcmp(name, "Tp_Get_Pos"))
    {
        state->r[0] = UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "sys_judge_event") || !strcmp(name, "_sys_judge_event"))
    {
        state->r[0] = inputHasPendingEvent();
        return true;
    }
    if (!strcmp(name, "stricmp"))
    {
        state->r[0] = (uint32_t)compareGuestStringsIgnoreCase(runtime,
            state->r[0], state->r[1]);
        return true;
    }

    if (!strcmp(name, "dl_res_open"))
    {
        char resourceName[256] = {};
        bool hasName = readGuestString(runtime, state->r[2], resourceName, sizeof(resourceName));
        if (!hasName) hasName = readGuestString(runtime, state->r[1], resourceName, sizeof(resourceName));
        if (!hasName) hasName = readGuestString(runtime, state->r[0], resourceName, sizeof(resourceName));
        if (!hasName) { state->r[0] = 0; return true; }
        GuestResourceEntry* entry = guestPackageFindResource(runtime->package, resourceName);
        if (!entry) { state->r[0] = 0; return true; }
        uint32_t address = allocateMemory(runtime, 16);
        runtime->resources.push_back({ address, entry, 0, 0 });
        runtimeResourceMonitorRecordGuestOpen(
            resourceName, entry, entry->decoded_data != NULL);
        state->r[0] = address; return true;
    }
    if (!strcmp(name, "dl_res_get_size"))
    {
        CcRuntimeContext::ResourceHandle* resource = findResource(runtime, state->r[0]);
        state->r[0] = resource ? resource->entry->size : 0; return true;
    }
    if (!strcmp(name, "dl_res_get_data"))
    {
        CcRuntimeContext::ResourceHandle* resource = findResource(runtime, state->r[0]);
        if (!resource) { state->r[0] = 0; return true; }
        const uint8_t* data = guestPackageResourceData(runtime->package, resource->entry);
        if (!data) { state->r[0] = 0; return true; }
        if (!state->r[1])
        {
            if (!resource->dataAddress)
            {
                resource->dataAddress = allocateMemory(runtime, resource->entry->size);
                if (resource->dataAddress)
                {
                    busWrite(runtime, resource->dataAddress, data, resource->entry->size);
                    runtimeResourceMonitorRecordGuestLoadContent(
                        resource->entry, resource->dataAddress, data,
                        resource->entry->size, resource->entry->size);
                }
            }
            state->r[0] = resource->dataAddress;
            return true;
        }
        uint64_t requested = state->r[3] ? (uint64_t)state->r[2] * state->r[3] : state->r[2];
        if (requested > UINT32_MAX) { state->r[0] = 0; return true; }
        uint32_t available = resource->position < resource->entry->size ? resource->entry->size - resource->position : 0;
        uint32_t bytes = std::min<uint32_t>((uint32_t)requested, available);
        if (!data || !busWrite(runtime, state->r[1], data + resource->position, bytes)) bytes = 0;
        resource->position += bytes;
        runtimeResourceMonitorRecordGuestLoadContent(
            resource->entry, state->r[1], data + resource->position - bytes,
            bytes, resource->position);
        state->r[0] = state->r[3] ? bytes / state->r[3] : bytes;
        return true;
    }
    if (!strcmp(name, "dl_res_close"))
    {
        uint32_t address = state->r[0];
        for (size_t i = 0; i < runtime->resources.size(); ++i)
        {
            if (runtime->resources[i].address == address)
            {
                runtimeResourceMonitorRecordGuestClose(runtime->resources[i].entry);
                freeMemory(runtime, runtime->resources[i].dataAddress);
                runtime->resources.erase(runtime->resources.begin() + i);
                freeMemory(runtime, address);
                break;
            }
        }
        state->r[0] = 0;
        return true;
    }

    if (!strcmp(name, "waveout_open"))
    {
        markCurrentTaskAsAudioProducer(runtime);
        waveout_args* guest = (waveout_args*)resolveMemory(runtime, state->r[0], sizeof(waveout_args));
        if (!guest) { state->r[0] = 0; return true; }
        waveout_args* copy = (waveout_args*)malloc(sizeof(*copy));
        if (!copy) { state->r[0] = 0; return true; }
        *copy = *guest; state->r[0] = waveout_open(copy); return true;
    }
    if (!strcmp(name, "waveout_write"))
    {
        markCurrentTaskAsAudioProducer(runtime);
        const bool skipsAudioOutput = waveout_skips_audio_output();
        if (!skipsAudioOutput && !waveout_can_write_nonblocking())
        {
            state->r[15] -= 4u;
            scheduleCurrentTaskHostDelayMicros(runtime,
                1000000ull / OS_TICKS_PER_SEC);
            return false;
        }
        char* data = (char*)resolveMemory(runtime, state->r[1], state->r[2]);
        char* copy = data && state->r[2] ? (char*)malloc(state->r[2]) : NULL;
        if (copy) memcpy(copy, data, state->r[2]);
        if (!copy)
        {
            state->r[0] = 0;
        }
        else if (skipsAudioOutput)
        {
            state->r[0] = waveout_write(state->r[0], copy, (int)state->r[2]);
        }
        else
        {
            state->r[0] = waveout_try_write(state->r[0], copy, (int)state->r[2]);
        }
        return true;
    }
    if (!strcmp(name, "waveout_close")) { state->r[0] = waveout_close(state->r[0]); return true; }
    if (!strcmp(name, "waveout_can_write"))
    {
        markCurrentTaskAsAudioProducer(runtime);
        state->r[0] = waveout_can_write_nonblocking();
        return true;
    }
    if (!strcmp(name, "waveout_set_volume")) { state->r[0] = waveout_set_volume(state->r[0]); return true; }
    if (!strcmp(name, "waveout_mute")) { state->r[0] = waveout_mute(state->r[0]); return true; }

    bool legacyAudioCompleted = true;
    if (handleLegacyAudioImport(runtime, state, name, &legacyAudioCompleted))
    {
        return legacyAudioCompleted;
    }

    if (!strcmp(name, "printf"))
    {
        state->r[0] = 0;
        return true;
    }

    recordUnknownImport(runtime, name);
    state->r[0] = 0;
    return true;
}

static Arm32RunResult runState(CcRuntimeContext* runtime, Arm32State* state,
    uint64_t sliceInstructions = kSliceInstructions)
{
    pauseGateWaitForResume();
    runtime->yielded = false;
    runtime->currentDelayTicks = 0;
    runtime->currentHostDelayMicros = 0;
    runtime->faultAddress = 0;
    runtime->faultSize = 0;
    runtime->faultWrite = false;
    runtime->faultFetch = false;
    uint64_t before = state->instructions;
    Arm32RunResult result;
#if defined(DINGOO_PIE_ARM32_DYNARMIC)
    if (s_useOptimizedBackend.load() && !runtime->bus.profilePcSamples)
    {
        result = arm32RunDynarmic(state, &runtime->bus, kExitAddress,
            state->instructions + sliceInstructions);
    }
    else
#endif
    {
        result = arm32Run(state, &runtime->bus, kExitAddress,
            state->instructions + sliceInstructions);
    }
    processPendingCheatCodeCacheFlush(runtime);
    runtime->stats->instructions += state->instructions - before;
    uint64_t targetIps = s_targetInstructionsPerSecond.load();
    if (targetIps)
    {
        uint64_t pacedInstructions = runtime->stats->instructions -
            runtime->instructionPacingStartInstructions;
        uint64_t expectedMicros = ccInstructionsToMicros(pacedInstructions, targetIps);
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        uint64_t hostMicros = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            now - runtime->instructionPacingStartTime).count();
        if (hostMicros > expectedMicros + kInstructionPacingMaxLagMicros)
        {
            runtime->instructionPacingStartTime = now;
            runtime->instructionPacingStartInstructions = runtime->stats->instructions;
            return result;
        }
        if (expectedMicros > hostMicros)
        {
            std::this_thread::sleep_for(
                std::chrono::microseconds(expectedMicros - hostMicros));
        }
    }
    return result;
}

static uint32_t currentOsTick(const CcRuntimeContext* runtime)
{
    return (uint32_t)(currentGuestMicros(runtime) / 10000u);
}

static void profileCcRuntime(CcRuntimeContext* runtime)
{
    if (!runtimeLogProfileEnabled())
    {
        return;
    }
    uint64_t elapsedMillis = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - runtime->startTime).count();
    if (elapsedMillis - runtime->profileLastMillis < runtimeLogProfileIntervalMs())
    {
        return;
    }
    uint32_t activeTasks = 0;
    uint32_t delayedTasks = 0;
    uint32_t firstPc = 0;
    uint32_t firstDelay = 0;
    for (size_t i = 0; i < runtime->tasks.size(); ++i)
    {
        const CcRuntimeContext::Task& task = runtime->tasks[i];
        if (task.finished)
        {
            continue;
        }
        ++activeTasks;
        if (task.delayTicks)
        {
            ++delayedTasks;
        }
        if (!firstPc)
        {
            firstPc = task.state.r[15];
            firstDelay = task.delayTicks;
        }
    }
    uint64_t intervalInstructions = runtime->stats->instructions -
        runtime->profileLastInstructions;
    uint64_t submittedFrames = framebufferConsumeSubmittedCount();
    uint64_t framebufferCopyMicros = framebufferConsumeCopyMicros();
    uint64_t totalFrameIntervalMicros = 0;
    uint64_t maxFrameIntervalMicros = 0;
    uint64_t frameIntervalsOver25ms = 0;
    uint64_t frameIntervalsOver33ms = 0;
    framebufferConsumeTimingStats(&totalFrameIntervalMicros, &maxFrameIntervalMicros,
        &frameIntervalsOver25ms, &frameIntervalsOver33ms);
    uint64_t averageFrameIntervalMicros = submittedFrames ?
        totalFrameIntervalMicros / submittedFrames : 0;
    printf("cc-profile: elapsed_ms=%llu ips=%llu tick=%u tasks=%u delayed=%u "
        "pc=0x%08x deadline=%u frames=%u fb_submit=%llu fb_copy_us=%llu "
        "fb_interval_us=%llu/%llu over25=%llu over33=%llu last=%s\n",
        (unsigned long long)elapsedMillis,
        (unsigned long long)runtimeLogRatePerSecond(intervalInstructions,
            elapsedMillis - runtime->profileLastMillis),
        currentOsTick(runtime), activeTasks, delayedTasks, firstPc, firstDelay,
        runtime->stats->framesSubmitted,
        (unsigned long long)submittedFrames,
        (unsigned long long)framebufferCopyMicros,
        (unsigned long long)averageFrameIntervalMicros,
        (unsigned long long)maxFrameIntervalMicros,
        (unsigned long long)frameIntervalsOver25ms,
        (unsigned long long)frameIntervalsOver33ms,
        runtime->stats->lastImport[0] ? runtime->stats->lastImport : "(none)");
    auto printHotspots = [&](const char* label,
        const std::vector<uint32_t>& samples)
    {
        struct Hotspot
        {
            uint32_t count;
            uint32_t index;
        };
        Hotspot top[8] = {};
        for (uint32_t index = 0; index < samples.size(); ++index)
        {
            uint32_t count = samples[index];
            if (count <= top[7].count)
            {
                continue;
            }
            uint32_t position = 7;
            while (position > 0 && count > top[position - 1].count)
            {
                top[position] = top[position - 1];
                --position;
            }
            top[position] = { count, index };
        }
        printf("cc-profile: %s", label);
        for (const Hotspot& hotspot : top)
        {
            if (!hotspot.count)
            {
                break;
            }
            printf(" 0x%08x:%u", runtime->package->origin +
                hotspot.index * 4u, hotspot.count);
        }
        printf("\n");
    };
    printHotspots("pc", runtime->profilePcSamples);
    printHotspots("lr", runtime->profileLrSamples);
    runtime->profileLastMillis = elapsedMillis;
    runtime->profileLastInstructions = runtime->stats->instructions;
}

static bool decodeArmBranchTarget(uint32_t address, uint32_t instruction,
    uint32_t* target)
{
    if (!target || (instruction & 0xff000000u) != 0xeb000000u)
    {
        return false;
    }
    int32_t displacement = (int32_t)(instruction << 8) >> 6;
    *target = address + 8u + (uint32_t)displacement;
    return true;
}

static bool writeArmTailBranch(CcRuntimeContext* runtime, uint32_t address,
    uint32_t target)
{
    int64_t displacement = (int64_t)target - ((int64_t)address + 8);
    if ((displacement & 3) != 0 || displacement < -0x02000000ll ||
        displacement > 0x01fffffcll)
    {
        return false;
    }
    uint32_t instruction = 0xea000000u |
        ((uint32_t)(displacement >> 2) & 0x00ffffffu);
    return busWrite(runtime, address, &instruction, sizeof(instruction));
}

static void patchLegacySdkAllocator(CcRuntimeContext* runtime)
{
    uint32_t mallocIndex = findImport(runtime->package, "malloc");
    uint32_t freeIndex = findImport(runtime->package, "free");
    if (mallocIndex == UINT32_MAX || freeIndex == UINT32_MAX)
    {
        return;
    }
    uint32_t mallocImport = runtime->package->import_data[mallocIndex]->offset;
    uint32_t freeImport = runtime->package->import_data[freeIndex]->offset;
    uint32_t patchedMalloc = 0;
    uint32_t patchedFree = 0;
    uint32_t start = runtime->package->origin;
    uint32_t end = start + runtime->package->prog_size;
    for (uint32_t address = start; address + 48u <= end; address += 4u)
    {
        uint32_t* code = (uint32_t*)resolveMemory(runtime, address, 48u);
        if (!code)
        {
            break;
        }
        uint32_t target = 0;
        if (!patchedMalloc && code[0] == 0xe92d4008u &&
            decodeArmBranchTarget(address + 4u, code[1], &target) &&
            target == mallocImport)
        {
            if (writeArmTailBranch(runtime, address, mallocImport))
            {
                patchedMalloc = address;
            }
        }
        if (!patchedFree && code[0] == 0xe92d4038u && code[2] == 0xe3a04000u)
        {
            for (uint32_t i = 1; i < 12u; ++i)
            {
                if (decodeArmBranchTarget(address + i * 4u, code[i], &target) &&
                    target == freeImport)
                {
                    if (writeArmTailBranch(runtime, address, freeImport))
                    {
                        patchedFree = address;
                    }
                    break;
                }
            }
        }
        if (patchedMalloc && patchedFree)
        {
            break;
        }
    }
    if (patchedMalloc || patchedFree)
    {
        printf("cc-arm: patched SDK allocator wrappers malloc=0x%08x free=0x%08x\n",
            patchedMalloc, patchedFree);
    }
}

static bool initializeRuntime(CcRuntimeContext* runtime, const char* path)
{
    bool homebrewLayout = ccPackageUsesHomebrewLayout(runtime->package->origin);
    runtime->ramStart = homebrewLayout ? kCcHomebrewRamStart : kCcRetailRamStart;
    if (homebrewLayout)
    {
        uint64_t heapStart = ((uint64_t)runtime->package->origin +
            runtime->package->prog_size + 0xfffu) & ~0xfffull;
        if (heapStart > (uint64_t)kCcHomebrewRamStart + kCcHomebrewRamSize)
        {
            return false;
        }
        runtime->heapStart = kCcHomebrewHeapStart;
        runtime->systemMemory.resize(kCcHomebrewSystemRamSize);
        runtime->ram.resize(kCcHomebrewRamSize);
        runtime->heapMemory.resize(kCcHomebrewHeapSize);
    }
    else
    {
        runtime->heapStart = kCcRetailHeapStart;
        runtime->ram.resize(kCcRetailRamSize);
        runtime->heapMemory.resize(kCcRetailHeapSize);
    }
    runtime->stack.resize(kStackSize);
    runtime->framebuffer.resize(kCcVideoMemorySize);
    runtime->legacyMmio.resize(kLegacyMmioSize);
    runtime->framebufferAddress = kFramebufferAddress;
    runtime->framebufferBits = 16u;
    memcpy(runtime->ram.data() + runtime->package->origin - runtime->ramStart,
        runtime->package->bin_data, runtime->package->prog_size);
    if (homebrewLayout)
    {
        runtime->legacyLowMemory.resize(kLegacyLowMemorySize);
        runtime->legacyAudioMmio.resize(kLegacyAudioMmioSize);
        runtime->legacySystemMmio.resize(kLegacySystemMmioSize);
    }
    runtime->heapCursor = runtime->heapStart;
    if (homebrewLayout)
    {
        uint32_t statusOffset = kLegacyGraphicsStatus - kLegacySystemMmioStart;
        memcpy(runtime->legacySystemMmio.data() + statusOffset,
            &kLegacyGraphicsReady, sizeof(kLegacyGraphicsReady));
    }
    runtime->tasks.reserve(32);
    runtime->bus = { runtime, busFetch, busRead, busWrite, handleSvc };
    runtime->bus.directSystemRam = runtime->systemMemory.data();
    runtime->bus.directSystemRamBase = kCcHomebrewSystemRamStart;
    runtime->bus.directSystemRamSize =
        (uint32_t)runtime->systemMemory.size();
    runtime->bus.directRam = runtime->ram.data();
    runtime->bus.directRamBase = runtime->ramStart;
    runtime->bus.directRamSize = (uint32_t)runtime->ram.size();
    runtime->bus.directStack = runtime->stack.data();
    runtime->bus.directStackBase = kStackStart;
    runtime->bus.directStackSize = kStackSize;
    runtime->bus.directHeap = runtime->heapMemory.data();
    runtime->bus.directHeapBase = runtime->heapStart;
    runtime->bus.directHeapSize = runtime->legacySystemMmio.empty() ?
        (uint32_t)runtime->heapMemory.size() :
        kLegacySystemMmioStart - runtime->heapStart;
    runtime->bus.directFramebufferBase = kFramebufferAddress;
    runtime->bus.directFramebufferSize = (uint32_t)runtime->framebuffer.size();
    runtime->bus.directProgram = runtime->ram.data() +
        (runtime->package->origin - runtime->ramStart);
    runtime->bus.directProgramBase = runtime->package->origin;
    runtime->bus.directProgramSize = runtime->package->prog_size;
    runtime->bus.directThunkBase = kDynamicThunkStart;
    runtime->bus.directThunkSize = 0x10000u;
    patchLegacySdkAllocator(runtime);
    std::string fileName = gameFileNameFromPath(path ? path : "game.cc");
    std::string locale = ".\\" + fileName;
    busWrite(runtime, kLocaleString, locale.c_str(), locale.size() + 1);
    std::vector<uint16_t> widePath(locale.size() + 1);
    for (size_t i = 0; i < locale.size(); ++i)
    {
        widePath[i] = (uint8_t)locale[i];
    }
    busWrite(runtime, kAppPathWideString, widePath.data(), widePath.size() * sizeof(uint16_t));
    for (uint32_t i = 0; i < runtime->package->import_count; ++i)
    {
        if (!runtime->package->import_data[i]) continue;
        uint32_t stub[2] = { 0xef000000u | i, 0xe12fff1eu };
        size_t stubSize = homebrewLayout ?
            sizeof(stub[0]) : sizeof(stub);
        if (!busWrite(runtime, runtime->package->import_data[i]->offset,
                stub, stubSize)) return false;
    }
    runtime->instructionCache.resize((runtime->package->prog_size + 3u) / 4u);
    runtime->bus.instructionCache = runtime->instructionCache.data();
    runtime->bus.instructionCacheCount =
        (uint32_t)runtime->instructionCache.size();
    if (runtimeCollectsInterpreterProfileSamples())
    {
        runtime->profilePcSamples.resize(runtime->instructionCache.size());
        runtime->profileLrSamples.resize(runtime->instructionCache.size());
        runtime->bus.profilePcSamples = runtime->profilePcSamples.data();
        runtime->bus.profileLrSamples = runtime->profileLrSamples.data();
        runtime->bus.profileSampleCount =
            (uint32_t)runtime->profilePcSamples.size();
    }
    memset(framebufferPixels(), 0, VM_LCD_FB_SIZE);
    memset(runtime->framebuffer.data(), 0, runtime->framebuffer.size());
    return true;
}

bool ccRuntimeRunFile(const char* path,
    const std::vector<std::string>& enabledCheatFeatureKeys,
    CcRuntimeStats* stats)
{
    if (!path || !stats || s_running.exchange(true)) return false;
    framebufferSetTransientPartialProtectionEnabled(true);
    memset(stats, 0, sizeof(*stats));
    printf("cc-arm: timing runtime_scale=%.3f delay_scale=%.3f\n",
        s_runtimeSpeedScale.load(), s_hostDelayScale.load());
    FILE* file = platformOpenGameFile(path);
    if (!file)
    {
        snprintf(stats->error, sizeof(stats->error), "open failed");
        framebufferSetTransientPartialProtectionEnabled(false);
        s_running.store(false);
        return false;
    }

    std::vector<uint8_t> fileData;
    long fileSize = 0;
    if (fseek(file, 0, SEEK_END) == 0)
    {
        fileSize = ftell(file);
        if (fileSize > 0 && fseek(file, 0, SEEK_SET) != 0) fileSize = 0;
    }
    if (fileSize <= 0)
    {
        clearerr(file);
        uint8_t chunk[64 * 1024];
        while (true)
        {
            size_t count = fread(chunk, 1, sizeof(chunk), file);
            if (count) fileData.insert(fileData.end(), chunk, chunk + count);
            if (count < sizeof(chunk)) break;
        }
        fclose(file);
        file = NULL;
        if (fileData.empty() || fileData.size() > UINT32_MAX)
        {
            snprintf(stats->error, sizeof(stats->error), "invalid file size");
            framebufferSetTransientPartialProtectionEnabled(false);
            s_running.store(false);
            return false;
        }
        file = tmpfile();
        if (file && fwrite(fileData.data(), 1, fileData.size(), file) == fileData.size())
        {
            rewind(file);
        }
        else
        {
            if (file) fclose(file);
            file = NULL;
        }
        fileSize = (long)fileData.size();
    }
    if (!file || fileSize <= 0 || (uint64_t)fileSize > UINT32_MAX)
    {
        snprintf(stats->error, sizeof(stats->error), "invalid file size");
        if (file) fclose(file);
        framebufferSetTransientPartialProtectionEnabled(false);
        s_running.store(false);
        return false;
    }
    CcRuntimeContext runtime = {};
    runtime.currentTaskIndex = UINT32_MAX;
    runtime.stats = stats;
    runtime.package = guestPackageCreate(file, (uint32_t)fileSize);
    fclose(file);
    bool retailLayout = runtime.package &&
        ccPackageUsesRetailLayout(runtime.package->origin);
    bool homebrewLayout = runtime.package &&
        ccPackageUsesHomebrewLayout(runtime.package->origin);
    uint32_t ramStart = homebrewLayout ? kCcHomebrewRamStart : kCcRetailRamStart;
    uint32_t ramSize = homebrewLayout ? kCcHomebrewRamSize : kCcRetailRamSize;
    if ((!retailLayout && !homebrewLayout) ||
        (uint64_t)runtime.package->origin + runtime.package->prog_size >
            (uint64_t)ramStart + ramSize ||
        !initializeRuntime(&runtime, path))
    {
        snprintf(stats->error, sizeof(stats->error), "unsupported ARM CCDL image");
        if (runtime.package) guestPackageDestroy(runtime.package);
        framebufferSetTransientPartialProtectionEnabled(false);
        s_running.store(false); return false;
    }
    runtime.startTime = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(s_runtimeMutex);
        s_activeRuntime = &runtime;
    }
    fsys_reset_guest_package(runtime.package);
    std::string gameSha256 = sha256Hex(runtime.package->file_data,
        runtime.package->file_size);
    runtime.gamePath = path;
    runtime.gameSha256 = gameSha256;
    runtimeResourceMonitorSetAppSha256(gameSha256.c_str());
    runtimeResourceMonitorSetGuestResources(runtime.package);
    fsys_set_game_identity(gameSha256.c_str());
    std::string gameName = gameFileNameFromPath(path);
    fsys_set_game_name(gameName.c_str());
    std::string saveDirectory = platformGetCcSaveDirectory(path, gameSha256);
    fsys_set_save_directory(saveDirectory.c_str());
    printf("cc-arm: save directory: %s\n", saveDirectory.c_str());
    cheatRuntimeLoadForGame(gameSha256.c_str(), path, enabledCheatFeatureKeys);
    cheatRuntimeBindMemory(&runtime, cheatReadCallback, cheatWriteCallback,
        cheatFlushCallback);
    uint32_t startupCheatApplyCount = cheatRuntimeApplyStartupBound();
    runtime.cheatCodeCacheFlushPending = false;
    CheatRuntimeStatus cheatStatus = cheatRuntimeGetStatus();
    printf("cc-arm: game settings cheats_enabled=%u cheats_available=%u "
        "cheat_entries=%u cheat_startup_applied=%u app_sha256=%s\n",
        cheatStatus.enabled ? 1u : 0u, cheatStatus.available ? 1u : 0u,
        (unsigned int)cheatStatus.entries.size(), startupCheatApplyCount,
        gameSha256.c_str());
    runtime.instructionPacingStartTime = std::chrono::steady_clock::now();
    runtime.instructionPacingStartInstructions = runtime.stats->instructions;

    Arm32State boot = {};
    arm32Reset(&boot, runtime.package->bin_entry, kExitAddress - 16u, kExitAddress);
    Arm32RunResult result;
    do
    {
        result = runState(&runtime, &boot);
    }
    while (result == ARM32_RUN_LIMIT && !s_stopRequested.load());
    Arm32State crashState = boot;
    uint32_t appMain = findExport(runtime.package, "AppMain");
    CcRuntimeContext::Task mainTask = {};
    mainTask.entry = appMain;
    mainTask.argument = kAppPathWideString;
    mainTask.stack = kExitAddress - 16u;
    if (result != ARM32_RUN_OK || !appMain)
    {
        snprintf(stats->error, sizeof(stats->error), "boot failed result=%u pc=0x%08x", result, boot.r[15]);
    }
    else
    {
        runtime.tasks.push_back(mainTask);
        if (getenv("DINGOO_PIE_FORCE_GUEST_CRASH"))
        {
            CcRuntimeContext::Task& task = runtime.tasks[0];
            arm32Reset(&task.state, task.entry, task.stack, kExitAddress);
            task.state.r[0] = task.argument;
            task.started = true;
            crashState = task.state;
            snprintf(stats->error, sizeof(stats->error),
                "forced guest crash for diagnostics");
            printf("cc-arm: forcing guest crash for diagnostics\n");
        }
        while (!s_stopRequested.load() && stats->error[0] == '\0')
        {
            pauseGateWaitForResume();
            bool anyActive = false;
            bool ranTask = false;
            size_t count = std::min<size_t>(runtime.tasks.size(), 32);
            for (int pass = 0; pass < 2 && !s_stopRequested.load(); ++pass)
            {
                const bool audioPass = pass == 0;
                for (size_t i = 0; i < count && !s_stopRequested.load(); ++i)
                {
                    CcRuntimeContext::Task& task = runtime.tasks[i];
                    if (task.finished || task.audioProducer != audioPass) continue;
                    anyActive = true;
                    if (task.hostDelayUntilMicros)
                    {
                        uint64_t now = currentHostMicros(&runtime);
                        if (now < task.hostDelayUntilMicros)
                        {
                            continue;
                        }
                        task.hostDelayUntilMicros = 0;
                    }
                    if (task.delayTicks)
                    {
                        uint32_t now = currentTaskSchedulerTick(&runtime);
                        if ((int32_t)(now - task.delayTicks) < 0)
                        {
                            continue;
                        }
                        task.delayTicks = 0;
                    }
                    if (!task.started)
                    {
                        arm32Reset(&task.state, task.entry, task.stack, kExitAddress);
                        task.state.r[0] = task.argument;
                        task.started = true;
                    }
                    ranTask = true;
                    runtime.currentTaskIndex = (uint32_t)i;
                    result = runState(&runtime, &task.state,
                        task.audioProducer ? kSliceInstructions :
                        kNonAudioSliceInstructions);
                    runtime.currentTaskIndex = UINT32_MAX;
                    crashState = task.state;
                    task.audioProducer = task.audioProducer ||
                        runtime.tasks[i].audioProducer;
                    if (result == ARM32_RUN_OK) task.finished = true;
                    else if (result == ARM32_RUN_STOPPED && runtime.yielded)
                    {
                        if (runtime.currentHostDelayMicros)
                        {
                            task.hostDelayUntilMicros = currentHostMicros(&runtime) +
                                runtime.currentHostDelayMicros;
                            task.delayTicks = 0;
                        }
                        else
                        {
                            uint32_t delayTicks = ccScaleDelayTicks(
                                runtime.currentDelayTicks, s_hostDelayScale.load());
                            task.delayTicks = currentTaskSchedulerTick(&runtime) + delayTicks;
                            task.hostDelayUntilMicros = 0;
                        }
                    }
                    else if (result != ARM32_RUN_LIMIT)
                    {
                        stats->faultAddress = runtime.faultAddress;
                        stats->faultSize = runtime.faultSize;
                        stats->faultWrite = runtime.faultWrite;
                        stats->faultFetch = runtime.faultFetch;
                        stats->unsupportedPc = task.state.unsupportedPc;
                        stats->failedTaskIndex = (uint32_t)i;
                        stats->failedTaskEntry = task.entry;
                        stats->failedTaskStack = task.stack;
                        stats->failedTaskPriority = task.priority;
                        stats->failedTaskDelayTicks = task.delayTicks;
                        snprintf(stats->error, sizeof(stats->error),
                            "task failed result=%u pc=0x%08x insn=0x%08x sp=0x%08x lr=0x%08x "
                            "r4=0x%08x r5=0x%08x fault=%c%c0x%08x/%u import=%s",
                            result, task.state.r[15], task.state.unsupportedInstruction,
                            task.state.r[13], task.state.r[14], task.state.r[4], task.state.r[5],
                            runtime.faultFetch ? 'F' : '-', runtime.faultWrite ? 'W' : 'R',
                            runtime.faultAddress, runtime.faultSize, stats->lastImport);
                        break;
                    }
                }
                if (stats->error[0] != '\0') break;
            }
            if (!anyActive)
            {
                stats->guestCompleted = true;
                break;
            }
            if (!ranTask)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            profileCcRuntime(&runtime);
        }
    }

    bool ok = stats->error[0] == '\0';
    if (!ok)
    {
        CcCrashLogContext crashContext = {};
        crashContext.gamePath = path;
        crashContext.gameSha256 = gameSha256.c_str();
        crashContext.saveDirectory = saveDirectory.c_str();
        crashContext.error = stats->error;
        crashContext.backend = s_useOptimizedBackend.load() ? "dynarmic" : "arm32_interpreter";
        crashContext.registers = crashState.r;
        crashContext.cpsr = crashState.cpsr;
        crashContext.unsupportedInstruction = crashState.unsupportedInstruction;
        crashContext.unsupportedPc = stats->unsupportedPc;
        crashContext.faultAddress = stats->faultAddress;
        crashContext.faultSize = stats->faultSize;
        crashContext.faultWrite = stats->faultWrite;
        crashContext.faultFetch = stats->faultFetch;
        crashContext.lastImportPc = stats->lastImportPc;
        crashContext.lastImportReturnAddress = stats->lastImportReturnAddress;
        crashContext.failedTaskIndex = stats->failedTaskIndex;
        crashContext.failedTaskEntry = stats->failedTaskEntry;
        crashContext.failedTaskStack = stats->failedTaskStack;
        crashContext.failedTaskPriority = stats->failedTaskPriority;
        crashContext.failedTaskDelayTicks = stats->failedTaskDelayTicks;
        crashContext.instructions = stats->instructions;
        crashContext.importCalls = stats->importCalls;
        crashContext.unknownImports = stats->unknownImports;
        crashContext.framesSubmitted = stats->framesSubmitted;
        crashContext.tasksCreated = stats->tasksCreated;
        crashContext.lastImport = stats->lastImport;
        std::string crashLogFileName;
        if (crashLogWriteCcFailure(crashContext, &crashLogFileName))
        {
            printf("crash-log:wrote file=%s reason=cc-runtime-failure\n",
                crashLogFileName.c_str());
        }
        else
        {
            printf("crash-log:failed reason=cc-runtime-failure\n");
        }
    }
    for (size_t i = 0; i < runtime.openStreams.size(); ++i)
    {
        fsys_fclose(runtime.openStreams[i]);
    }
    cheatRuntimeUnbindMemory(&runtime);
    fsys_reset_guest_package(NULL);
    fsys_set_save_directory("");
    fsys_set_game_identity("");
    fsys_set_game_name("");
    guestPackageDestroy(runtime.package);
#if defined(DINGOO_PIE_ARM32_DYNARMIC)
    arm32DynarmicReset();
#endif
    {
        std::lock_guard<std::mutex> lock(s_runtimeMutex);
        s_activeRuntime = NULL;
    }
    framebufferSetTransientPartialProtectionEnabled(false);
    runtimeResourceMonitorSetActive(false);
    s_running.store(false);
    printf("cc-arm: stopped ok=%u instructions=%llu imports=%u unknown=%u frames=%u tasks=%u last=%s error=%s\n",
        ok ? 1u : 0u, (unsigned long long)stats->instructions, stats->importCalls,
        stats->unknownImports, stats->framesSubmitted, stats->tasksCreated,
        stats->lastImport[0] ? stats->lastImport : "(none)",
        stats->error[0] ? stats->error : "(none)");
    return ok;
}

void ccRuntimeRequestStop(void)
{
    s_stopRequested.store(true);
}

void ccRuntimeApplySettings(void)
{
    double runtimeScale = kAutoRuntimeSpeedScale;
    double delayScale = 1.0;
    double envScale = 1.0;
    if (parsePositiveScaleEnv("DINGOO_PIE_RUNTIME_SPEED_SCALE", &envScale))
    {
        runtimeScale = envScale;
    }
    if (runtimeScale > 1.0)
    {
        runtimeScale = 1.0;
    }
    if (runtimeScale < 0.10)
    {
        runtimeScale = 0.10;
    }
    if (parsePositiveScaleEnv("DINGOO_PIE_OSTIMEDLY_SCALE", &envScale))
    {
        delayScale = envScale;
    }
    s_runtimeSpeedScale.store(runtimeScale);
    s_hostDelayScale.store(delayScale);
    uint64_t targetIps = 0;
    const char* cpuClock = getenv("DINGOO_PIE_IRJIT_CLOCK_HZ");
    if (cpuClock && cpuClock[0])
    {
        char* end = NULL;
        unsigned long long parsed = strtoull(cpuClock, &end, 10);
        if (end != cpuClock && !*end && parsed > 0)
        {
            targetIps = ccCpuClockToTargetIps(parsed, kReferenceCpuClockHz,
                kReferenceInterpreterIps);
        }
    }
    s_targetInstructionsPerSecond.store(targetIps);
    const char* requestedBackend = getenv("DINGOO_PIE_BACKEND");
    RuntimeExecutionMode executionMode = runtimeExecutionModeFromName(requestedBackend, NULL);
    bool optimizedBackendAvailable = false;
#if defined(DINGOO_PIE_ARM32_DYNARMIC)
    optimizedBackendAvailable = true;
#endif
    bool useOptimizedBackend = optimizedBackendAvailable &&
        runtimeExecutionModeUsesOptimizedBackend(executionMode) &&
        !runtimeCollectsInterpreterProfileSamples();
    s_useOptimizedBackend.store(useOptimizedBackend);
    const char* effectiveBackend = useOptimizedBackend ?
        "dynarmic" : "arm32_interpreter";
    const char* executionPath =
        runtimeExecutionModeUsesOptimizedBackend(executionMode) ?
            "optimized" : "base";
    printf("cc-arm: settings requested_backend=%s effective_backend=%s "
        "execution_mode=%s cpu_clock=%s target_ips=%llu runtime_scale=%.3f "
        "delay_scale=%.3f profile=%u\n",
        runtimeExecutionModeName(executionMode), effectiveBackend, executionPath,
        cpuClock && cpuClock[0] ? cpuClock : "auto",
        (unsigned long long)targetIps, runtimeScale, delayScale,
        runtimeLogProfileEnabled() ? 1u : 0u);
    if (executionMode == RUNTIME_EXECUTION_MODE_COMPATIBILITY)
    {
        printf("cc-arm: compatibility mode uses base ARM32 execution paths\n");
    }
}

void ccRuntimePrepareRun(void)
{
#if defined(DINGOO_PIE_ARM32_DYNARMIC)
    arm32DynarmicReset();
#endif
    ccRuntimeApplySettings();
    s_stopRequested.store(false);
}

bool ccRuntimeIsRunning(void)
{
    return s_running.load();
}

bool ccRuntimeReadMemory(uint32_t address, void* out, size_t size)
{
    if (!out || size == 0)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    return s_activeRuntime && busRead(s_activeRuntime, address, out, size);
}

bool ccRuntimeWriteMemory(uint32_t address, const void* in, size_t size)
{
    if (!in || size == 0)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    return s_activeRuntime && busWrite(s_activeRuntime, address, in, size);
}

static bool emitCcMemoryRegion(CcRuntimeMemoryRegionCallback callback,
    void* userData, uint32_t start, size_t size)
{
    return size == 0 || (size <= UINT32_MAX &&
        callback(start, (uint32_t)size, userData));
}

bool ccRuntimeForEachReadableRegion(
    CcRuntimeMemoryRegionCallback callback, void* userData)
{
    if (!callback)
    {
        return false;
    }
    struct Region
    {
        uint32_t start;
        uint32_t size;
    };
    std::vector<Region> regions;
    {
        std::lock_guard<std::mutex> lock(s_runtimeMutex);
        CcRuntimeContext* runtime = s_activeRuntime;
        if (!runtime)
        {
            return false;
        }
        const struct Region source[] = {
            { 0, (uint32_t)runtime->legacyLowMemory.size() },
            { kLegacyMmioStart, (uint32_t)runtime->legacyMmio.size() },
            { kLegacyAudioMmioStart, (uint32_t)runtime->legacyAudioMmio.size() },
            { kLegacySystemMmioStart, (uint32_t)runtime->legacySystemMmio.size() },
            { kCcHomebrewSystemRamStart, (uint32_t)runtime->systemMemory.size() },
            { runtime->ramStart, (uint32_t)runtime->ram.size() },
            { kStackStart, (uint32_t)runtime->stack.size() },
            { runtime->heapStart, (uint32_t)runtime->heapMemory.size() },
            { kFramebufferAddress, (uint32_t)runtime->framebuffer.size() }
        };
        regions.assign(source, source + sizeof(source) / sizeof(source[0]));
    }
    for (size_t i = 0; i < regions.size(); ++i)
    {
        if (!emitCcMemoryRegion(callback, userData,
                regions[i].start, regions[i].size))
        {
            return false;
        }
    }
    return true;
}

bool ccRuntimeGetRegisterSnapshot(CcRuntimeRegisterSnapshot* out)
{
    if (!out)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    CcRuntimeContext* runtime = s_activeRuntime;
    if (!runtime || runtime->currentTaskIndex >= runtime->tasks.size())
    {
        return false;
    }
    const Arm32State& state = runtime->tasks[runtime->currentTaskIndex].state;
    memset(out, 0, sizeof(*out));
    out->running = true;
    memcpy(out->r, state.r, sizeof(out->r));
    out->cpsr = state.cpsr;
    out->currentTaskIndex = runtime->currentTaskIndex;
    return true;
}

bool ccRuntimeGetGameInfo(CcRuntimeGameInfo* out)
{
    if (!out)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    CcRuntimeContext* runtime = s_activeRuntime;
    if (!runtime || !runtime->package)
    {
        return false;
    }
    *out = CcRuntimeGameInfo();
    out->running = true;
    out->path = runtime->gamePath;
    out->sha256 = runtime->gameSha256;
    out->fileSize = runtime->package->file_size;
    out->origin = runtime->package->origin;
    out->programSize = runtime->package->prog_size;
    out->importCount = runtime->package->import_count;
    out->exportCount = runtime->package->export_count;
    out->resourceCount = runtime->package->resource_count;
    out->taskCount = (uint32_t)runtime->tasks.size();
    return true;
}

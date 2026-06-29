#include "framebuffer.h"
#include "cheat_runtime.h"
#include "pause_gate.h"
#include "runtime_debug.h"

#include <atomic>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

uint32_t VM_LCD_FB_ADDRESS = 0x94000000;
static const uint32_t kLcdFramebufferAliases[] =
{
    0x94000000u,
    0x14000000u,
    0x90000000u,
    0x10000000u
};
uint8_t s_LcdFrameBufferPtr[VM_LCD_FB_SIZE] = { 0 };
// The guest writes directly to s_LcdFrameBufferPtr. The frontend only reads
// submitted snapshots so it never presents a frame while the guest is halfway
// through a large blit or tile update.
static uint8_t s_presentedFrameBuffers[2][VM_LCD_FB_SIZE] = { 0 };
static std::atomic<int> s_presentedFrameIndex(0);
static std::mutex s_presentedFrameMutex;
static std::atomic<int> s_FbUpdateRequested(1);
static std::atomic<uint64_t> s_FbWriteCount(0);
static std::atomic<uint64_t> s_FbWriteBytes(0);
static std::atomic<uint32_t> s_SubmittedFrameCount(0);
static std::atomic<uint64_t> s_SubmittedFrameProfileCount(0);
static std::atomic<uint64_t> s_FramebufferCopyMicros(0);
static std::atomic<uint64_t> s_LastSubmittedFrameMicros(0);
static std::atomic<uint64_t> s_FrameIntervalMicros(0);
static std::atomic<uint64_t> s_MaxFrameIntervalMicros(0);
static std::atomic<uint64_t> s_FrameIntervalsOver25ms(0);
static std::atomic<uint64_t> s_FrameIntervalsOver33ms(0);
static std::atomic<bool> s_FramebufferProfileEnabled(false);
static std::mutex s_FramePacingMutex;
static uint64_t s_FramePacingLastMicros = 0;
static uint32_t s_FramePacingFastStreak = 0;

static uint64_t framebufferNowMicros(void)
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

static bool framebufferWriteProfileEnabled(void)
{
    return s_FramebufferProfileEnabled.load();
}

static bool framebufferEnvEnabled(const char* name, bool defaultEnabled)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return defaultEnabled;
    }
    return strcmp(value, "0") != 0 &&
        _stricmp(value, "false") != 0 &&
        _stricmp(value, "off") != 0 &&
        _stricmp(value, "no") != 0;
}

static uint64_t framebufferFramePaceIntervalMicros(void)
{
    static int initialized = 0;
    static uint64_t intervalMicros = 1000000ull / 60ull;
    if (!initialized)
    {
        if (!framebufferEnvEnabled("DINGOO_PIE_LCD_FRAME_PACING", true))
        {
            intervalMicros = 0;
        }
        else
        {
            const char* value = getenv("DINGOO_PIE_DISPLAY_FPS");
            if (value && value[0])
            {
                char* end = NULL;
                unsigned long fps = strtoul(value, &end, 10);
                if (end != value && fps >= 1 && fps <= 240)
                {
                    intervalMicros = 1000000ull / fps;
                }
            }
        }
        initialized = 1;
    }
    return intervalMicros;
}

static uint64_t waitUntilFramebufferMicros(uint64_t targetMicros)
{
    uint64_t nowMicros = framebufferNowMicros();
    while (nowMicros < targetMicros)
    {
        uint64_t remaining = targetMicros - nowMicros;
        if (remaining > 2000)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(remaining - 1000));
        }
        else if (remaining > 500)
        {
#ifdef _WIN32
            Sleep(0);
#else
            std::this_thread::yield();
#endif
        }
        else
        {
            std::this_thread::yield();
        }
        nowMicros = framebufferNowMicros();
    }
    return nowMicros;
}

static uint64_t paceFramebufferSubmission(void)
{
    uint64_t intervalMicros = framebufferFramePaceIntervalMicros();
    uint64_t nowMicros = framebufferNowMicros();
    if (!intervalMicros)
    {
        return nowMicros;
    }

    std::lock_guard<std::mutex> lock(s_FramePacingMutex);
    if (!s_FramePacingLastMicros)
    {
        s_FramePacingLastMicros = nowMicros;
        s_FramePacingFastStreak = 0;
        return nowMicros;
    }

    uint64_t elapsedMicros = nowMicros >= s_FramePacingLastMicros ?
        nowMicros - s_FramePacingLastMicros : 0;
    uint64_t fastThresholdMicros = (intervalMicros * 3ull) / 4ull;
    if (elapsedMicros > 0 && elapsedMicros < fastThresholdMicros)
    {
        if (s_FramePacingFastStreak < UINT32_MAX)
        {
            s_FramePacingFastStreak++;
        }
        if (s_FramePacingFastStreak >= 3)
        {
            uint64_t nextMicros = s_FramePacingLastMicros + intervalMicros;
            if (nowMicros < nextMicros)
            {
                nowMicros = waitUntilFramebufferMicros(nextMicros);
            }
        }
    }
    else
    {
        s_FramePacingFastStreak = 0;
    }
    s_FramePacingLastMicros = nowMicros;
    return nowMicros;
}

static void resetFramebufferPacing(void)
{
    std::lock_guard<std::mutex> lock(s_FramePacingMutex);
    s_FramePacingLastMicros = 0;
    s_FramePacingFastStreak = 0;
    s_LastSubmittedFrameMicros.store(0, std::memory_order_release);
}

void framebufferSetProfileEnabled(bool enabled)
{
    s_FramebufferProfileEnabled.store(enabled);
}

static void writeLe16File(FILE* fp, uint16_t value)
{
    fputc((int)(value & 0xff), fp);
    fputc((int)((value >> 8) & 0xff), fp);
}

static void writeLe32File(FILE* fp, uint32_t value)
{
    fputc((int)(value & 0xff), fp);
    fputc((int)((value >> 8) & 0xff), fp);
    fputc((int)((value >> 16) & 0xff), fp);
    fputc((int)((value >> 24) & 0xff), fp);
}

static void maybeDumpPresentedFrame(const uint8_t* pixels, uint32_t frameNumber)
{
    static int initialized = 0;
    static char pattern[512] = {};
    static uint32_t targetFrame = 0;
    static uint32_t startFrame = 0;
    static uint32_t endFrame = 0;
    static uint32_t stepFrame = 1;
    if (!initialized)
    {
        const char* dumpPath = getenv("DINGOO_PIE_DUMP_FRAME_BMP");
        const char* frameText = getenv("DINGOO_PIE_DUMP_FRAME_INDEX");
        const char* startText = getenv("DINGOO_PIE_DUMP_FRAME_START");
        const char* endText = getenv("DINGOO_PIE_DUMP_FRAME_END");
        const char* stepText = getenv("DINGOO_PIE_DUMP_FRAME_STEP");
        if (dumpPath && dumpPath[0])
        {
            snprintf(pattern, sizeof(pattern), "%s", dumpPath);
        }
        targetFrame = frameText && frameText[0] ? (uint32_t)strtoul(frameText, NULL, 10) : 0;
        startFrame = startText && startText[0] ? (uint32_t)strtoul(startText, NULL, 10) : 0;
        endFrame = endText && endText[0] ? (uint32_t)strtoul(endText, NULL, 10) : 0;
        stepFrame = stepText && stepText[0] ? (uint32_t)strtoul(stepText, NULL, 10) : 1;
        if (stepFrame == 0)
        {
            stepFrame = 1;
        }
        initialized = 1;
    }

    if (!pattern[0])
    {
        return;
    }
    if (targetFrame && frameNumber != targetFrame)
    {
        return;
    }
    if (!targetFrame && startFrame)
    {
        if (frameNumber < startFrame)
        {
            return;
        }
        if (endFrame && frameNumber > endFrame)
        {
            return;
        }
        if (((frameNumber - startFrame) % stepFrame) != 0)
        {
            return;
        }
    }
    else if (!targetFrame && !startFrame)
    {
        return;
    }

    char path[768];
    if (strstr(pattern, "%u") || strstr(pattern, "%d"))
    {
        snprintf(path, sizeof(path), pattern, frameNumber);
    }
    else
    {
        snprintf(path, sizeof(path), "%s", pattern);
    }

    FILE* fp = fopen(path, "wb");
    if (!fp)
    {
        printf("framebuffer: failed to write frame dump: %s\n", path);
        return;
    }

    const uint32_t rowBytes = SCREEN_WIDTH * 2;
    const uint32_t imageBytes = rowBytes * SCREEN_HEIGHT;
    // The framebuffer is RGB565. A 16-bit BI_RGB BMP is ambiguous and many
    // viewers treat it as RGB555, so write explicit channel masks.
    const uint32_t dibHeaderBytes = 40;
    const uint32_t bitfieldBytes = 12;
    const uint32_t pixelOffset = 14 + dibHeaderBytes + bitfieldBytes;
    const uint32_t fileBytes = pixelOffset + imageBytes;
    fwrite("BM", 1, 2, fp);
    writeLe32File(fp, fileBytes);
    writeLe16File(fp, 0);
    writeLe16File(fp, 0);
    writeLe32File(fp, pixelOffset);
    writeLe32File(fp, dibHeaderBytes);
    writeLe32File(fp, SCREEN_WIDTH);
    writeLe32File(fp, SCREEN_HEIGHT);
    writeLe16File(fp, 1);
    writeLe16File(fp, 16);
    writeLe32File(fp, 3);
    writeLe32File(fp, imageBytes);
    writeLe32File(fp, 0);
    writeLe32File(fp, 0);
    writeLe32File(fp, 0);
    writeLe32File(fp, 0);
    writeLe32File(fp, 0x0000F800);
    writeLe32File(fp, 0x000007E0);
    writeLe32File(fp, 0x0000001F);

    for (int y = SCREEN_HEIGHT - 1; y >= 0; --y)
    {
        fwrite(pixels + y * rowBytes, 1, rowBytes, fp);
    }
    fclose(fp);
    printf("framebuffer: dumped frame %u to %s\n", frameNumber, path);
}

int InitFb(NativeRuntime* runtime)
{
    for (size_t i = 0; i < sizeof(kLcdFramebufferAliases) / sizeof(kLcdFramebufferAliases[0]); ++i)
    {
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j)
        {
            if (kLcdFramebufferAliases[j] == kLcdFramebufferAliases[i])
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        RuntimeError err = nativeRuntimeMapMemory(runtime, kLcdFramebufferAliases[i],
            sizeof(s_LcdFrameBufferPtr), RUNTIME_PROT_ALL, s_LcdFrameBufferPtr);
        if (err)
        {
            printf("Failed mem map s_LcdFrameBufferPtr alias 0x%08x: %u (%s)\n",
                kLcdFramebufferAliases[i], err, nativeRuntimeErrorString(err));
            return -1;
        }
    }

    memcpy(s_presentedFrameBuffers[0], s_LcdFrameBufferPtr, sizeof(s_presentedFrameBuffers[0]));
    s_presentedFrameIndex.store(0, std::memory_order_release);
    s_SubmittedFrameCount.store(0, std::memory_order_release);
    resetFramebufferPacing();
    return 0;
}

uint32_t _lcd_get_frame(void)
{
    return VM_LCD_FB_ADDRESS;
}

bool framebufferHostPointer(uint32_t addr, void** out)
{
    if (!out)
    {
        return false;
    }
    for (size_t i = 0; i < sizeof(kLcdFramebufferAliases) / sizeof(kLcdFramebufferAliases[0]); ++i)
    {
        uint32_t base = kLcdFramebufferAliases[i];
        if (addr >= base && addr < base + VM_LCD_FB_SIZE)
        {
            *out = (void*)((size_t)addr - (size_t)base + (size_t)s_LcdFrameBufferPtr);
            return true;
        }
    }
    return false;
}

bool framebufferVmPointer(void* ptr, uint32_t* out)
{
    if (!out)
    {
        return false;
    }
    if ((size_t)ptr >= (size_t)s_LcdFrameBufferPtr &&
        (size_t)ptr < (size_t)s_LcdFrameBufferPtr + VM_LCD_FB_SIZE)
    {
        *out = (uint32_t)(((size_t)ptr - (size_t)s_LcdFrameBufferPtr) + VM_LCD_FB_ADDRESS);
        return true;
    }
    return false;
}

void* getFramebuffPtr(void)
{
    return s_LcdFrameBufferPtr;
}

void* getPresentedFramebuffPtr(void)
{
    int index = s_presentedFrameIndex.load(std::memory_order_acquire);
    return s_presentedFrameBuffers[index & 1];
}

void copyPresentedFramebuff(void* dst, uint32_t size)
{
    if (!dst)
    {
        return;
    }
    if (size > VM_LCD_FB_SIZE)
    {
        size = VM_LCD_FB_SIZE;
    }
    std::lock_guard<std::mutex> lock(s_presentedFrameMutex);
    memcpy(dst, getPresentedFramebuffPtr(), size);
}

void requestFbUpdate(void)
{
    // Snapshot on lcd_set_frame/lcd_flip boundaries. This keeps visual pacing
    // tied to the Dingoo SDK frame submission point instead of host refresh.
    // Pausing here freezes guest execution at a complete frame boundary while
    // leaving the frontend event loop responsive for menu commands.
    uint32_t restoreGeneration = pauseGateRestoreGeneration();
    if (pauseGateWaitForResume())
    {
        resetFramebufferPacing();
        if (restoreGeneration != pauseGateRestoreGeneration())
        {
            return;
        }
    }
    cheatRuntimeApplyFrame();
    uint64_t beginMicros = paceFramebufferSubmission();
    uint64_t previousMicros = s_LastSubmittedFrameMicros.exchange(beginMicros, std::memory_order_acq_rel);
    if (previousMicros)
    {
        uint64_t interval = beginMicros - previousMicros;
        s_FrameIntervalMicros.fetch_add(interval, std::memory_order_relaxed);
        uint64_t currentMax = s_MaxFrameIntervalMicros.load(std::memory_order_relaxed);
        while (interval > currentMax &&
            !s_MaxFrameIntervalMicros.compare_exchange_weak(currentMax, interval, std::memory_order_relaxed))
        {
        }
        if (interval > 25000)
        {
            s_FrameIntervalsOver25ms.fetch_add(1, std::memory_order_relaxed);
        }
        if (interval > 33000)
        {
            s_FrameIntervalsOver33ms.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::lock_guard<std::mutex> lock(s_presentedFrameMutex);
    int nextIndex = s_presentedFrameIndex.load(std::memory_order_relaxed) ^ 1;
    memcpy(s_presentedFrameBuffers[nextIndex], s_LcdFrameBufferPtr, sizeof(s_presentedFrameBuffers[nextIndex]));
    uint32_t frameNumber = s_SubmittedFrameCount.fetch_add(1, std::memory_order_acq_rel) + 1;
    s_SubmittedFrameProfileCount.fetch_add(1, std::memory_order_relaxed);
    maybeDumpPresentedFrame(s_presentedFrameBuffers[nextIndex], frameNumber);
    s_presentedFrameIndex.store(nextIndex, std::memory_order_release);
    s_FbUpdateRequested.store(1, std::memory_order_release);
    s_FramebufferCopyMicros.fetch_add(framebufferNowMicros() - beginMicros, std::memory_order_relaxed);
}

int consumeFbUpdateRequest(void)
{
    return s_FbUpdateRequested.exchange(0, std::memory_order_acq_rel);
}

uint64_t consumeFramebufferSubmittedCount(void)
{
    return s_SubmittedFrameProfileCount.exchange(0, std::memory_order_acq_rel);
}

uint64_t consumeFramebufferCopyMicros(void)
{
    return s_FramebufferCopyMicros.exchange(0, std::memory_order_acq_rel);
}

void consumeFramebufferTimingStats(uint64_t* totalIntervalMicros, uint64_t* maxIntervalMicros,
    uint64_t* over25msCount, uint64_t* over33msCount)
{
    if (totalIntervalMicros)
    {
        *totalIntervalMicros = s_FrameIntervalMicros.exchange(0, std::memory_order_acq_rel);
    }
    if (maxIntervalMicros)
    {
        *maxIntervalMicros = s_MaxFrameIntervalMicros.exchange(0, std::memory_order_acq_rel);
    }
    if (over25msCount)
    {
        *over25msCount = s_FrameIntervalsOver25ms.exchange(0, std::memory_order_acq_rel);
    }
    if (over33msCount)
    {
        *over33msCount = s_FrameIntervalsOver33ms.exchange(0, std::memory_order_acq_rel);
    }
}

void trackFramebufferWrite(uint32_t address, uint32_t size)
{
    if (!framebufferWriteProfileEnabled())
    {
        return;
    }

    if (framebufferAddressOverlaps(address, size))
    {
        s_FbWriteCount.fetch_add(1, std::memory_order_relaxed);
        s_FbWriteBytes.fetch_add(size, std::memory_order_relaxed);
    }
}

bool framebufferAddressOverlaps(uint32_t address, uint32_t size)
{
    uint64_t begin = address;
    uint64_t end = begin + size;
    for (size_t i = 0; i < sizeof(kLcdFramebufferAliases) / sizeof(kLcdFramebufferAliases[0]); ++i)
    {
        uint64_t fbBegin = kLcdFramebufferAliases[i];
        uint64_t fbEnd = fbBegin + VM_LCD_FB_SIZE;
        if (begin < fbEnd && end > fbBegin)
        {
            return true;
        }
    }
    return false;
}

uint64_t consumeFramebufferWriteCount(void)
{
    return s_FbWriteCount.exchange(0, std::memory_order_acq_rel);
}

uint64_t consumeFramebufferWriteBytes(void)
{
    return s_FbWriteBytes.exchange(0, std::memory_order_acq_rel);
}


#include "shared/execution/pause_gate.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

static std::mutex s_pauseMutex;
static std::condition_variable s_pauseCondition;
static std::atomic<bool> s_paused(false);
static std::atomic<unsigned int> s_waiterCount(0);
static std::atomic<uint32_t> s_restoreGeneration(0);

void pauseGateSetPaused(bool paused)
{
    bool previous = s_paused.exchange(paused, std::memory_order_acq_rel);
    if (previous == paused)
    {
        return;
    }

    // Entering pause is observed through the atomic flag; only resume has to
    // wake guest threads that are already blocked in pauseGateWaitForResume().
    if (!paused)
    {
        s_pauseCondition.notify_all();
    }
}

bool pauseGateWaitForPaused(uint32_t timeoutMs)
{
    return pauseGateWaitForPausedWaiters(timeoutMs, 1);
}

bool pauseGateWaitForPausedWaiters(uint32_t timeoutMs, uint32_t minimumWaiters)
{
    if (!s_paused.load(std::memory_order_acquire))
    {
        return false;
    }
    if (minimumWaiters == 0)
    {
        return true;
    }
    if (s_waiterCount.load(std::memory_order_acquire) >= minimumWaiters)
    {
        return true;
    }

    std::unique_lock<std::mutex> lock(s_pauseMutex);
    return s_pauseCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs), [minimumWaiters] {
        return s_waiterCount.load(std::memory_order_acquire) >= minimumWaiters ||
            !s_paused.load(std::memory_order_acquire);
    }) && s_waiterCount.load(std::memory_order_acquire) >= minimumWaiters;
}

bool pauseGateWaitForResume(void)
{
    if (!s_paused.load(std::memory_order_acquire))
    {
        return false;
    }

    bool waited = false;
    std::unique_lock<std::mutex> lock(s_pauseMutex);
    while (s_paused.load(std::memory_order_acquire))
    {
        if (!waited)
        {
            s_waiterCount.fetch_add(1, std::memory_order_acq_rel);
            s_pauseCondition.notify_all();
            waited = true;
        }
        s_pauseCondition.wait(lock);
    }
    if (waited)
    {
        s_waiterCount.fetch_sub(1, std::memory_order_acq_rel);
        s_pauseCondition.notify_all();
    }
    return waited;
}

bool pauseGateWaitForNoWaiters(uint32_t timeoutMs)
{
    if (s_waiterCount.load(std::memory_order_acquire) == 0)
    {
        return true;
    }

    std::unique_lock<std::mutex> lock(s_pauseMutex);
    return s_pauseCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs), [] {
        return s_waiterCount.load(std::memory_order_acquire) == 0;
    });
}

uint32_t pauseGateWaiterCount(void)
{
    return s_waiterCount.load(std::memory_order_acquire);
}

void pauseGateMarkRuntimeRestored(void)
{
    s_restoreGeneration.fetch_add(1, std::memory_order_acq_rel);
}

uint32_t pauseGateRestoreGeneration(void)
{
    return s_restoreGeneration.load(std::memory_order_acquire);
}

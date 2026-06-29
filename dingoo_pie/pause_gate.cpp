#include "pause_gate.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

static std::mutex s_PauseGateMutex;
static std::condition_variable s_PauseGateCondition;
static std::atomic<bool> s_PauseGatePaused(false);
static std::atomic<unsigned int> s_PauseGateWaiterCount(0);
static std::atomic<uint32_t> s_PauseGateRestoreGeneration(0);

void pauseGateSetPaused(bool paused)
{
    bool previous = s_PauseGatePaused.exchange(paused, std::memory_order_acq_rel);
    if (previous == paused)
    {
        return;
    }

    // Entering pause is observed through the atomic flag; only resume has to
    // wake guest threads that are already blocked in pauseGateWaitForResume().
    if (!paused)
    {
        s_PauseGateCondition.notify_all();
    }
}

bool pauseGateWaitForPaused(uint32_t timeoutMs)
{
    return pauseGateWaitForPausedWaiters(timeoutMs, 1);
}

bool pauseGateWaitForPausedWaiters(uint32_t timeoutMs, uint32_t minimumWaiters)
{
    if (!s_PauseGatePaused.load(std::memory_order_acquire))
    {
        return false;
    }
    if (minimumWaiters == 0)
    {
        return true;
    }
    if (s_PauseGateWaiterCount.load(std::memory_order_acquire) >= minimumWaiters)
    {
        return true;
    }

    std::unique_lock<std::mutex> lock(s_PauseGateMutex);
    return s_PauseGateCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs), [minimumWaiters] {
        return s_PauseGateWaiterCount.load(std::memory_order_acquire) >= minimumWaiters ||
            !s_PauseGatePaused.load(std::memory_order_acquire);
    }) && s_PauseGateWaiterCount.load(std::memory_order_acquire) >= minimumWaiters;
}

bool pauseGateWaitForResume(void)
{
    if (!s_PauseGatePaused.load(std::memory_order_acquire))
    {
        return false;
    }

    bool waited = false;
    std::unique_lock<std::mutex> lock(s_PauseGateMutex);
    while (s_PauseGatePaused.load(std::memory_order_acquire))
    {
        if (!waited)
        {
            s_PauseGateWaiterCount.fetch_add(1, std::memory_order_acq_rel);
            s_PauseGateCondition.notify_all();
            waited = true;
        }
        s_PauseGateCondition.wait(lock);
    }
    if (waited)
    {
        s_PauseGateWaiterCount.fetch_sub(1, std::memory_order_acq_rel);
    }
    return waited;
}

uint32_t pauseGateWaiterCount(void)
{
    return s_PauseGateWaiterCount.load(std::memory_order_acquire);
}

void pauseGateMarkRuntimeRestored(void)
{
    s_PauseGateRestoreGeneration.fetch_add(1, std::memory_order_acq_rel);
}

uint32_t pauseGateRestoreGeneration(void)
{
    return s_PauseGateRestoreGeneration.load(std::memory_order_acquire);
}

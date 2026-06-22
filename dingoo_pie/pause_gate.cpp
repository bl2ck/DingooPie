#include "pause_gate.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

static std::mutex s_PauseGateMutex;
static std::condition_variable s_PauseGateCondition;
static std::atomic<bool> s_PauseGatePaused(false);

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
        waited = true;
        s_PauseGateCondition.wait(lock);
    }
    return waited;
}

#ifndef DINGOO_PIE_SHARED_EXECUTION_RUNTIME_TICK_CLOCK_H
#define DINGOO_PIE_SHARED_EXECUTION_RUNTIME_TICK_CLOCK_H

#include <atomic>
#include <stdint.h>

class RuntimeTickClock
{
public:
    RuntimeTickClock() : encodedBase_(0) {}

    uint64_t elapsed(uint64_t now)
    {
        uint64_t encoded = encodedBase_.load(std::memory_order_relaxed);
        if (!encoded)
        {
            uint64_t desired = now == UINT64_MAX ? UINT64_MAX : now + 1;
            if (!encodedBase_.compare_exchange_strong(
                    encoded, desired, std::memory_order_relaxed))
            {
                desired = encoded;
            }
            encoded = desired;
        }
        uint64_t base = encoded - 1;
        return now >= base ? now - base : 0;
    }

    void restoreElapsed(uint64_t now, uint64_t elapsed)
    {
        uint64_t base = now > elapsed ? now - elapsed : 1;
        encodedBase_.store(base + 1, std::memory_order_relaxed);
    }

    void reset()
    {
        encodedBase_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> encodedBase_;
};

#endif

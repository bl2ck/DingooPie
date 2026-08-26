#ifndef DINGOO_PIE_SHARED_DIAGNOSTICS_PROFILE_COUNTER_H
#define DINGOO_PIE_SHARED_DIAGNOSTICS_PROFILE_COUNTER_H

#include <atomic>
#include <stdint.h>

class RuntimeProfileCounter
{
public:
    RuntimeProfileCounter() : value_(0) {}

    void increment()
    {
        value_.fetch_add(1, std::memory_order_relaxed);
    }

    void add(uint64_t value)
    {
        value_.fetch_add(value, std::memory_order_relaxed);
    }

    uint64_t take()
    {
        return value_.exchange(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> value_;
};

#endif

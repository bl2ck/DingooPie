#ifndef DINGOO_PIE_SHARED_DIAGNOSTICS_RUNTIME_SHARED_TEXT_H
#define DINGOO_PIE_SHARED_DIAGNOSTICS_RUNTIME_SHARED_TEXT_H

#include <mutex>
#include <stddef.h>
#include <stdio.h>

template <size_t Capacity>
class RuntimeSharedText
{
public:
    RuntimeSharedText()
    {
        value_[0] = 0;
    }

    void set(const char* value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snprintf(value_, sizeof(value_), "%s", value ? value : "");
    }

    void copy(char* output, size_t outputSize) const
    {
        if (!output || !outputSize)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        snprintf(output, outputSize, "%s", value_);
    }

private:
    mutable std::mutex mutex_;
    char value_[Capacity];
};

#endif

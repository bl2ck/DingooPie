#ifndef DINGOO_PIE_SHARED_EXECUTION_DIRECT_MEMORY_REGION_H
#define DINGOO_PIE_SHARED_EXECUTION_DIRECT_MEMORY_REGION_H

#include <stdint.h>

struct DirectMemoryRegion
{
    uint8_t* data;
    uint32_t base;
    uint32_t size;

    bool operator==(const DirectMemoryRegion& other) const
    {
        return data == other.data && base == other.base && size == other.size;
    }

    uint8_t* pointer(uint32_t address) const
    {
        return data + (address - base);
    }

    uint64_t end() const
    {
        return (uint64_t)base + size;
    }
};

#endif

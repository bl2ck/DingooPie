#ifndef DINGOO_PIE_VM_HEAP_SNAPSHOT_H
#define DINGOO_PIE_VM_HEAP_SNAPSHOT_H

#include <stdint.h>

// Save states restore heap bytes and allocator metadata together so later
// guest malloc/free calls continue from the same allocator state.
struct VmHeapSnapshot
{
    bool valid;
    uint32_t beginAddress;
    uint32_t size;
    uint32_t freeNext;
    uint32_t freeLen;
    uint32_t left;
    uint32_t min;
    uint32_t top;
};

#endif

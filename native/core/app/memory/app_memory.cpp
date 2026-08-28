#include "app/memory/app_memory.h"
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include "app_runtime_debug.h"
#include "frontend/video/framebuffer.h"
#include <pthread.h>
static const uint32_t kCpuRegisterBaseAddress = 0xB0000000;
static const uint32_t kCpuRegisterSize = 0x04000000;
static const uint32_t kVmHeapSize = 64 * 1024 * 1024;
static const uint32_t kVmStackSize = 16 * 1024 * 1024;
static const uint32_t kVmStackUpperAddress = 0xA0000000;
static const uint32_t kVmAppBeginAddress = 0x80a00000;
static void* s_appProgramData = 0;
static uint32_t s_appProgramSize = 0;
static uint32_t s_heapBeginAddress = 0;

static uint8_t s_heapMemory[kVmHeapSize] = { 0 };
static uint8_t s_stackMemory[kVmStackSize] = { 0 };
static uint8_t s_registerMemory[kCpuRegisterSize] = { 0 };

struct LegacyHeapFreeBlock {
    size_t next;
    size_t len;
};

static uint32_t s_legacyHeapMinimumFree;
static uint32_t s_legacyHeapHighWaterOffset;
static LegacyHeapFreeBlock s_legacyHeapFreeList;
static void* s_legacyHeapBase;
static uint32_t s_legacyHeapLength;
static void* s_originalLegacyHeapBase;
static uint32_t s_originalLegacyHeapLength;
static void* s_legacyHeapEnd;
static uint32_t s_legacyHeapFreeBytes;
static std::recursive_mutex s_vmHeapMutex;

static size_t alignedLegacyHeapSize(size_t size)
{
    return (size + 7u) & ~size_t(7u);
}

#define MEM_DEBUG

static int mapAliasIfNeeded(NativeRuntime* runtime, uint32_t addr, uint32_t size, void* ptr, const char* name)
{
    uint32_t alias = addr & 0x1fffffff;
    if (alias == addr)
    {
        return 0;
    }

    RuntimeError err = nativeRuntimeMapMemory(runtime, alias, size, RUNTIME_PROT_ALL, ptr);
    if (err)
    {
        printf("memory: failed to map alias %s addr=0x%08x size=0x%08x: %u (%s)\n",
            name, alias, size, err, nativeRuntimeErrorString(err));
        return -1;
    }

    return 0;
}

static void initializeVmHeapAllocator(void* baseAddress, uint32_t len)
{
	std::lock_guard<std::recursive_mutex> lock(s_vmHeapMutex);
	printf("memory: initialize heap base=%p length=0x%08x\n", baseAddress, len);
	s_originalLegacyHeapBase = baseAddress;
	s_originalLegacyHeapLength = len;

	s_legacyHeapBase = (void*)((size_t)((size_t)s_originalLegacyHeapBase + 3) & (~3));
	s_legacyHeapLength = (s_originalLegacyHeapLength - ((size_t)s_legacyHeapBase - (size_t)s_originalLegacyHeapBase)) & (~3);
	s_legacyHeapEnd = (void*)((size_t)s_legacyHeapBase + s_legacyHeapLength);
	s_legacyHeapFreeList.next = 0;
	s_legacyHeapFreeList.len = 0;
	((LegacyHeapFreeBlock*)s_legacyHeapBase)->next = s_legacyHeapLength;
	((LegacyHeapFreeBlock*)s_legacyHeapBase)->len = s_legacyHeapLength;
	s_legacyHeapFreeBytes = s_legacyHeapLength;
#ifdef MEM_DEBUG
	s_legacyHeapMinimumFree = s_legacyHeapLength;
	s_legacyHeapHighWaterOffset = 0;
#endif
}
static void* allocateVmHeapBlock(uint32_t len)
{
    std::lock_guard<std::recursive_mutex> lock(s_vmHeapMutex);
    LegacyHeapFreeBlock* previous, * nextfree, * l;
    void* ret;

    len = (uint32_t)alignedLegacyHeapSize(len);
    if (len >= s_legacyHeapFreeBytes) {
        printf("memory: heap allocation failed length=%08x\n", len);
        goto err;
    }
    if (!len) {
        printf("memory: invalid zero-length heap allocation\n");
        goto err;
    }
    if ((size_t)s_legacyHeapBase + s_legacyHeapFreeList.next > (size_t)s_legacyHeapEnd) {
        printf("memory: heap free list is corrupted\n");
        goto err;
    }
    previous = &s_legacyHeapFreeList;
    nextfree = (LegacyHeapFreeBlock*)((size_t)s_legacyHeapBase + previous->next);
    while ((char*)nextfree < s_legacyHeapEnd) {
        if (nextfree->len == len) {
            previous->next = nextfree->next;
            s_legacyHeapFreeBytes -= len;
#ifdef MEM_DEBUG
            if (s_legacyHeapFreeBytes < s_legacyHeapMinimumFree)
                s_legacyHeapMinimumFree = s_legacyHeapFreeBytes;
            if (s_legacyHeapHighWaterOffset < previous->next)
                s_legacyHeapHighWaterOffset = previous->next;
#endif
            ret = (void*)nextfree;
            goto end;
        }
        if (nextfree->len > len) {
            l = (LegacyHeapFreeBlock*)((char*)nextfree + len);
            l->next = nextfree->next;
            l->len = (size_t)(nextfree->len - len);
            previous->next += len;
            s_legacyHeapFreeBytes -= len;
#ifdef MEM_DEBUG
            if (s_legacyHeapFreeBytes < s_legacyHeapMinimumFree)
                s_legacyHeapMinimumFree = s_legacyHeapFreeBytes;
            if (s_legacyHeapHighWaterOffset < previous->next)
                s_legacyHeapHighWaterOffset = previous->next;
#endif
            ret = (void*)nextfree;
            goto end;
        }
        previous = nextfree;
        nextfree = (LegacyHeapFreeBlock*)((size_t)s_legacyHeapBase + nextfree->next);
    }
    printf("memory: heap allocation failed length=%08x\n", len);
err:
    return 0;
end:
    return ret;
}

static void freeVmHeapBlock(void* p, uint32_t len) {
    std::lock_guard<std::recursive_mutex> lock(s_vmHeapMutex);
    LegacyHeapFreeBlock* free, * n;
    len = (uint32_t)alignedLegacyHeapSize(len);
#ifdef MEM_DEBUG
    if (!len || !p || (char*)p < s_legacyHeapBase || (char*)p >= s_legacyHeapEnd || (char*)p + len > s_legacyHeapEnd || (char*)p + len <= s_legacyHeapBase) {
        printf("memory: invalid heap free pointer=0x%" PRIXPTR
            " length=%u base=0x%" PRIXPTR " end=0x%" PRIXPTR "\n",
            (size_t)p, len, (size_t)s_legacyHeapBase, (size_t)s_legacyHeapEnd);
        return;
    }
#endif
    free = &s_legacyHeapFreeList;
    n = (LegacyHeapFreeBlock*)((size_t)s_legacyHeapBase + free->next);
    while (((char*)n < s_legacyHeapEnd) && ((void*)n < p)) {
        free = n;
        n = (LegacyHeapFreeBlock*)((size_t)s_legacyHeapBase + n->next);
    }
#ifdef MEM_DEBUG
    if (p == (void*)free || p == (void*)n) {
        printf("memory: heap block is already free\n");
        return;
    }
#endif
    if ((free != &s_legacyHeapFreeList) && ((char*)free + free->len == p)) {
        free->len += len;
    }
    else {
        free->next = (size_t)((char*)p - (char*)s_legacyHeapBase);
        free = (LegacyHeapFreeBlock*)p;
        free->next = (size_t)((char*)n - (char*)s_legacyHeapBase);
        free->len = len;
    }
    if (((char*)n < s_legacyHeapEnd) && ((char*)p + len == (char*)n)) {
        free->next = n->next;
        free->len += n->len;
    }
    s_legacyHeapFreeBytes += len;
}

int appMemoryInitialize(NativeRuntime* runtime, GuestPackage* app)
{
	RuntimeError err;

	if (kVmAppBeginAddress != app->origin)
	{
		printf("memory: appMemoryInitialize invalid origin 0x%08x\n", app->origin);
		return -1;
	}

	s_appProgramData = app->bin_data;
	s_appProgramSize = app->bin_size;

	s_heapBeginAddress = ALIGN((app->prog_size + app->origin), 4096);

	memset(s_heapMemory, 0x00, kVmHeapSize);
	initializeVmHeapAllocator(s_heapMemory, kVmHeapSize);

	err = nativeRuntimeMapMemory(runtime, s_heapBeginAddress, kVmHeapSize, RUNTIME_PROT_ALL, s_heapMemory);
	if (err)
	{
		printf("memory: failed to map s_heapMemory: %u (%s)\n", err, nativeRuntimeErrorString(err));
		return -1;
	}
	if (mapAliasIfNeeded(runtime, s_heapBeginAddress, kVmHeapSize, s_heapMemory, "s_heapMemory"))
	{
		return -1;
	}

	memset(s_stackMemory, 0x00, kVmStackSize);
	err = nativeRuntimeMapMemory(runtime, kVmStackUpperAddress - kVmStackSize, kVmStackSize, RUNTIME_PROT_ALL, s_stackMemory);
	if (err)
	{
		printf("memory: failed to map s_stackMemory: %u (%s)\n", err, nativeRuntimeErrorString(err));
		return -1;
	}
	if (mapAliasIfNeeded(runtime, kVmStackUpperAddress - kVmStackSize, kVmStackSize, s_stackMemory, "s_stackMemory"))
	{
		return -1;
	}

	uint32_t value = kVmStackUpperAddress - 0x20u;
	nativeRuntimeWriteRegister(runtime, RUNTIME_REG_SP, &value);

	// Map the emulated CPU register page used by SDK code.
	memset(s_registerMemory, 0x00, kCpuRegisterSize);
	*(uint32_t*)(s_registerMemory + 0x2020) = 0x00000004;
	err = nativeRuntimeMapMemory(runtime, kCpuRegisterBaseAddress, kCpuRegisterSize, RUNTIME_PROT_ALL, s_registerMemory);
	if (err)
	{
		printf("memory: failed to map s_registerMemory: %u (%s)\n", err, nativeRuntimeErrorString(err));
		return -1;
	}
	if (mapAliasIfNeeded(runtime, kCpuRegisterBaseAddress, kCpuRegisterSize, s_registerMemory, "s_registerMemory"))
	{
		return -1;
	}

	return 0;
}

int appMemoryMapTaskRuntime(NativeRuntime* runtime)
{
    RuntimeError err;

    err = nativeRuntimeMapMemory(runtime, s_heapBeginAddress, kVmHeapSize, RUNTIME_PROT_ALL, s_heapMemory);
    if (err)
    {
        printf("memory: failed to map s_heapMemory: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return -1;
    }
    if (mapAliasIfNeeded(runtime, s_heapBeginAddress, kVmHeapSize, s_heapMemory, "s_heapMemory"))
    {
        return -1;
    }

    err = nativeRuntimeMapMemory(runtime, kVmStackUpperAddress - kVmStackSize, kVmStackSize, RUNTIME_PROT_ALL, s_stackMemory);
    if (err)
    {
        printf("memory: failed to map s_stackMemory: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return -1;
    }
    if (mapAliasIfNeeded(runtime, kVmStackUpperAddress - kVmStackSize, kVmStackSize, s_stackMemory, "s_stackMemory"))
    {
        return -1;
    }

    // Reuse the shared emulated CPU register page for guest subtasks.
    err = nativeRuntimeMapMemory(runtime, kCpuRegisterBaseAddress, kCpuRegisterSize, RUNTIME_PROT_ALL, s_registerMemory);
    if (err)
    {
        printf("memory: failed to map s_registerMemory: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return -1;
    }
    if (mapAliasIfNeeded(runtime, kCpuRegisterBaseAddress, kCpuRegisterSize, s_registerMemory, "s_registerMemory"))
    {
        return -1;
    }

    return 0;
}

void* allocateTrackedVmHeapBlock(uint32_t len) {
    std::lock_guard<std::recursive_mutex> lock(s_vmHeapMutex);
    void* p = NULL;
    if (len == 0)
    {
        return NULL;
    }
    if (len > UINT32_MAX - 15)
    {
        printf("memory: invalid tracked heap allocation length=0x%08x\n", len);
        return NULL;
    }

    p = allocateVmHeapBlock(len + 8);
    if (p)
    {
        ((uint32_t*)p)[0] = len;
        void* userPtr = (void*)((uint8_t*)p + 8);
        return userPtr;
    }
    return p;
}

static bool allocationRangeIsAllocatedLocked(uint64_t blockOffset, uint64_t blockLength)
{
    uint64_t blockEnd = blockOffset + blockLength;
    size_t nextOffset = s_legacyHeapFreeList.next;
    size_t remainingNodes = s_legacyHeapLength / 8u + 1u;
    while (nextOffset < s_legacyHeapLength && remainingNodes-- != 0)
    {
        if ((nextOffset & 7u) != 0 ||
            nextOffset > s_legacyHeapLength - sizeof(LegacyHeapFreeBlock))
        {
            return false;
        }

        const LegacyHeapFreeBlock* freeBlock =
            (const LegacyHeapFreeBlock*)((const uint8_t*)s_legacyHeapBase + nextOffset);
        uint64_t freeLength = freeBlock->len;
        uint64_t freeEnd = (uint64_t)nextOffset + freeLength;
        if (freeLength == 0 || freeEnd > s_legacyHeapLength ||
            (blockOffset < freeEnd && nextOffset < blockEnd))
        {
            return false;
        }
        if (freeBlock->next <= nextOffset || freeBlock->next > s_legacyHeapLength)
        {
            return false;
        }
        nextOffset = freeBlock->next;
    }
    return nextOffset == s_legacyHeapLength;
}

static bool trackedAllocationLengthLocked(void* p, uint32_t* outLength)
{
    if (!p || !outLength || !s_legacyHeapBase || !s_legacyHeapEnd)
    {
        return false;
    }

    uintptr_t userAddress = (uintptr_t)p;
    uintptr_t heapBegin = (uintptr_t)s_legacyHeapBase;
    uintptr_t heapEnd = (uintptr_t)s_legacyHeapEnd;
    if (userAddress < heapBegin + 8u || userAddress >= heapEnd)
    {
        return false;
    }

    uint32_t length = *(uint32_t*)(userAddress - 8u);
    if (length == 0 || length > UINT32_MAX - 15)
    {
        return false;
    }
    uint64_t blockOffset = (uint64_t)(userAddress - heapBegin) - 8u;
    uint64_t blockLength = alignedLegacyHeapSize((uint64_t)length + 8u);
    if ((blockOffset & 7u) != 0 || blockOffset > s_legacyHeapLength ||
        blockLength > s_legacyHeapLength - blockOffset ||
        !allocationRangeIsAllocatedLocked(blockOffset, blockLength))
    {
        return false;
    }

    *outLength = length;
    return true;
}

static bool traceAllocEnabled(void)
{
    static const bool enabled = []() {
        const char* value = getenv("DINGOO_PIE_TRACE_ALLOC");
        return value && value[0] && strcmp(value, "0") != 0;
    }();
    return enabled;
}

void freeTrackedVmHeapBlock(void* p)
{
    if (!p)
    {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(s_vmHeapMutex);
    uint32_t length = 0;
    if (trackedAllocationLengthLocked(p, &length))
    {
        freeVmHeapBlock((uint8_t*)p - 8, length + 8);
    }
}

void* reallocateTrackedVmHeapBlock(void* p, uint32_t newLen) {
    if (p == NULL) {
        return allocateTrackedVmHeapBlock(newLen);
    }
    else if (newLen == 0) {
        freeTrackedVmHeapBlock(p);
        return NULL;
    }
    else
    {
        std::lock_guard<std::recursive_mutex> lock(s_vmHeapMutex);
        uint32_t oldlen = 0;
        if (!trackedAllocationLengthLocked(p, &oldlen))
        {
            return NULL;
        }
        size_t minsize = (oldlen < newLen) ? oldlen : newLen;
        void* newblock = allocateTrackedVmHeapBlock(newLen);
        if (newblock == NULL)
        {
            return newblock;
        }
        memmove(newblock, p, minsize);
        freeTrackedVmHeapBlock(p);
        return newblock;
    }
}

uint32_t vm_malloc(uint32_t len)
{
    void* p = allocateTrackedVmHeapBlock(len);
    if (!p)
    {
        return 0;
    }
    uint32_t ret =  (uint32_t)(((size_t)p - (size_t)s_heapMemory) + s_heapBeginAddress);
    if (traceAllocEnabled())
    {
        printf("trace-alloc: malloc len=%u -> 0x%08x\n", len, ret);
    }
    return ret;
}

void vm_free(uint32_t addr)
{
    if (addr == 0)
    {
        return;
    }
    void* p = toHostPtrRange(addr, 1);
    freeTrackedVmHeapBlock(p);
}

uint32_t vm_realloc(uint32_t addr, uint32_t len)
{
    if (addr == 0)
    {
        return vm_malloc(len);
    }
    if (len == 0)
    {
        vm_free(addr);
        return 0;
    }

    void* p = toHostPtrRange(addr, 1);
    if (!p)
    {
        return 0;
    }
    void* retPtr = reallocateTrackedVmHeapBlock(p, len);
    if (!retPtr)
    {
        return 0;
    }
    uint32_t ret = (uint32_t)(((size_t)retPtr - (size_t)s_heapMemory) + s_heapBeginAddress);
    if (traceAllocEnabled())
    {
        printf("trace-alloc: realloc addr=0x%08x len=%u -> 0x%08x\n", addr, len, ret);
    }
    return ret;
}

bool vmHeapCaptureSnapshot(VmHeapSnapshot* out)
{
    if (!out)
    {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(s_vmHeapMutex);
    memset(out, 0, sizeof(*out));
    if (!s_legacyHeapBase || !s_legacyHeapEnd || s_legacyHeapLength == 0)
    {
        return false;
    }

    out->valid = true;
    out->beginAddress = s_heapBeginAddress;
    out->size = s_legacyHeapLength;
    out->freeNext = (uint32_t)s_legacyHeapFreeList.next;
    out->freeLen = (uint32_t)s_legacyHeapFreeList.len;
    out->left = s_legacyHeapFreeBytes;
    out->min = s_legacyHeapMinimumFree;
    out->top = s_legacyHeapHighWaterOffset;
    return true;
}

bool vmHeapRestoreSnapshot(const VmHeapSnapshot& snapshot)
{
    if (!snapshot.valid)
    {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(s_vmHeapMutex);
    if (!s_legacyHeapBase || !s_legacyHeapEnd || s_legacyHeapLength == 0 ||
        snapshot.beginAddress != s_heapBeginAddress ||
        snapshot.size != s_legacyHeapLength ||
        snapshot.freeNext > s_legacyHeapLength ||
        snapshot.freeLen > s_legacyHeapLength ||
        snapshot.left > s_legacyHeapLength ||
        snapshot.min > s_legacyHeapLength ||
        snapshot.top > s_legacyHeapLength)
    {
        return false;
    }

    s_legacyHeapFreeList.next = snapshot.freeNext;
    s_legacyHeapFreeList.len = snapshot.freeLen;
    s_legacyHeapFreeBytes = snapshot.left;
    s_legacyHeapMinimumFree = snapshot.min;
    s_legacyHeapHighWaterOffset = snapshot.top;
    return true;
}

// Framebuffer memory is owned by framebuffer.cpp and can also be translated
// through the generic VM pointer helpers below.
extern uint32_t kLcdFramebufferAddress;
extern uint8_t s_framebufferPixels[VM_LCD_FB_SIZE];

static bool guestRangeFits(uint32_t addr, uint32_t size, uint32_t base, uint32_t regionSize)
{
    uint64_t rangeBegin = addr;
    uint64_t rangeEnd = rangeBegin + (size ? size : 1u);
    uint64_t regionBegin = base;
    uint64_t regionEnd = regionBegin + regionSize;
    return rangeBegin >= regionBegin && rangeBegin < regionEnd && rangeEnd <= regionEnd;
}

void* toHostPtrRange(uint32_t addr, uint32_t size)
{
    uint32_t heapAlias = s_heapBeginAddress & 0x1fffffff;
    uint32_t stackBegin = kVmStackUpperAddress - kVmStackSize;
    uint32_t stackAlias = stackBegin & 0x1fffffff;
    uint32_t appAlias = kVmAppBeginAddress & 0x1fffffff;

    // VM heap and its cached alias.
    if (guestRangeFits(addr, size, s_heapBeginAddress, kVmHeapSize))
    {
        void* p = (void*)((size_t)addr - (size_t)s_heapBeginAddress + (size_t)s_heapMemory);
        return p;
    }
    if (heapAlias != s_heapBeginAddress && guestRangeFits(addr, size, heapAlias, kVmHeapSize))
    {
        void* p = (void*)((size_t)addr - (size_t)heapAlias + (size_t)s_heapMemory);
        return p;
    }

    // VM stack and its cached alias.
    if (guestRangeFits(addr, size, stackBegin, kVmStackSize))
    {
        void* p = (void*)((size_t)addr - (size_t)stackBegin + (size_t)s_stackMemory);
        return p;
    }
    if (stackAlias != stackBegin && guestRangeFits(addr, size, stackAlias, kVmStackSize))
    {
        void* p = (void*)((size_t)addr - (size_t)stackAlias + (size_t)s_stackMemory);
        return p;
    }

    // Loaded app image and its cached alias.
    if (guestRangeFits(addr, size, kVmAppBeginAddress, s_appProgramSize))
    {
        void* p = (void*)((size_t)addr - (size_t)kVmAppBeginAddress + (size_t)s_appProgramData);
        return p;
    }
    if (appAlias != kVmAppBeginAddress && guestRangeFits(addr, size, appAlias, s_appProgramSize))
    {
        void* p = (void*)((size_t)addr - (size_t)appAlias + (size_t)s_appProgramData);
        return p;
    }
    // LCD framebuffer region.
    void* framebufferPtr = NULL;
    if (framebufferHostPointer(addr, &framebufferPtr))
    {
        size_t framebufferOffset = (size_t)framebufferPtr - (size_t)s_framebufferPixels;
        uint64_t framebufferEnd = (uint64_t)framebufferOffset + (size ? size : 1u);
        if (framebufferOffset < VM_LCD_FB_SIZE && framebufferEnd <= VM_LCD_FB_SIZE)
        {
            return framebufferPtr;
        }
    }

    printf("memory: failed to translate VM address address=0x%08x size=%u\n", addr, size);
    return NULL;
}

void* toHostPtr(uint32_t addr)
{
    return toHostPtrRange(addr, 1);
}

static uint32_t hostRegionRemaining(void* ptr, void* base, uint32_t size)
{
    size_t pointerValue = (size_t)ptr;
    size_t baseValue = (size_t)base;
    if (!ptr || !base || pointerValue < baseValue || pointerValue >= baseValue + size)
    {
        return 0;
    }
    return size - (uint32_t)(pointerValue - baseValue);
}

uint32_t toHostPtrRemaining(uint32_t addr, void** out)
{
    if (!out)
    {
        return 0;
    }
    *out = toHostPtr(addr);
    if (!*out)
    {
        return 0;
    }

    uint32_t remaining = hostRegionRemaining(*out, s_heapMemory, kVmHeapSize);
    if (!remaining) remaining = hostRegionRemaining(*out, s_stackMemory, kVmStackSize);
    if (!remaining) remaining = hostRegionRemaining(*out, s_appProgramData, s_appProgramSize);
    if (!remaining) remaining = hostRegionRemaining(*out, s_framebufferPixels, VM_LCD_FB_SIZE);
    if (!remaining)
    {
        *out = NULL;
    }
    return remaining;
}

const char* toHostString(uint32_t addr)
{
    void* ptr = NULL;
    uint32_t remaining = toHostPtrRemaining(addr, &ptr);
    if (!remaining || !memchr(ptr, 0, remaining))
    {
        return NULL;
    }
    return (const char*)ptr;
}

uint32_t toVmPtr(void* ptr)
{
    // VM heap.
    if ((size_t)ptr >= (size_t)s_heapMemory && (size_t)ptr < (size_t)s_heapMemory + kVmHeapSize)
    {
        return (uint32_t)(((size_t)ptr - (size_t)s_heapMemory) + s_heapBeginAddress);
    }

    // VM stack.
    if ((size_t)ptr >= (size_t)s_stackMemory && (size_t)ptr < (size_t)s_stackMemory + kVmStackSize)
    {
        return (uint32_t)(((size_t)ptr - (size_t)s_stackMemory) + (kVmStackUpperAddress - kVmStackSize));
    }

    // Loaded app image.
    if ((size_t)ptr >= (size_t)s_appProgramData && (size_t)ptr < (size_t)s_appProgramData + s_appProgramSize)
    {
        return (uint32_t)(((size_t)ptr - (size_t)s_appProgramData) + kVmAppBeginAddress);
    }
    // LCD framebuffer region.
    uint32_t framebufferPtr = 0;
    if (framebufferVmPointer(ptr, &framebufferPtr))
    {
        return framebufferPtr;
    }
    printf("memory: failed to translate host pointer pointer=%p\n", ptr);
    return 0;
}

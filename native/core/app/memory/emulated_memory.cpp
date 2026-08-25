#include "emulated_memory.h"
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include "runtime_debug.h"
#include "framebuffer.h"
#include <pthread.h>
const uint32_t CPU_REGISTER_BASE_ADDR = 0xB0000000;
const uint32_t CPU_REGISTER_SIZE = 0x04000000;
const uint32_t VM_HEAP_SIZE = 64 * 1024 * 1024;
const uint32_t VM_STACK_SIZE = 16 * 1024 * 1024;
const uint32_t VM_STACK_UPPER_ADDRESS = 0xA0000000;
const uint32_t VM_APP_BEGIN_ADDRESS = 0x80a00000;
void* s_App_Prog_Ptr = 0;
uint32_t s_App_Prog_Size = 0;
uint32_t s_Heap_Begin_Address = 0;

uint8_t s_HeapMemPtr[VM_HEAP_SIZE] = { 0 };
uint8_t s_StackMemPtr[VM_STACK_SIZE] = { 0 };
uint8_t s_RegisterMemPtr[CPU_REGISTER_SIZE] = { 0 };

typedef struct {
    size_t next;
    size_t len;
} LG_mem_free_t;

uint32_t LG_mem_min;
uint32_t LG_mem_top;
LG_mem_free_t LG_mem_free;
void* LG_mem_base;
uint32_t LG_mem_len;
void* Origin_LG_mem_base;
uint32_t Origin_LG_mem_len;
void* LG_mem_end;
uint32_t LG_mem_left;
static std::recursive_mutex g_vmHeapMutex;

#define realLGmemSize(x) (((x) + 7) & (0xfffffff8))

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

void initMemoryManager(void* baseAddress, uint32_t len)
{
	std::lock_guard<std::recursive_mutex> lock(g_vmHeapMutex);
	printf("initMemoryManager: baseAddress:0x%" PRIx64 " len: 0x%08x\n", (size_t)baseAddress, len);
	Origin_LG_mem_base = baseAddress;
	Origin_LG_mem_len = len;

	LG_mem_base = (void*)((size_t)((size_t)Origin_LG_mem_base + 3) & (~3));
	LG_mem_len = (Origin_LG_mem_len - ((size_t)LG_mem_base - (size_t)Origin_LG_mem_base)) & (~3);
	LG_mem_end = (void*)((size_t)LG_mem_base + LG_mem_len);
	LG_mem_free.next = 0;
	LG_mem_free.len = 0;
	((LG_mem_free_t*)LG_mem_base)->next = LG_mem_len;
	((LG_mem_free_t*)LG_mem_base)->len = LG_mem_len;
	LG_mem_left = LG_mem_len;
#ifdef MEM_DEBUG
	LG_mem_min = LG_mem_len;
	LG_mem_top = 0;
#endif
}
void printMemoryInfo() {
    printf(".......total:%d, min:%d, free:%d, top:%d\n", LG_mem_len, LG_mem_min, LG_mem_left, LG_mem_top);
    printf(".......base:%p, end:%p\n", LG_mem_base, LG_mem_end);
    printf(".......obase:%p, olen:%d\n", Origin_LG_mem_base, Origin_LG_mem_len);
}

void* my_malloc(uint32_t len)
{
    std::lock_guard<std::recursive_mutex> lock(g_vmHeapMutex);
    LG_mem_free_t* previous, * nextfree, * l;
    void* ret;

    len = (uint32_t)realLGmemSize(len);
    if (len >= LG_mem_left) {
        printf("my_malloc no memory: len %08x\n", len);
        goto err;
    }
    if (!len) {
        printf("my_malloc invalid memory request");
        goto err;
    }
    if ((size_t)LG_mem_base + LG_mem_free.next > (size_t)LG_mem_end) {
        printf("my_malloc corrupted memory");
        goto err;
    }
    previous = &LG_mem_free;
    nextfree = (LG_mem_free_t*)((size_t)LG_mem_base + previous->next);
    while ((char*)nextfree < LG_mem_end) {
        if (nextfree->len == len) {
            previous->next = nextfree->next;
            LG_mem_left -= len;
#ifdef MEM_DEBUG
            if (LG_mem_left < LG_mem_min)
                LG_mem_min = LG_mem_left;
            if (LG_mem_top < previous->next)
                LG_mem_top = previous->next;
#endif
            ret = (void*)nextfree;
            goto end;
        }
        if (nextfree->len > len) {
            l = (LG_mem_free_t*)((char*)nextfree + len);
            l->next = nextfree->next;
            l->len = (size_t)(nextfree->len - len);
            previous->next += len;
            LG_mem_left -= len;
#ifdef MEM_DEBUG
            if (LG_mem_left < LG_mem_min)
                LG_mem_min = LG_mem_left;
            if (LG_mem_top < previous->next)
                LG_mem_top = previous->next;
#endif
            ret = (void*)nextfree;
            goto end;
        }
        previous = nextfree;
        nextfree = (LG_mem_free_t*)((size_t)LG_mem_base + nextfree->next);
    }
    printf("my_malloc no memory: len %08x\n", len);
err:
    return 0;
end:
    return ret;
}

void my_free(void* p, uint32_t len) {
    std::lock_guard<std::recursive_mutex> lock(g_vmHeapMutex);
    LG_mem_free_t* free, * n;
    len = (uint32_t)realLGmemSize(len);
#ifdef MEM_DEBUG
    if (!len || !p || (char*)p < LG_mem_base || (char*)p >= LG_mem_end || (char*)p + len > LG_mem_end || (char*)p + len <= LG_mem_base) {
        printf("my_free invalid\n");
        printf("p=%" PRIXPTR ", l=%d, base=%" PRIXPTR ",LG_mem_end=%" PRIXPTR "\n", (size_t)p, len, (size_t)LG_mem_base, (size_t)LG_mem_end);
        return;
    }
#endif
    free = &LG_mem_free;
    n = (LG_mem_free_t*)((size_t)LG_mem_base + free->next);
    while (((char*)n < LG_mem_end) && ((void*)n < p)) {
        free = n;
        n = (LG_mem_free_t*)((size_t)LG_mem_base + n->next);
    }
#ifdef MEM_DEBUG
    if (p == (void*)free || p == (void*)n) {
        printf("my_free:already free\n");
        return;
    }
#endif
    if ((free != &LG_mem_free) && ((char*)free + free->len == p)) {
        free->len += len;
    }
    else {
        free->next = (size_t)((char*)p - (char*)LG_mem_base);
        free = (LG_mem_free_t*)p;
        free->next = (size_t)((char*)n - (char*)LG_mem_base);
        free->len = len;
    }
    if (((char*)n < LG_mem_end) && ((char*)p + len == (char*)n)) {
        free->next = n->next;
        free->len += n->len;
    }
    LG_mem_left += len;
}

void* my_realloc(void* p, uint32_t oldlen, uint32_t len) {
    std::lock_guard<std::recursive_mutex> lock(g_vmHeapMutex);
    unsigned long minsize = (oldlen > len) ? len : oldlen;
    void* newblock;
    if (p == NULL) {
        return my_malloc(len);
    }
    if (len == 0) {
        my_free(p, oldlen);
        return NULL;
    }
    newblock = my_malloc(len);
    if (newblock == NULL) {
        return newblock;
    }
    memmove(newblock, p, minsize);
    my_free(p, oldlen);
    return newblock;
}

int InitVmMem(NativeRuntime *runtime, app *_app)
{
	RuntimeError err;

	if (VM_APP_BEGIN_ADDRESS != _app->origin)
	{
		printf("memory: InitVmMem invalid origin 0x%08x\n", _app->origin);
		return -1;
	}

	s_App_Prog_Ptr = _app->bin_data;
	s_App_Prog_Size = _app->bin_size;

	s_Heap_Begin_Address = ALIGN((_app->prog_size + _app->origin), 4096);

	memset(s_HeapMemPtr, 0x00, VM_HEAP_SIZE);
	initMemoryManager(s_HeapMemPtr, VM_HEAP_SIZE);

	err = nativeRuntimeMapMemory(runtime, s_Heap_Begin_Address, VM_HEAP_SIZE, RUNTIME_PROT_ALL, s_HeapMemPtr);
	if (err)
	{
		printf("memory: failed to map s_HeapMemPtr: %u (%s)\n", err, nativeRuntimeErrorString(err));
		return -1;
	}
	if (mapAliasIfNeeded(runtime, s_Heap_Begin_Address, VM_HEAP_SIZE, s_HeapMemPtr, "s_HeapMemPtr"))
	{
		return -1;
	}

	memset(s_StackMemPtr, 0x00, VM_STACK_SIZE);
	err = nativeRuntimeMapMemory(runtime, VM_STACK_UPPER_ADDRESS - VM_STACK_SIZE, VM_STACK_SIZE, RUNTIME_PROT_ALL, s_StackMemPtr);
	if (err)
	{
		printf("memory: failed to map s_StackMemPtr: %u (%s)\n", err, nativeRuntimeErrorString(err));
		return -1;
	}
	if (mapAliasIfNeeded(runtime, VM_STACK_UPPER_ADDRESS - VM_STACK_SIZE, VM_STACK_SIZE, s_StackMemPtr, "s_StackMemPtr"))
	{
		return -1;
	}

	uint32_t value = VM_STACK_UPPER_ADDRESS - 0x20u;
	nativeRuntimeWriteRegister(runtime, RUNTIME_REG_SP, &value);

	// Map the emulated CPU register page used by SDK code.
	memset(s_RegisterMemPtr, 0x00, CPU_REGISTER_SIZE);
	*(uint32_t*)(s_RegisterMemPtr + 0x2020) = 0x00000004;
	err = nativeRuntimeMapMemory(runtime, CPU_REGISTER_BASE_ADDR, CPU_REGISTER_SIZE, RUNTIME_PROT_ALL, s_RegisterMemPtr);
	if (err)
	{
		printf("memory: failed to map s_RegisterMemPtr: %u (%s)\n", err, nativeRuntimeErrorString(err));
		return -1;
	}
	if (mapAliasIfNeeded(runtime, CPU_REGISTER_BASE_ADDR, CPU_REGISTER_SIZE, s_RegisterMemPtr, "s_RegisterMemPtr"))
	{
		return -1;
	}

	return 0;
}

int InitVmMemSubTask(NativeRuntime* runtime)
{
    RuntimeError err;

    err = nativeRuntimeMapMemory(runtime, s_Heap_Begin_Address, VM_HEAP_SIZE, RUNTIME_PROT_ALL, s_HeapMemPtr);
    if (err)
    {
        printf("memory: failed to map s_HeapMemPtr: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return -1;
    }
    if (mapAliasIfNeeded(runtime, s_Heap_Begin_Address, VM_HEAP_SIZE, s_HeapMemPtr, "s_HeapMemPtr"))
    {
        return -1;
    }

    err = nativeRuntimeMapMemory(runtime, VM_STACK_UPPER_ADDRESS - VM_STACK_SIZE, VM_STACK_SIZE, RUNTIME_PROT_ALL, s_StackMemPtr);
    if (err)
    {
        printf("memory: failed to map s_StackMemPtr: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return -1;
    }
    if (mapAliasIfNeeded(runtime, VM_STACK_UPPER_ADDRESS - VM_STACK_SIZE, VM_STACK_SIZE, s_StackMemPtr, "s_StackMemPtr"))
    {
        return -1;
    }

    // Reuse the shared emulated CPU register page for guest subtasks.
    err = nativeRuntimeMapMemory(runtime, CPU_REGISTER_BASE_ADDR, CPU_REGISTER_SIZE, RUNTIME_PROT_ALL, s_RegisterMemPtr);
    if (err)
    {
        printf("memory: failed to map s_RegisterMemPtr: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return -1;
    }
    if (mapAliasIfNeeded(runtime, CPU_REGISTER_BASE_ADDR, CPU_REGISTER_SIZE, s_RegisterMemPtr, "s_RegisterMemPtr"))
    {
        return -1;
    }

    return 0;
}

void* my_mallocExt(uint32_t len) {
    std::lock_guard<std::recursive_mutex> lock(g_vmHeapMutex);
    void* p = NULL;
    if (len == 0)
    {
        return NULL;
    }
    if (len > UINT32_MAX - 15)
    {
        printf("my_mallocExt invalid memory request: len %08x\n", len);
        return NULL;
    }

    p = my_malloc(len + 8);
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
    size_t nextOffset = LG_mem_free.next;
    size_t remainingNodes = LG_mem_len / 8u + 1u;
    while (nextOffset < LG_mem_len && remainingNodes-- != 0)
    {
        if ((nextOffset & 7u) != 0 ||
            nextOffset > LG_mem_len - sizeof(LG_mem_free_t))
        {
            return false;
        }

        const LG_mem_free_t* freeBlock =
            (const LG_mem_free_t*)((const uint8_t*)LG_mem_base + nextOffset);
        uint64_t freeLength = freeBlock->len;
        uint64_t freeEnd = (uint64_t)nextOffset + freeLength;
        if (freeLength == 0 || freeEnd > LG_mem_len ||
            (blockOffset < freeEnd && nextOffset < blockEnd))
        {
            return false;
        }
        if (freeBlock->next <= nextOffset || freeBlock->next > LG_mem_len)
        {
            return false;
        }
        nextOffset = freeBlock->next;
    }
    return nextOffset == LG_mem_len;
}

static bool trackedAllocationLengthLocked(void* p, uint32_t* outLength)
{
    if (!p || !outLength || !LG_mem_base || !LG_mem_end)
    {
        return false;
    }

    uintptr_t userAddress = (uintptr_t)p;
    uintptr_t heapBegin = (uintptr_t)LG_mem_base;
    uintptr_t heapEnd = (uintptr_t)LG_mem_end;
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
    uint64_t blockLength = realLGmemSize((uint64_t)length + 8u);
    if ((blockOffset & 7u) != 0 || blockOffset > LG_mem_len ||
        blockLength > LG_mem_len - blockOffset ||
        !allocationRangeIsAllocatedLocked(blockOffset, blockLength))
    {
        return false;
    }

    *outLength = length;
    return true;
}

void my_freeExt(void* p)
{
    if (!p)
    {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(g_vmHeapMutex);
    uint32_t length = 0;
    if (trackedAllocationLengthLocked(p, &length))
    {
        my_free((uint8_t*)p - 8, length + 8);
    }
}

void* my_reallocExt(void* p, uint32_t newLen) {
    if (p == NULL) {
        return my_mallocExt(newLen);
    }
    else if (newLen == 0) {
        my_freeExt(p);
        return NULL;
    }
    else
    {
        std::lock_guard<std::recursive_mutex> lock(g_vmHeapMutex);
        uint32_t oldlen = 0;
        if (!trackedAllocationLengthLocked(p, &oldlen))
        {
            return NULL;
        }
        size_t minsize = (oldlen < newLen) ? oldlen : newLen;
        void* newblock = my_mallocExt(newLen);
        if (newblock == NULL)
        {
            return newblock;
        }
        memmove(newblock, p, minsize);
        my_freeExt(p);
        return newblock;
    }
}

uint32_t vm_malloc(uint32_t len)
{
    void* p = my_mallocExt(len);
    if (!p)
    {
        return 0;
    }
    uint32_t ret =  (uint32_t)(((size_t)p - (size_t)s_HeapMemPtr) + s_Heap_Begin_Address);
    static int traceAllocEnabled = -1;
    if (traceAllocEnabled < 0)
    {
        const char* traceAlloc = getenv("DINGOO_PIE_TRACE_ALLOC");
        traceAllocEnabled = (traceAlloc && traceAlloc[0] && strcmp(traceAlloc, "0") != 0) ? 1 : 0;
    }
    if (traceAllocEnabled != 0)
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
    my_freeExt(p);
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
    void* retPtr = my_reallocExt(p, len);
    if (!retPtr)
    {
        return 0;
    }
    uint32_t ret = (uint32_t)(((size_t)retPtr - (size_t)s_HeapMemPtr) + s_Heap_Begin_Address);
    static int traceAllocEnabled = -1;
    if (traceAllocEnabled < 0)
    {
        const char* traceAlloc = getenv("DINGOO_PIE_TRACE_ALLOC");
        traceAllocEnabled = (traceAlloc && traceAlloc[0] && strcmp(traceAlloc, "0") != 0) ? 1 : 0;
    }
    if (traceAllocEnabled != 0)
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

    std::lock_guard<std::recursive_mutex> lock(g_vmHeapMutex);
    memset(out, 0, sizeof(*out));
    if (!LG_mem_base || !LG_mem_end || LG_mem_len == 0)
    {
        return false;
    }

    out->valid = true;
    out->beginAddress = s_Heap_Begin_Address;
    out->size = LG_mem_len;
    out->freeNext = (uint32_t)LG_mem_free.next;
    out->freeLen = (uint32_t)LG_mem_free.len;
    out->left = LG_mem_left;
    out->min = LG_mem_min;
    out->top = LG_mem_top;
    return true;
}

bool vmHeapRestoreSnapshot(const VmHeapSnapshot& snapshot)
{
    if (!snapshot.valid)
    {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(g_vmHeapMutex);
    if (!LG_mem_base || !LG_mem_end || LG_mem_len == 0 ||
        snapshot.beginAddress != s_Heap_Begin_Address ||
        snapshot.size != LG_mem_len ||
        snapshot.freeNext > LG_mem_len ||
        snapshot.freeLen > LG_mem_len ||
        snapshot.left > LG_mem_len ||
        snapshot.min > LG_mem_len ||
        snapshot.top > LG_mem_len)
    {
        return false;
    }

    LG_mem_free.next = snapshot.freeNext;
    LG_mem_free.len = snapshot.freeLen;
    LG_mem_left = snapshot.left;
    LG_mem_min = snapshot.min;
    LG_mem_top = snapshot.top;
    return true;
}

// Framebuffer memory is owned by framebuffer.cpp and can also be translated
// through the generic VM pointer helpers below.
extern uint32_t VM_LCD_FB_ADDRESS;
extern uint8_t s_LcdFrameBufferPtr[VM_LCD_FB_SIZE];

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
    uint32_t heapAlias = s_Heap_Begin_Address & 0x1fffffff;
    uint32_t stackBegin = VM_STACK_UPPER_ADDRESS - VM_STACK_SIZE;
    uint32_t stackAlias = stackBegin & 0x1fffffff;
    uint32_t appAlias = VM_APP_BEGIN_ADDRESS & 0x1fffffff;

    // VM heap and its cached alias.
    if (guestRangeFits(addr, size, s_Heap_Begin_Address, VM_HEAP_SIZE))
    {
        void* p = (void*)((size_t)addr - (size_t)s_Heap_Begin_Address + (size_t)s_HeapMemPtr);
        return p;
    }
    if (heapAlias != s_Heap_Begin_Address && guestRangeFits(addr, size, heapAlias, VM_HEAP_SIZE))
    {
        void* p = (void*)((size_t)addr - (size_t)heapAlias + (size_t)s_HeapMemPtr);
        return p;
    }

    // VM stack and its cached alias.
    if (guestRangeFits(addr, size, stackBegin, VM_STACK_SIZE))
    {
        void* p = (void*)((size_t)addr - (size_t)stackBegin + (size_t)s_StackMemPtr);
        return p;
    }
    if (stackAlias != stackBegin && guestRangeFits(addr, size, stackAlias, VM_STACK_SIZE))
    {
        void* p = (void*)((size_t)addr - (size_t)stackAlias + (size_t)s_StackMemPtr);
        return p;
    }

    // Loaded app image and its cached alias.
    if (guestRangeFits(addr, size, VM_APP_BEGIN_ADDRESS, s_App_Prog_Size))
    {
        void* p = (void*)((size_t)addr - (size_t)VM_APP_BEGIN_ADDRESS + (size_t)s_App_Prog_Ptr);
        return p;
    }
    if (appAlias != VM_APP_BEGIN_ADDRESS && guestRangeFits(addr, size, appAlias, s_App_Prog_Size))
    {
        void* p = (void*)((size_t)addr - (size_t)appAlias + (size_t)s_App_Prog_Ptr);
        return p;
    }
    // LCD framebuffer region.
    void* framebufferPtr = NULL;
    if (framebufferHostPointer(addr, &framebufferPtr))
    {
        size_t framebufferOffset = (size_t)framebufferPtr - (size_t)s_LcdFrameBufferPtr;
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

    uint32_t remaining = hostRegionRemaining(*out, s_HeapMemPtr, VM_HEAP_SIZE);
    if (!remaining) remaining = hostRegionRemaining(*out, s_StackMemPtr, VM_STACK_SIZE);
    if (!remaining) remaining = hostRegionRemaining(*out, s_App_Prog_Ptr, s_App_Prog_Size);
    if (!remaining) remaining = hostRegionRemaining(*out, s_LcdFrameBufferPtr, VM_LCD_FB_SIZE);
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
    if ((size_t)ptr >= (size_t)s_HeapMemPtr && (size_t)ptr < (size_t)s_HeapMemPtr + VM_HEAP_SIZE)
    {
        return (uint32_t)(((size_t)ptr - (size_t)s_HeapMemPtr) + s_Heap_Begin_Address);
    }

    // VM stack.
    if ((size_t)ptr >= (size_t)s_StackMemPtr && (size_t)ptr < (size_t)s_StackMemPtr + VM_STACK_SIZE)
    {
        return (uint32_t)(((size_t)ptr - (size_t)s_StackMemPtr) + (VM_STACK_UPPER_ADDRESS - VM_STACK_SIZE));
    }

    // Loaded app image.
    if ((size_t)ptr >= (size_t)s_App_Prog_Ptr && (size_t)ptr < (size_t)s_App_Prog_Ptr + s_App_Prog_Size)
    {
        return (uint32_t)(((size_t)ptr - (size_t)s_App_Prog_Ptr) + VM_APP_BEGIN_ADDRESS);
    }
    // LCD framebuffer region.
    uint32_t framebufferPtr = 0;
    if (framebufferVmPointer(ptr, &framebufferPtr))
    {
        return framebufferPtr;
    }
    printf("ERR: toVmPtr 0x%x\n", ptr);
    return 0;
}

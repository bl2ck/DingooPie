#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_STATE_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_STATE_H

#include "app_heap_snapshot.h"

#include <stdint.h>
#include <vector>

struct AppRuntimeRegisterSnapshot
{
    bool running;
    uint32_t gpr[32];
    float fpr[32];
    float vfpu[128];
    uint32_t vfpuCtrl[16];
    uint32_t pc;
    uint32_t hi;
    uint32_t lo;
    uint32_t fcr31;
    uint32_t fpcond;
};

struct AppRuntimeStateRegion
{
    uint32_t start;
    uint32_t size;
    uint32_t perms;
    std::vector<uint8_t> data;
};

struct AppRuntimeState
{
    AppRuntimeRegisterSnapshot registers;
    VmHeapSnapshot heap;
    uint32_t osTicks;
    std::vector<uint32_t> hleSemaphoreCounts;
    std::vector<AppRuntimeRegisterSnapshot> taskRegisters;
    std::vector<AppRuntimeStateRegion> regions;
};

#endif

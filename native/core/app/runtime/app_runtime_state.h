#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_STATE_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_STATE_H

#include "app/memory/app_heap_snapshot.h"
#include "shared/game/game_runtime_types.h"

#include <stdint.h>
#include <vector>

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

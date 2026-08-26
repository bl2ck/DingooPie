#ifndef DINGOO_PIE_SHARED_GAME_GAME_RUNTIME_TYPES_H
#define DINGOO_PIE_SHARED_GAME_GAME_RUNTIME_TYPES_H

#include <stdint.h>

#include <string>

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

struct AppRuntimeInfo
{
    bool running;
    std::string path;
    std::string fileName;
    std::string guestMainPath;
    std::string sha256;
    std::string compatProfile;
    std::string backend;
    uint32_t fileSize;
    uint32_t origin;
    uint32_t binSize;
    uint32_t progSize;
    uint32_t bssSize;
    uint32_t bootEntry;
    uint32_t appMainEntry;
    uint32_t importCount;
    uint32_t exportCount;
    uint32_t resourceCount;
};

struct AppRuntimeDisassemblyLine
{
    uint32_t address;
    uint32_t encoding;
    std::string text;
    bool valid;
};

struct AppRuntimeMemorySearchCandidate
{
    uint32_t address;
    uint32_t previous;
};

enum AppRuntimeMemorySearchFilter
{
    APP_RUNTIME_MEMORY_SEARCH_EQUAL,
    APP_RUNTIME_MEMORY_SEARCH_INCREASED,
    APP_RUNTIME_MEMORY_SEARCH_DECREASED,
    APP_RUNTIME_MEMORY_SEARCH_UNCHANGED
};

#endif

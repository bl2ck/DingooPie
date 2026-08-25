#ifndef DINGOO_PIE_CC_SAVE_CC_SAVE_STATE_H
#define DINGOO_PIE_CC_SAVE_CC_SAVE_STATE_H

#include "cc/cpu/arm32_interpreter.h"
#include "shared/save/save_slots.h"

#include <stdint.h>
#include <string>
#include <vector>

struct CcSaveHeapBlock
{
    uint32_t address;
    uint32_t size;
    bool free;
};

struct CcSaveTask
{
    Arm32State state;
    uint32_t entry;
    uint32_t argument;
    uint32_t stack;
    uint32_t priority;
    uint32_t delayTicks;
    bool started;
    bool finished;
    bool audioProducer;
};

struct CcSaveResource
{
    uint32_t address;
    std::string name;
    uint32_t position;
    uint32_t dataAddress;
};

struct CcSaveFileStream
{
    uint32_t stream;
    std::string requestName;
    uint32_t position;
};

struct CcSaveFileHandle
{
    uint32_t address;
    uint32_t stream;
};

struct CcSaveSemaphore
{
    uint32_t address;
    uint32_t count;
};

struct CcRuntimeState
{
    std::string gameSha256;
    std::vector<uint8_t> ram;
    std::vector<uint8_t> systemMemory;
    std::vector<uint8_t> stack;
    std::vector<uint8_t> heapMemory;
    std::vector<uint8_t> legacyLowMemory;
    std::vector<uint8_t> framebuffer;
    std::vector<uint8_t> legacyMmio;
    std::vector<uint8_t> legacyAudioMmio;
    std::vector<uint8_t> legacySystemMmio;
    std::vector<CcSaveHeapBlock> heap;
    std::vector<CcSaveTask> tasks;
    std::vector<CcSaveResource> resources;
    std::vector<CcSaveFileStream> streams;
    std::vector<CcSaveFileHandle> files;
    std::vector<CcSaveSemaphore> semaphores;
    std::vector<std::string> dynamicImports;
    std::vector<std::string> unknownImportNames;
    uint64_t elapsedGuestMicros;
    uint64_t runtimeInstructions;
    uint32_t heapStart;
    uint32_t heapCursor;
    uint32_t dvcAudioHandle;
    uint32_t dvcAudioSampleRate;
    uint32_t dvcAudioVolume;
    uint32_t framebufferAddress;
    uint32_t framebufferBits;
    uint32_t framebufferWriteHighWater[4];
    bool framebufferBitsExplicit;
    bool dvcAudioStarted;
};

bool saveStateWriteCcSlot(const std::string& appPath, int slot,
    const CcRuntimeState& state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateReadCcSlot(const std::string& appPath, int slot,
    CcRuntimeState* state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);

#endif

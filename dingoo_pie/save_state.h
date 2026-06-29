#ifndef DINGOO_PIE_SAVE_STATE_H
#define DINGOO_PIE_SAVE_STATE_H

#include "emulator_core.h"

#include <string>

static const int kSaveStateSlotCount = 10;

enum SaveStateProgressPhase
{
    SAVE_STATE_PROGRESS_COMPRESS,
    SAVE_STATE_PROGRESS_DECOMPRESS
};

struct SaveStateProgress
{
    SaveStateProgressPhase phase;
    uint32_t percent;
};

typedef void (*SaveStateProgressCallback)(const SaveStateProgress& progress, void* userData);

struct SaveStateSlotInfo
{
    bool exists;
    std::string path;
    uint64_t modifiedTime;
    bool runtimeCountValid;
    // Includes the main runtime plus all captured task runtimes.
    uint32_t runtimeCount;
};

std::string saveStateAppIdForPath(const std::string& appPath);
std::string saveStatePathForSlot(const std::string& appPath, int slot);
SaveStateSlotInfo saveStateSlotInfo(const std::string& appPath, int slot);
bool saveStateWriteSlot(const std::string& appPath, int slot,
    const EmulatorRuntimeState& state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateReadSlot(const std::string& appPath, int slot,
    EmulatorRuntimeState* state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);

#endif

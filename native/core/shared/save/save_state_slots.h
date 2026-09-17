#ifndef DINGOO_PIE_SHARED_SAVE_SAVE_STATE_SLOTS_H
#define DINGOO_PIE_SHARED_SAVE_SAVE_STATE_SLOTS_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

static const int kSaveStateSlotCount = 15;

enum SaveStateFormat
{
    SAVE_STATE_FORMAT_APP,
    SAVE_STATE_FORMAT_CC
};

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
    uint32_t runtimeCount;
};

bool saveStateCompressPayload(const std::vector<uint8_t>& bytes,
    std::vector<uint8_t>* out, SaveStateProgressCallback progressCallback = 0,
    void* progressUserData = 0);
bool saveStateDecompressPayload(const std::vector<uint8_t>& bytes,
    size_t offset, size_t size, size_t expectedSize, std::vector<uint8_t>* out,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);

std::string saveStateGameIdForPath(const std::string& gamePath);
SaveStateFormat saveStateFormatForPath(const std::string& gamePath);
std::string saveStatePathForSlot(const std::string& gamePath,
    SaveStateFormat format, int slot);
std::string saveStateThumbnailPathForSlot(const std::string& gamePath,
    SaveStateFormat format, int slot);
bool saveStateSlotExists(const std::string& gamePath,
    SaveStateFormat format, int slot);
SaveStateSlotInfo saveStateSlotInfo(const std::string& gamePath,
    SaveStateFormat format, int slot);
bool saveStateDeleteSlot(const std::string& gamePath,
    SaveStateFormat format, int slot);

#endif

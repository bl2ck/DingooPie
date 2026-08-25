#ifndef DINGOO_PIE_SHARED_SAVE_SAVE_SLOTS_H
#define DINGOO_PIE_SHARED_SAVE_SAVE_SLOTS_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

static const int kSaveStateSlotCount = 15;

enum SaveStateGameFormat
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

std::string saveStateAppIdForPath(const std::string& appPath);
SaveStateGameFormat saveStateFormatForPath(const std::string& appPath);
std::string saveStatePathForSlot(const std::string& appPath,
    SaveStateGameFormat format, int slot);
std::string saveStateThumbnailPathForSlot(const std::string& appPath,
    SaveStateGameFormat format, int slot);
bool saveStateSlotExists(const std::string& appPath,
    SaveStateGameFormat format, int slot);
SaveStateSlotInfo saveStateSlotInfo(const std::string& appPath,
    SaveStateGameFormat format, int slot);
bool saveStateWriteThumbnailRgb565(const std::string& appPath,
    SaveStateGameFormat format, int slot, const uint16_t* pixels,
    uint32_t width, uint32_t height);
bool saveStateReadThumbnail(const std::string& appPath,
    SaveStateGameFormat format, int slot, std::vector<uint8_t>* out);
bool saveStateDeleteSlot(const std::string& appPath,
    SaveStateGameFormat format, int slot);

#endif

#ifndef DINGOO_PIE_SHARED_SAVE_SAVE_STATE_FORMAT_H
#define DINGOO_PIE_SHARED_SAVE_SAVE_STATE_FORMAT_H

#include <stdint.h>

static constexpr uint32_t makeSaveStateMagic(
    uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3)
{
    return (uint32_t)byte0 |
        ((uint32_t)byte1 << 8) |
        ((uint32_t)byte2 << 16) |
        ((uint32_t)byte3 << 24);
}

static const uint32_t kAppSaveStateMagic =
    makeSaveStateMagic('D', 'G', 'S', 'S');
static const uint32_t kCcSaveStateMagic =
    makeSaveStateMagic('D', 'G', 'F', 'F');
static const uint32_t kAppSaveStateHeaderSize = 96u;
static const uint32_t kCcSaveStateHeaderSize = 116u;

static_assert(kAppSaveStateMagic == 0x53534744u,
    "DGSS magic byte order changed");
static_assert(kCcSaveStateMagic == 0x46464744u,
    "DGFF magic byte order changed");

struct AppSaveStateFileHeader
{
    uint32_t magic;
    uint32_t headerSize;
    uint32_t payloadUncompressedSize;
    uint32_t payloadCompressedSize;
    uint32_t regionCount;
    uint32_t taskRegisterCount;
    uint32_t semaphoreCount;
    uint32_t osTicks;
    char appId[64];
};

struct CcSaveStateFileHeader
{
    uint32_t magic;
    uint32_t headerSize;
    uint32_t payloadSize;
    uint32_t payloadStoredSize;
    uint32_t memoryFlags;
    uint32_t taskCount;
    uint32_t heapCount;
    uint32_t resourceCount;
    uint32_t streamCount;
    uint32_t fileCount;
    uint32_t semaphoreCount;
    uint32_t dynamicImportCount;
    uint32_t unknownImportCount;
    char gameSha256[64];
};

static_assert(sizeof(AppSaveStateFileHeader) == kAppSaveStateHeaderSize,
    "DGSS header layout changed");
static_assert(sizeof(CcSaveStateFileHeader) == kCcSaveStateHeaderSize,
    "DGFF header layout changed");

#endif

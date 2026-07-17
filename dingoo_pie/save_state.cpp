#include "save_state.h"

#include "app_paths.h"
#include "platform_win32.h"
#include "Common/Crypto/sha256.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static const uint32_t kSaveStateMagic = 0x53534744u; // DGSS
static const uint32_t kSaveStateAppIdLength = 64;
static const uint8_t kSaveStateTokenRaw = 0;
static const uint8_t kSaveStateTokenFill = 1;
static const size_t kSaveStateMinFillRun = 16;
static const size_t kSaveStateMaxRawBlock = 0xffffu;

static std::string g_cachedAppPath;
static std::string g_cachedAppId;

// On disk: fixed header followed by the current compressed payload layout.
// The payload stores small runtime records first, then a compact writable
// memory region table followed by contiguous region data. appId verifies the
// save still belongs to the running game.
struct SaveStateHeader
{
    uint32_t magic;
    uint32_t headerSize;
    uint32_t payloadUncompressedSize;
    uint32_t payloadCompressedSize;
    uint32_t regionCount;
    uint32_t taskRegisterCount;
    uint32_t semaphoreCount;
    uint32_t osTicks;
    char appId[kSaveStateAppIdLength];
};

struct SaveStateHeapHeader
{
    uint32_t flags;
    uint32_t beginAddress;
    uint32_t size;
    uint32_t freeNext;
    uint32_t freeLen;
    uint32_t left;
    uint32_t min;
    uint32_t top;
};

struct SaveStateRegisterHeader
{
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

struct SaveStateRegionHeader
{
    uint32_t start;
    uint32_t size;
    uint32_t perms;
};

static void writeHeapHeader(SaveStateHeapHeader* out, const VmHeapSnapshot& in)
{
    memset(out, 0, sizeof(*out));
    out->flags = in.valid ? 1u : 0u;
    out->beginAddress = in.beginAddress;
    out->size = in.size;
    out->freeNext = in.freeNext;
    out->freeLen = in.freeLen;
    out->left = in.left;
    out->min = in.min;
    out->top = in.top;
}

static void readHeapHeader(const SaveStateHeapHeader& in, VmHeapSnapshot* out)
{
    memset(out, 0, sizeof(*out));
    out->valid = (in.flags & 1u) != 0;
    out->beginAddress = in.beginAddress;
    out->size = in.size;
    out->freeNext = in.freeNext;
    out->freeLen = in.freeLen;
    out->left = in.left;
    out->min = in.min;
    out->top = in.top;
}

static void writeRegisterHeader(SaveStateRegisterHeader* out,
    const EmulatorRuntimeRegisterSnapshot& in)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->gpr, in.gpr, sizeof(out->gpr));
    memcpy(out->fpr, in.fpr, sizeof(out->fpr));
    memcpy(out->vfpu, in.vfpu, sizeof(out->vfpu));
    memcpy(out->vfpuCtrl, in.vfpuCtrl, sizeof(out->vfpuCtrl));
    out->pc = in.pc;
    out->hi = in.hi;
    out->lo = in.lo;
    out->fcr31 = in.fcr31;
    out->fpcond = in.fpcond;
}

static void readRegisterHeader(const SaveStateRegisterHeader& in,
    EmulatorRuntimeRegisterSnapshot* out)
{
    memset(out, 0, sizeof(*out));
    out->running = true;
    memcpy(out->gpr, in.gpr, sizeof(out->gpr));
    memcpy(out->fpr, in.fpr, sizeof(out->fpr));
    memcpy(out->vfpu, in.vfpu, sizeof(out->vfpu));
    memcpy(out->vfpuCtrl, in.vfpuCtrl, sizeof(out->vfpuCtrl));
    out->pc = in.pc;
    out->hi = in.hi;
    out->lo = in.lo;
    out->fcr31 = in.fcr31;
    out->fpcond = in.fpcond;
}

static std::string sha256Hex(const uint8_t* data, size_t size)
{
    static const char kHex[] = "0123456789ABCDEF";
    uint8_t digest[32] = {};
    sha256_context context;
    sha256_starts(&context);
    sha256_update(&context, data, (uint32_t)size);
    sha256_finish(&context, digest);

    std::string out;
    out.resize(64);
    for (size_t i = 0; i < sizeof(digest); ++i)
    {
        out[i * 2] = kHex[digest[i] >> 4];
        out[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    return out;
}

static std::string fallbackAppId(const std::string& appPath)
{
    std::string normalized = appNormalizePath(appPath.c_str());
    return sha256Hex((const uint8_t*)normalized.data(), normalized.size());
}

static bool readWholeFile(const std::string& path, std::vector<uint8_t>* out)
{
    if (!out)
    {
        return false;
    }

#ifdef _WIN32
    std::wstring widePath = platformUtf8ToWide(path);
    FILE* file = _wfopen(widePath.c_str(), L"rb");
#else
    FILE* file = fopen(path.c_str(), "rb");
#endif
    if (!file)
    {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return false;
    }

    out->resize((size_t)size);
    bool ok = out->empty() ||
        fread(out->data(), 1, out->size(), file) == out->size();
    fclose(file);
    return ok;
}

static bool readSaveStateHeader(const std::string& path, SaveStateHeader* out)
{
    if (!out)
    {
        return false;
    }

#ifdef _WIN32
    std::wstring widePath = platformUtf8ToWide(path);
    FILE* file = _wfopen(widePath.c_str(), L"rb");
#else
    FILE* file = fopen(path.c_str(), "rb");
#endif
    if (!file)
    {
        return false;
    }

    bool ok = fread(out, 1, sizeof(*out), file) == sizeof(*out);
    fclose(file);
    return ok;
}

static bool writeAll(const std::string& path, const uint8_t* data, size_t size)
{
#ifdef _WIN32
    std::wstring widePath = platformUtf8ToWide(path);
    FILE* file = _wfopen(widePath.c_str(), L"wb");
#else
    FILE* file = fopen(path.c_str(), "wb");
#endif
    if (!file)
    {
        return false;
    }

    bool ok = size == 0 || fwrite(data, 1, size, file) == size;
    ok = fclose(file) == 0 && ok;
    return ok;
}

static bool fileExists(const std::string& path)
{
#ifdef _WIN32
    std::wstring widePath = platformUtf8ToWide(path);
    DWORD attrs = GetFileAttributesW(widePath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static uint64_t fileModifiedTime(const std::string& path)
{
#ifdef _WIN32
    std::wstring widePath = platformUtf8ToWide(path);
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(widePath.c_str(), GetFileExInfoStandard, &data))
    {
        return 0;
    }

    ULARGE_INTEGER fileTime;
    fileTime.LowPart = data.ftLastWriteTime.dwLowDateTime;
    fileTime.HighPart = data.ftLastWriteTime.dwHighDateTime;
    if (fileTime.QuadPart < 116444736000000000ull)
    {
        return 0;
    }
    return (fileTime.QuadPart - 116444736000000000ull) / 10000000ull;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
    {
        return 0;
    }
    return (uint64_t)st.st_mtime;
#endif
}

static bool ensureDirectory(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }

#ifdef _WIN32
    std::wstring widePath = platformUtf8ToWide(path);
    if (CreateDirectoryW(widePath.c_str(), NULL))
    {
        return true;
    }
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS)
    {
        DWORD attrs = GetFileAttributesW(widePath.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    }
    return false;
#else
    if (mkdir(path.c_str(), 0755) == 0 || errno == EEXIST)
    {
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    return false;
#endif
}

static std::string parentDirectory(const std::string& path)
{
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
    {
        return ".";
    }
    return path.substr(0, pos);
}

static void appendBytes(std::vector<uint8_t>* out, const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    out->insert(out->end(), bytes, bytes + size);
}

static bool hasBytes(size_t totalSize, size_t offset, size_t size)
{
    return offset <= totalSize && size <= totalSize - offset;
}

static void appendLe16(std::vector<uint8_t>* out, uint16_t value)
{
    out->push_back((uint8_t)(value & 0xff));
    out->push_back((uint8_t)((value >> 8) & 0xff));
}

static void appendLe32(std::vector<uint8_t>* out, uint32_t value)
{
    out->push_back((uint8_t)(value & 0xff));
    out->push_back((uint8_t)((value >> 8) & 0xff));
    out->push_back((uint8_t)((value >> 16) & 0xff));
    out->push_back((uint8_t)((value >> 24) & 0xff));
}

static bool readLe16(const std::vector<uint8_t>& bytes, size_t* offset,
    size_t limit, uint16_t* out)
{
    if (!offset || !out || limit > bytes.size() ||
        *offset > limit || sizeof(uint16_t) > limit - *offset)
    {
        return false;
    }

    const uint8_t* p = bytes.data() + *offset;
    *out = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    *offset += sizeof(uint16_t);
    return true;
}

static bool readLe32(const std::vector<uint8_t>& bytes, size_t* offset,
    size_t limit, uint32_t* out)
{
    if (!offset || !out || limit > bytes.size() ||
        *offset > limit || sizeof(uint32_t) > limit - *offset)
    {
        return false;
    }

    const uint8_t* p = bytes.data() + *offset;
    *out = (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
    *offset += sizeof(uint32_t);
    return true;
}

static size_t fillRunLength(const std::vector<uint8_t>& bytes, size_t offset)
{
    uint8_t value = bytes[offset];
    size_t end = offset + 1;
    while (end < bytes.size() && bytes[end] == value)
    {
        ++end;
    }
    return end - offset;
}

static void appendRawSaveStateBlock(std::vector<uint8_t>* out,
    const std::vector<uint8_t>& bytes, size_t offset, size_t size)
{
    while (size > 0)
    {
        size_t chunk = size > kSaveStateMaxRawBlock ? kSaveStateMaxRawBlock : size;
        out->push_back(kSaveStateTokenRaw);
        appendLe16(out, (uint16_t)chunk);
        appendBytes(out, bytes.data() + offset, chunk);
        offset += chunk;
        size -= chunk;
    }
}

static void appendFillSaveStateBlock(std::vector<uint8_t>* out, size_t size, uint8_t value)
{
    while (size > 0)
    {
        uint32_t chunk = size > 0xffffffffu ? 0xffffffffu : (uint32_t)size;
        out->push_back(kSaveStateTokenFill);
        appendLe32(out, chunk);
        out->push_back(value);
        size -= chunk;
    }
}

static void reportSaveStateProgress(SaveStateProgressCallback callback,
    void* userData, SaveStateProgressPhase phase, uint32_t percent)
{
    if (callback)
    {
        SaveStateProgress progress;
        progress.phase = phase;
        progress.percent = percent > 100 ? 100 : percent;
        callback(progress, userData);
    }
}

static uint32_t saveStatePercent(size_t done, size_t total)
{
    if (total == 0)
    {
        return 100;
    }
    return (uint32_t)((done * 100u) / total);
}

static size_t estimateCompressedPayloadCapacity(size_t size)
{
    if (size == 0)
    {
        return 0;
    }

    size_t rawBlocks = ((size - 1) / kSaveStateMaxRawBlock) + 1;
    size_t overhead = rawBlocks * 3u;
    if (size > ((size_t)-1) - overhead)
    {
        return size;
    }
    return size + overhead;
}

static bool compressSaveStatePayload(const std::vector<uint8_t>& bytes,
    std::vector<uint8_t>* out, SaveStateProgressCallback progressCallback,
    void* progressUserData)
{
    if (!out)
    {
        return false;
    }

    out->clear();
    out->reserve(estimateCompressedPayloadCapacity(bytes.size()));
    reportSaveStateProgress(progressCallback, progressUserData,
        SAVE_STATE_PROGRESS_COMPRESS, 0);

    // RLE uses raw blocks for mixed data and fill blocks for long repeated byte
    // runs, which keeps large zeroed heap/VRAM areas compact without external dependencies.
    size_t offset = 0;
    uint32_t lastPercent = 0;
    while (offset < bytes.size())
    {
        size_t run = fillRunLength(bytes, offset);
        if (run >= kSaveStateMinFillRun)
        {
            appendFillSaveStateBlock(out, run, bytes[offset]);
            offset += run;
            uint32_t percent = saveStatePercent(offset, bytes.size());
            if (percent != lastPercent)
            {
                lastPercent = percent;
                reportSaveStateProgress(progressCallback, progressUserData,
                    SAVE_STATE_PROGRESS_COMPRESS, percent);
            }
            continue;
        }

        size_t rawStart = offset++;
        while (offset < bytes.size())
        {
            run = fillRunLength(bytes, offset);
            if (run >= kSaveStateMinFillRun ||
                offset - rawStart >= kSaveStateMaxRawBlock)
            {
                break;
            }
            ++offset;
        }
        appendRawSaveStateBlock(out, bytes, rawStart, offset - rawStart);

        uint32_t percent = saveStatePercent(offset, bytes.size());
        if (percent != lastPercent)
        {
            lastPercent = percent;
            reportSaveStateProgress(progressCallback, progressUserData,
                SAVE_STATE_PROGRESS_COMPRESS, percent);
        }
    }
    reportSaveStateProgress(progressCallback, progressUserData,
        SAVE_STATE_PROGRESS_COMPRESS, 100);
    return true;
}

static bool decompressSaveStatePayload(const std::vector<uint8_t>& bytes,
    size_t offset, size_t size, size_t expectedSize, std::vector<uint8_t>* out,
    SaveStateProgressCallback progressCallback, void* progressUserData)
{
    if (!out || !hasBytes(bytes.size(), offset, size) ||
        expectedSize > 0xffffffffu)
    {
        return false;
    }

    size_t start = offset;
    size_t end = offset + size;
    out->clear();
    out->reserve(expectedSize);
    reportSaveStateProgress(progressCallback, progressUserData,
        SAVE_STATE_PROGRESS_DECOMPRESS, 0);
    uint32_t lastPercent = 0;
    while (offset < end)
    {
        uint8_t token = bytes[offset++];
        if (token == kSaveStateTokenRaw)
        {
            uint16_t length = 0;
            if (!readLe16(bytes, &offset, end, &length) || length == 0 ||
                offset > end || (size_t)length > end - offset ||
                out->size() > expectedSize ||
                (size_t)length > expectedSize - out->size())
            {
                return false;
            }
            appendBytes(out, bytes.data() + offset, length);
            offset += length;
        }
        else if (token == kSaveStateTokenFill)
        {
            uint32_t length = 0;
            if (!readLe32(bytes, &offset, end, &length) || length == 0 ||
                offset >= end || out->size() > expectedSize ||
                (size_t)length > expectedSize - out->size())
            {
                return false;
            }
            uint8_t value = bytes[offset++];
            out->resize(out->size() + length, value);
        }
        else
        {
            return false;
        }

        uint32_t percent = saveStatePercent(offset - start, size);
        if (percent != lastPercent)
        {
            lastPercent = percent;
            reportSaveStateProgress(progressCallback, progressUserData,
                SAVE_STATE_PROGRESS_DECOMPRESS, percent);
        }
    }
    reportSaveStateProgress(progressCallback, progressUserData,
        SAVE_STATE_PROGRESS_DECOMPRESS, 100);
    return out->size() == expectedSize;
}

static bool recordCountFits(size_t totalSize, size_t offset, uint32_t count,
    size_t recordSize)
{
    return offset <= totalSize &&
        count <= (totalSize - offset) / recordSize;
}

// Save-state files are byte streams; copy fixed-size records after bounds
// checks so truncated files cannot produce unaligned or out-of-range reads.
template <typename T>
static bool readRecord(const std::vector<uint8_t>& bytes, size_t* offset, T* out)
{
    if (!offset || !out || !hasBytes(bytes.size(), *offset, sizeof(T)))
    {
        return false;
    }

    memcpy(out, bytes.data() + *offset, sizeof(T));
    *offset += sizeof(T);
    return true;
}

static bool addPayloadSize(size_t* total, size_t amount)
{
    if (!total || *total > ((size_t)-1) - amount)
    {
        return false;
    }

    *total += amount;
    return true;
}

static bool addPayloadArraySize(size_t* total, size_t count, size_t recordSize)
{
    if (recordSize != 0 && count > ((size_t)-1) / recordSize)
    {
        return false;
    }
    return addPayloadSize(total, count * recordSize);
}

static bool estimateSaveStatePayloadSize(const EmulatorRuntimeState& state,
    size_t* out)
{
    if (!out)
    {
        return false;
    }

    size_t size = 0;
    if (!addPayloadSize(&size, sizeof(SaveStateHeapHeader)) ||
        !addPayloadSize(&size, sizeof(SaveStateRegisterHeader)) ||
        !addPayloadArraySize(&size, state.taskRegisters.size(),
            sizeof(SaveStateRegisterHeader)) ||
        !addPayloadArraySize(&size, state.hleSemaphoreCounts.size(),
            sizeof(uint32_t)) ||
        !addPayloadArraySize(&size, state.regions.size(),
            sizeof(SaveStateRegionHeader)))
    {
        return false;
    }

    for (size_t i = 0; i < state.regions.size(); ++i)
    {
        if (!addPayloadSize(&size, state.regions[i].data.size()))
        {
            return false;
        }
    }

    *out = size;
    return true;
}

static bool buildSaveStatePayload(const EmulatorRuntimeState& state,
    std::vector<uint8_t>* payload, std::string* error)
{
    if (!payload)
    {
        return false;
    }
    if (state.taskRegisters.size() > 0xffffffffu ||
        state.hleSemaphoreCounts.size() > 0xffffffffu ||
        state.regions.size() > 0xffffffffu)
    {
        if (error) *error = "runtime state has too many records";
        return false;
    }
    for (size_t i = 0; i < state.regions.size(); ++i)
    {
        const EmulatorRuntimeStateRegion& region = state.regions[i];
        if (region.size == 0 || region.data.size() != region.size)
        {
            if (error) *error = "invalid state region";
            return false;
        }
    }

    size_t estimatedSize = 0;
    if (!estimateSaveStatePayloadSize(state, &estimatedSize))
    {
        if (error) *error = "save-state payload is too large";
        return false;
    }

    payload->clear();
    payload->reserve(estimatedSize);

    SaveStateHeapHeader heapHeader;
    writeHeapHeader(&heapHeader, state.heap);
    appendBytes(payload, &heapHeader, sizeof(heapHeader));

    SaveStateRegisterHeader mainRegisterHeader;
    writeRegisterHeader(&mainRegisterHeader, state.registers);
    appendBytes(payload, &mainRegisterHeader, sizeof(mainRegisterHeader));

    for (size_t i = 0; i < state.taskRegisters.size(); ++i)
    {
        SaveStateRegisterHeader taskHeader;
        writeRegisterHeader(&taskHeader, state.taskRegisters[i]);
        appendBytes(payload, &taskHeader, sizeof(taskHeader));
    }

    appendBytes(payload, state.hleSemaphoreCounts.data(),
        state.hleSemaphoreCounts.size() * sizeof(state.hleSemaphoreCounts[0]));

    for (size_t i = 0; i < state.regions.size(); ++i)
    {
        const EmulatorRuntimeStateRegion& region = state.regions[i];
        SaveStateRegionHeader regionHeader;
        regionHeader.start = region.start;
        regionHeader.size = region.size;
        regionHeader.perms = region.perms;
        appendBytes(payload, &regionHeader, sizeof(regionHeader));
    }

    for (size_t i = 0; i < state.regions.size(); ++i)
    {
        const EmulatorRuntimeStateRegion& region = state.regions[i];
        appendBytes(payload, region.data.data(), region.data.size());
    }

    if (payload->size() != estimatedSize)
    {
        if (error) *error = "save-state payload size mismatch";
        return false;
    }
    return true;
}

static bool readTaskRegisterRecords(const std::vector<uint8_t>& records,
    size_t* offset, uint32_t count,
    std::vector<EmulatorRuntimeRegisterSnapshot>* out, std::string* error)
{
    if (!offset || !out ||
        !recordCountFits(records.size(), *offset, count,
            sizeof(SaveStateRegisterHeader)))
    {
        if (error) *error = "save-state task register table is truncated";
        return false;
    }

    out->clear();
    out->reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        SaveStateRegisterHeader taskHeader;
        if (!readRecord(records, offset, &taskHeader))
        {
            if (error) *error = "save-state task register table is truncated";
            return false;
        }

        EmulatorRuntimeRegisterSnapshot taskSnapshot;
        readRegisterHeader(taskHeader, &taskSnapshot);
        out->push_back(taskSnapshot);
    }
    return true;
}

static bool readSemaphoreRecords(const std::vector<uint8_t>& records,
    size_t* offset, uint32_t count, std::vector<uint32_t>* out,
    std::string* error)
{
    if (!offset || !out || count == 0 ||
        !recordCountFits(records.size(), *offset, count, sizeof(uint32_t)))
    {
        if (error) *error = "save-state semaphore table is truncated";
        return false;
    }

    out->resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!readRecord(records, offset, &(*out)[i]))
        {
            if (error) *error = "save-state semaphore table is truncated";
            return false;
        }
    }
    return true;
}

static bool readRegionRecords(const std::vector<uint8_t>& records,
    size_t* offset, uint32_t count,
    std::vector<EmulatorRuntimeStateRegion>* out, std::string* error)
{
    if (!offset || !out || count == 0 ||
        !recordCountFits(records.size(), *offset, count,
            sizeof(SaveStateRegionHeader)))
    {
        if (error) *error = "save-state region table is truncated";
        return false;
    }

    std::vector<SaveStateRegionHeader> regionHeaders;
    regionHeaders.resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!readRecord(records, offset, &regionHeaders[i]) ||
            regionHeaders[i].size == 0)
        {
            if (error) *error = "save-state region table is truncated";
            return false;
        }
    }

    size_t totalDataSize = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!addPayloadSize(&totalDataSize, regionHeaders[i].size))
        {
            if (error) *error = "save-state region data is too large";
            return false;
        }
    }
    if (!hasBytes(records.size(), *offset, totalDataSize))
    {
        if (error) *error = "save-state region data is truncated";
        return false;
    }

    out->clear();
    out->reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const SaveStateRegionHeader& regionHeader = regionHeaders[i];
        EmulatorRuntimeStateRegion region;
        region.start = regionHeader.start;
        region.size = regionHeader.size;
        region.perms = regionHeader.perms;
        region.data.assign(records.begin() + *offset,
            records.begin() + *offset + regionHeader.size);
        *offset += regionHeader.size;
        out->push_back(region);
    }
    return true;
}

static size_t boundedStringLength(const char* text, size_t maxLength)
{
    size_t length = 0;
    while (length < maxLength && text[length])
    {
        ++length;
    }
    return length;
}

static std::string saveStateFileStemForPath(const std::string& appPath)
{
    std::string name = appFileNameFromPath(appNormalizePath(appPath.c_str()));
    if (appPathHasAppExtension(name))
    {
        name.resize(name.size() - 4);
    }
    return name.empty() ? "game" : name;
}

std::string saveStateAppIdForPath(const std::string& appPath)
{
    std::string normalized = appNormalizePath(appPath.c_str());
    if (!normalized.empty() && normalized == g_cachedAppPath && !g_cachedAppId.empty())
    {
        return g_cachedAppId;
    }

    std::vector<uint8_t> data;
    std::string appId;
    if (!normalized.empty() && readWholeFile(normalized, &data) && !data.empty())
    {
        appId = sha256Hex(data.data(), data.size());
    }
    else
    {
        appId = fallbackAppId(normalized);
    }

    g_cachedAppPath = normalized;
    g_cachedAppId = appId;
    return appId;
}

std::string saveStatePathForSlot(const std::string& appPath, int slot)
{
    if (slot < 1 || slot > kSaveStateSlotCount)
    {
        return "";
    }

    std::string dir = parentDirectory(appNormalizePath(appPath.c_str()));
    if (dir.empty())
    {
        dir = ".";
    }
    dir += "\\savestates";

    char slotSuffix[16] = {};
    snprintf(slotSuffix, sizeof(slotSuffix), ".slot%d.dps", slot);
    return dir + "\\" + saveStateFileStemForPath(appPath) + slotSuffix;
}

std::string saveStateThumbnailPathForSlot(const std::string& appPath, int slot)
{
    std::string path = saveStatePathForSlot(appPath, slot);
    if (path.empty())
    {
        return "";
    }

    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos)
    {
        path.resize(dot);
    }
    path += ".thumb.bmp";
    return path;
}

SaveStateSlotInfo saveStateSlotInfo(const std::string& appPath, int slot)
{
    SaveStateSlotInfo info;
    info.exists = false;
    info.path = saveStatePathForSlot(appPath, slot);
    info.modifiedTime = 0;
    info.runtimeCountValid = false;
    info.runtimeCount = 0;
    if (!info.path.empty())
    {
        info.exists = fileExists(info.path);
        if (info.exists)
        {
            info.modifiedTime = fileModifiedTime(info.path);
            SaveStateHeader header;
            if (readSaveStateHeader(info.path, &header) &&
                header.magic == kSaveStateMagic &&
                header.headerSize == sizeof(SaveStateHeader))
            {
                info.runtimeCountValid = true;
                info.runtimeCount = 1u + header.taskRegisterCount;
            }
        }
    }
    return info;
}

bool saveStateWriteSlot(const std::string& appPath, int slot,
    const EmulatorRuntimeState& state, std::string* error,
    SaveStateProgressCallback progressCallback, void* progressUserData)
{
    if (!state.registers.running || state.regions.empty() || state.hleSemaphoreCounts.empty())
    {
        if (error) *error = "runtime state is not available";
        return false;
    }

    std::string path = saveStatePathForSlot(appPath, slot);
    if (path.empty())
    {
        if (error) *error = "invalid slot";
        return false;
    }
    if (!ensureDirectory(parentDirectory(path)))
    {
        if (error) *error = "failed to create save-state directory";
        return false;
    }

    std::vector<uint8_t> payload;
    if (!buildSaveStatePayload(state, &payload, error))
    {
        return false;
    }

    std::vector<uint8_t> compressedPayload;
    if (!compressSaveStatePayload(payload, &compressedPayload,
        progressCallback, progressUserData))
    {
        if (error) *error = "failed to compress save-state file";
        return false;
    }
    if (payload.size() > 0xffffffffu || compressedPayload.size() > 0xffffffffu)
    {
        if (error) *error = "save-state file is too large";
        return false;
    }

    SaveStateHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = kSaveStateMagic;
    header.headerSize = sizeof(header);
    header.payloadUncompressedSize = (uint32_t)payload.size();
    header.payloadCompressedSize = (uint32_t)compressedPayload.size();
    header.regionCount = (uint32_t)state.regions.size();
    header.taskRegisterCount = (uint32_t)state.taskRegisters.size();
    header.semaphoreCount = (uint32_t)state.hleSemaphoreCounts.size();
    header.osTicks = state.osTicks;

    std::string appId = saveStateAppIdForPath(appPath);
    memcpy(header.appId, appId.c_str(),
        appId.size() < sizeof(header.appId) ? appId.size() : sizeof(header.appId));

    std::vector<uint8_t> bytes;
    bytes.reserve(sizeof(header) + compressedPayload.size());
    appendBytes(&bytes, &header, sizeof(header));
    appendBytes(&bytes, compressedPayload.data(), compressedPayload.size());

    std::string tempPath = path + ".tmp";
    if (!writeAll(tempPath, bytes.data(), bytes.size()))
    {
        if (error) *error = "failed to write save-state file";
        return false;
    }

#ifdef _WIN32
    std::wstring wideTemp = platformUtf8ToWide(tempPath);
    std::wstring widePath = platformUtf8ToWide(path);
    if (!MoveFileExW(wideTemp.c_str(), widePath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(wideTemp.c_str());
        if (error) *error = "failed to finalize save-state file";
        return false;
    }
#else
    if (rename(tempPath.c_str(), path.c_str()) != 0)
    {
        remove(tempPath.c_str());
        if (error) *error = "failed to finalize save-state file";
        return false;
    }
#endif
    return true;
}

bool saveStateReadSlot(const std::string& appPath, int slot,
    EmulatorRuntimeState* state, std::string* error,
    SaveStateProgressCallback progressCallback, void* progressUserData)
{
    if (!state)
    {
        return false;
    }
    state->regions.clear();
    state->taskRegisters.clear();
    state->hleSemaphoreCounts.clear();
    memset(&state->registers, 0, sizeof(state->registers));
    memset(&state->heap, 0, sizeof(state->heap));
    state->osTicks = 0;

    std::string path = saveStatePathForSlot(appPath, slot);
    std::vector<uint8_t> bytes;
    if (path.empty() || !readWholeFile(path, &bytes))
    {
        if (error) *error = "save-state file not found";
        return false;
    }
    if (bytes.size() < sizeof(SaveStateHeader))
    {
        if (error) *error = "save-state file is truncated";
        return false;
    }

    size_t offset = 0;
    SaveStateHeader header;
    if (!readRecord(bytes, &offset, &header) ||
        header.magic != kSaveStateMagic ||
        header.headerSize != sizeof(SaveStateHeader) ||
        header.payloadUncompressedSize == 0 ||
        header.payloadCompressedSize == 0)
    {
        if (error) *error = "unsupported save-state file";
        return false;
    }

    std::string expectedAppId = saveStateAppIdForPath(appPath);
    std::string savedAppId(header.appId,
        header.appId + boundedStringLength(header.appId, sizeof(header.appId)));
    if (savedAppId != expectedAppId)
    {
        if (error) *error = "save-state belongs to a different game";
        return false;
    }

    std::vector<uint8_t> payload;
    if (!decompressSaveStatePayload(bytes, offset, header.payloadCompressedSize,
        header.payloadUncompressedSize, &payload, progressCallback, progressUserData))
    {
        if (error) *error = "failed to decompress save-state file";
        return false;
    }
    offset += header.payloadCompressedSize;
    if (offset != bytes.size())
    {
        if (error) *error = "save-state file has unexpected data";
        return false;
    }

    const std::vector<uint8_t>& records = payload;
    offset = 0;
    SaveStateHeapHeader heapHeader;
    if (!readRecord(records, &offset, &heapHeader))
    {
        if (error) *error = "save-state file is truncated";
        return false;
    }
    readHeapHeader(heapHeader, &state->heap);
    state->osTicks = header.osTicks;

    SaveStateRegisterHeader mainRegisterHeader;
    if (!readRecord(records, &offset, &mainRegisterHeader))
    {
        if (error) *error = "save-state file is truncated";
        return false;
    }
    readRegisterHeader(mainRegisterHeader, &state->registers);

    if (!readTaskRegisterRecords(records, &offset, header.taskRegisterCount,
        &state->taskRegisters, error) ||
        !readSemaphoreRecords(records, &offset, header.semaphoreCount,
            &state->hleSemaphoreCounts, error) ||
        !readRegionRecords(records, &offset, header.regionCount,
            &state->regions, error))
    {
        return false;
    }
    if (offset != records.size())
    {
        if (error) *error = "save-state file has unexpected data";
        return false;
    }

    return true;
}

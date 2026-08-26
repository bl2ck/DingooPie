#include "shared/services/guest_filesystem.h"
#include "config/compatibility/compat_profile.h"
#include "shared/platform/storage_services.h"
#include "shared/save/guest_save_transaction.h"
#include "shared/diagnostics/runtime_log.h"
#include "runtime_resource_monitor.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <atomic>
#include <mutex>

enum VirtualFileType {
    VIRTUAL_FILE_TYPE_NONE,
    VIRTUAL_FILE_TYPE_HOST,
    VIRTUAL_FILE_TYPE_RESOURCE
};

struct VirtualFileEntry {
    VirtualFileType type;
    FILE* fp;
    const uint8_t* data;
    uint8_t* ownedData;
    GuestResourceEntry* resource;
    bool isGuestPackage;
    char requestName[1024];
    uint32_t size;
    uint32_t offset;
    uint8_t xorKey;
    bool saveTransaction;
    char saveTransactionName[1024];
    bool writable;
    bool wroteData;
};

static VirtualFileEntry s_fileMap[128];
static GuestPackage* s_guestPackage = NULL;
static const char* s_guestPackageSha256 = "";
static std::string s_guestPackageName;
static std::string s_saveDirectory;
static std::atomic<bool> s_suspiciousOpenFailure(false);
static std::atomic<bool> s_successfulSaveWrite(false);
static std::recursive_mutex s_filesystemMutex;

struct FilesystemProfile {
    uint64_t fopenCalls;
    uint64_t fcloseCalls;
    uint64_t freadCalls;
    uint64_t freadBytes;
    uint64_t fseekCalls;
    uint64_t ftellCalls;
    uint64_t feofCalls;
    uint64_t resourceOpens;
    uint64_t resourceCachedOpens;
    uint64_t hostOpens;
    uint64_t hostReadCalls;
    uint64_t hostReadBytes;
    uint64_t hostSeekCalls;
    uint64_t resourceReadCalls;
    uint64_t resourceReadBytes;
    uint64_t resourceSeekCalls;
    uint64_t fastFreadCalls;
    uint64_t fastFreadBytes;
    uint64_t fastFseekCalls;
    uint64_t slowFreadCalls;
    uint64_t slowFreadBytes;
    uint64_t slowFseekCalls;
};

static FilesystemProfile s_filesystemProfile = {};
static thread_local int s_fastHleCallDepth = 0;
static std::atomic<bool> s_profileEnabled(false);

static uint64_t fsysNowMs(void)
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static bool fsysProfileEnabled(void)
{
    return s_profileEnabled.load();
}

void fsys_set_profile_enabled(bool enabled)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_profileEnabled.store(enabled);
}

static void fsysProfileTick(void)
{
    static uint64_t lastTicks = 0;
    if (!fsysProfileEnabled())
    {
        return;
    }

    uint64_t now = fsysNowMs();
    if (!lastTicks)
    {
        lastTicks = now;
        return;
    }
    if (now - lastTicks < runtimeLogProfileIntervalMs())
    {
        return;
    }

    if (!s_filesystemProfile.fopenCalls && !s_filesystemProfile.hostOpens &&
        !s_filesystemProfile.resourceOpens && !s_filesystemProfile.resourceCachedOpens &&
        !s_filesystemProfile.freadCalls && !s_filesystemProfile.freadBytes &&
        !s_filesystemProfile.fseekCalls && !s_filesystemProfile.fastFreadCalls &&
        !s_filesystemProfile.fastFreadBytes && !s_filesystemProfile.fastFseekCalls &&
        !s_filesystemProfile.slowFreadCalls && !s_filesystemProfile.slowFreadBytes &&
        !s_filesystemProfile.slowFseekCalls && !s_filesystemProfile.hostReadCalls &&
        !s_filesystemProfile.hostReadBytes && !s_filesystemProfile.hostSeekCalls &&
        !s_filesystemProfile.resourceReadCalls && !s_filesystemProfile.resourceReadBytes &&
        !s_filesystemProfile.resourceSeekCalls && !s_filesystemProfile.ftellCalls &&
        !s_filesystemProfile.feofCalls && !s_filesystemProfile.fcloseCalls &&
        !runtimeLogShouldPrintEmptyProfile())
    {
        lastTicks = now;
        return;
    }

    printf("profile:fsys fopen=%llu host=%llu resource=%llu cached=%llu "
        "fread=%llu/%llub fseek=%llu fast=%llu/%llub/%llu "
        "slow=%llu/%llub/%llu host_io=%llu/%llub/%llu "
        "resource_io=%llu/%llub/%llu ftell=%llu feof=%llu fclose=%llu\n",
        (unsigned long long)s_filesystemProfile.fopenCalls,
        (unsigned long long)s_filesystemProfile.hostOpens,
        (unsigned long long)s_filesystemProfile.resourceOpens,
        (unsigned long long)s_filesystemProfile.resourceCachedOpens,
        (unsigned long long)s_filesystemProfile.freadCalls,
        (unsigned long long)s_filesystemProfile.freadBytes,
        (unsigned long long)s_filesystemProfile.fseekCalls,
        (unsigned long long)s_filesystemProfile.fastFreadCalls,
        (unsigned long long)s_filesystemProfile.fastFreadBytes,
        (unsigned long long)s_filesystemProfile.fastFseekCalls,
        (unsigned long long)s_filesystemProfile.slowFreadCalls,
        (unsigned long long)s_filesystemProfile.slowFreadBytes,
        (unsigned long long)s_filesystemProfile.slowFseekCalls,
        (unsigned long long)s_filesystemProfile.hostReadCalls,
        (unsigned long long)s_filesystemProfile.hostReadBytes,
        (unsigned long long)s_filesystemProfile.hostSeekCalls,
        (unsigned long long)s_filesystemProfile.resourceReadCalls,
        (unsigned long long)s_filesystemProfile.resourceReadBytes,
        (unsigned long long)s_filesystemProfile.resourceSeekCalls,
        (unsigned long long)s_filesystemProfile.ftellCalls,
        (unsigned long long)s_filesystemProfile.feofCalls,
        (unsigned long long)s_filesystemProfile.fcloseCalls);

    memset(&s_filesystemProfile, 0x00, sizeof(s_filesystemProfile));
    lastTicks = now;
}

void fsys_begin_fast_hle_call(void)
{
    s_fastHleCallDepth++;
}

void fsys_end_fast_hle_call(void)
{
    if (s_fastHleCallDepth > 0)
    {
        s_fastHleCallDepth--;
    }
}

static bool fsysInFastHleCall(void)
{
    return s_fastHleCallDepth > 0;
}

static bool traceFsEnabled(void)
{
    const char* value = getenv("DINGOO_PIE_TRACE_FS");
    return value && value[0] && strcmp(value, "0") != 0;
}

static bool traceFsOpenEnabled(void)
{
    const char* value = getenv("DINGOO_PIE_TRACE_FS_OPEN");
    return value && value[0] && strcmp(value, "0") != 0;
}

static const char* virtualFileTypeName(VirtualFileType type)
{
    switch (type)
    {
    case VIRTUAL_FILE_TYPE_HOST:
        return "host";
    case VIRTUAL_FILE_TYPE_RESOURCE:
        return "resource";
    default:
        return "none";
    }
}

void fsys_set_guest_package(GuestPackage* guestPackage)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_guestPackage = guestPackage;
    s_suspiciousOpenFailure.store(false);
    s_successfulSaveWrite.store(false);
}

void fsys_reset_guest_package(GuestPackage* guestPackage)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    for (uint32_t index = 1; index < sizeof(s_fileMap) / sizeof(s_fileMap[0]); ++index)
    {
        if (s_fileMap[index].type != VIRTUAL_FILE_TYPE_NONE)
        {
            fsys_fclose(index);
        }
    }
    fsys_set_guest_package(guestPackage);
}

void fsys_set_game_identity(const char* sha256Hex)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_guestPackageSha256 = sha256Hex ? sha256Hex : "";
    s_suspiciousOpenFailure.store(false);
}

void fsys_set_game_name(const char* gameName)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_guestPackageName = gameName ? gameName : "";
    s_suspiciousOpenFailure.store(false);
}

void fsys_set_save_directory(const char* directory)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_saveDirectory = directory ? directory : "";
}

bool fsys_saw_suspicious_open_failure(void)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    return s_suspiciousOpenFailure.load();
}

bool fsys_saw_successful_save_write(void)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    return s_successfulSaveWrite.load();
}

static bool isBlockBreakerApp(void)
{
    return compatShouldUseBinResourceView(s_guestPackageSha256);
}

static const char* fsysPathBasenameLocal(const char* path)
{
    if (!path)
    {
        return NULL;
    }

    const char* base = path;
    for (const char* p = path; *p; ++p)
    {
        if (*p == '\\' || *p == '/' || *p == ':')
        {
            base = p + 1;
        }
    }
    return base;
}

static uint16_t readLe16(const uint8_t* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void writeLe32(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xff);
    data[1] = (uint8_t)((value >> 8) & 0xff);
    data[2] = (uint8_t)((value >> 16) & 0xff);
    data[3] = (uint8_t)((value >> 24) & 0xff);
}

static bool shouldUseBlockBreakerBinView(const char* name, const uint8_t* data, uint32_t size)
{
    if (!isBlockBreakerApp() || !name || !data || size < 12)
    {
        return false;
    }

    const char* base = fsysPathBasenameLocal(name);
    const char* dot = base ? strrchr(base, '.') : NULL;
    if (!dot || _stricmp(dot, ".bin") != 0)
    {
        return false;
    }

    uint32_t dwordSize = (uint32_t)data[8] |
        ((uint32_t)data[9] << 8) |
        ((uint32_t)data[10] << 16) |
        ((uint32_t)data[11] << 24);
    return dwordSize > size && readLe16(data) > 0;
}

static uint8_t* createBlockBreakerBinView(const uint8_t* data, uint32_t size, uint32_t* outSize)
{
    if (!data || !outSize || size <= 4)
    {
        return NULL;
    }

    uint32_t payloadSize = size - 4;
    uint32_t viewSize = 4 + 16 + payloadSize;
    uint8_t* view = (uint8_t*)malloc(viewSize);
    if (!view)
    {
        assert(0);
        return NULL;
    }

    memset(view, 0x00, viewSize);
    writeLe32(view + 0, 1);
    writeLe32(view + 4, 0);
    writeLe32(view + 8, payloadSize);
    memcpy(view + 12, data + 4, payloadSize);
    writeLe32(view + 16, 0);
    *outSize = viewSize;
    return view;
}

static uint32_t allocateFileSlot(void)
{
    for (uint32_t index = 1; index < sizeof(s_fileMap) / sizeof(s_fileMap[0]); ++index)
    {
        if (s_fileMap[index].type == VIRTUAL_FILE_TYPE_NONE)
        {
            return index;
        }
    }

    printf("fsys: virtual file table exhausted capacity=%u\n",
        (unsigned)(sizeof(s_fileMap) / sizeof(s_fileMap[0]) - 1));
    return 0;
}

static int modeAllowsWrites(const char* mode)
{
    return mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'));
}

static bool pathLooksSuspiciousFailedRead(const char* path)
{
    if (!path)
    {
        return false;
    }

    size_t length = 0;
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p)
    {
        length++;
        if (*p < 0x20 || *p == 0x7f)
        {
            return true;
        }
    }

    return length == 1;
}

static void recordOpenFailure(const char* name, const char* mode)
{
    if (!modeAllowsWrites(mode) && pathLooksSuspiciousFailedRead(name))
    {
        s_suspiciousOpenFailure.store(true);
        if (traceFsEnabled() || traceFsOpenEnabled())
        {
            printf("trace-fs: suspicious open failure name=%s mode=%s\n", name, mode);
        }
    }
}

static bool normalizeHostFileMode(const char* mode, char* out, size_t outSize)
{
    if (!mode || !out || outSize == 0)
    {
        return false;
    }

    bool hasBinary = false;
    size_t pos = 0;
    for (const char* p = mode; *p; ++p)
    {
        if (*p == 's' || *p == 't')
        {
            continue;
        }
        if (*p == 'b')
        {
            hasBinary = true;
        }
        if (pos + 1 >= outSize)
        {
            out[0] = 0;
            return false;
        }
        out[pos++] = *p;
    }
    if (!hasBinary)
    {
        if (pos + 1 >= outSize)
        {
            out[0] = 0;
            return false;
        }
        out[pos++] = 'b';
    }
    out[pos] = 0;
    return pos > 0;
}

static const char* normalizedHostFileMode(
    const char* mode, char* buffer, size_t bufferSize)
{
    return normalizeHostFileMode(mode, buffer, bufferSize) ? buffer : NULL;
}

static const char* pathBaseName(const char* path)
{
    if (!path)
    {
        return NULL;
    }

    const char* base = path;
    for (const char* p = path; *p; ++p)
    {
        if (*p == '\\' || *p == '/' || *p == ':')
        {
            base = p + 1;
        }
    }
    return base;
}

static void normalizeGuestPath(const char* in, char* out, size_t outSize)
{
    if (!out || outSize == 0)
    {
        return;
    }

    out[0] = 0;
    if (!in)
    {
        return;
    }

    while (in[0] == '.' && (in[1] == '\\' || in[1] == '/'))
    {
        in += 2;
    }
    if (in[0] && in[1] == ':')
    {
        in += 2;
        while (*in == '\\' || *in == '/')
        {
            in++;
        }
    }
    while (*in == '\\' || *in == '/')
    {
        in++;
    }

    size_t pos = 0;
    while (*in && pos + 1 < outSize)
    {
        out[pos++] = (*in == '\\') ? '/' : *in;
        in++;
    }
    out[pos] = 0;
}

static FILE* openGuestFile(const char* name, const char* mode)
{
    char hostMode[16];
    const char* effectiveMode = normalizedHostFileMode(
        mode, hostMode, sizeof(hostMode));
    return name && effectiveMode ? platformOpenHostFile(name, effectiveMode) : NULL;
}

static FILE* tryOpenHostFile(const char* name, const char* mode,
    bool* saveTransaction, char* saveTransactionName,
    size_t saveTransactionNameSize)
{
    char normalizedMode[16];
    const char* effectiveMode = normalizedHostFileMode(
        mode, normalizedMode, sizeof(normalizedMode));
    if (!effectiveMode)
    {
        return NULL;
    }
    if (saveTransaction)
    {
        *saveTransaction = false;
    }
    if (saveTransactionName && saveTransactionNameSize)
    {
        saveTransactionName[0] = 0;
    }
    if (name && !s_saveDirectory.empty() && name[0] != '/' && name[0] != '\\')
    {
        char normalized[1024];
        normalizeGuestPath(name, normalized, sizeof(normalized));
        if (normalized[0])
        {
            bool transactional = false;
            FILE* saveFile = guestSaveOpenFile(
                s_saveDirectory, normalized, effectiveMode, &transactional);
            if (saveFile)
            {
                if (saveTransaction)
                {
                    *saveTransaction = transactional;
                }
                if (transactional && saveTransactionName && saveTransactionNameSize)
                {
                    snprintf(saveTransactionName, saveTransactionNameSize, "%s", normalized);
                }
                return saveFile;
            }
            if (modeAllowsWrites(effectiveMode))
            {
                return NULL;
            }
        }
    }

    FILE* fp = openGuestFile(name, effectiveMode);
    if (fp)
    {
        return fp;
    }

    char normalized[1024];
    normalizeGuestPath(name, normalized, sizeof(normalized));
    if (normalized[0] && strcmp(normalized, name) != 0)
    {
        fp = openGuestFile(normalized, effectiveMode);
        if (fp)
        {
            return fp;
        }
    }

    const char* base = pathBaseName(name);
    if (base && base[0] && strcmp(base, name) != 0)
    {
        fp = openGuestFile(base, effectiveMode);
        if (fp)
        {
            return fp;
        }
    }

    return NULL;
}

static bool shouldCacheHostFile(const char* name, const char* mode)
{
    if (modeAllowsWrites(mode))
    {
        return false;
    }

    const char* value = getenv("DINGOO_PIE_CACHE_HOST_FILES");
    if (value && value[0] && strcmp(value, "0") == 0)
    {
        return false;
    }

    const char* base = pathBaseName(name);
    const char* dot = base ? strrchr(base, '.') : NULL;
    if (!dot)
    {
        return false;
    }

    return _stricmp(dot, ".app") == 0 ||
        _stricmp(dot, ".war") == 0 ||
        _stricmp(dot, ".dat") == 0 ||
        _stricmp(dot, ".bin") == 0;
}

static void setVirtualFileRequestName(VirtualFileEntry* entry, const char* name)
{
    if (!entry)
    {
        return;
    }

    entry->requestName[0] = 0;
    if (!name)
    {
        return;
    }

    size_t length = strlen(name);
    if (length >= sizeof(entry->requestName))
    {
        length = sizeof(entry->requestName) - 1;
    }
    memcpy(entry->requestName, name, length);
    entry->requestName[length] = 0;
}

static bool hostFileMatchesGuestPackage(const uint8_t* data, uint32_t size)
{
    return s_guestPackage && data &&
        size == s_guestPackage->file_size &&
        memcmp(data, s_guestPackage->file_data, size) == 0;
}

static uint8_t* readWholeHostFile(FILE* fp, uint32_t* outSize)
{
    if (!fp || !outSize)
    {
        return NULL;
    }

    long original = ftell(fp);
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        return NULL;
    }
    long end = ftell(fp);
    if (end <= 0 || end > 0x7fffffffl)
    {
        if (original >= 0)
        {
            fseek(fp, original, SEEK_SET);
        }
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        return NULL;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)end);
    if (!data)
    {
        assert(0);
        return NULL;
    }
    size_t got = fread(data, 1, (size_t)end, fp);
    if (got != (size_t)end)
    {
        free(data);
        if (original >= 0)
        {
            fseek(fp, original, SEEK_SET);
        }
        return NULL;
    }

    *outSize = (uint32_t)end;
    if (original >= 0)
    {
        fseek(fp, original, SEEK_SET);
    }
    return data;
}

static GuestResourceEntry* tryOpenResource(const char* name)
{
    if (!s_guestPackage)
    {
        return NULL;
    }

    GuestResourceEntry* res = guestPackageFindResource(s_guestPackage, name);
    if (res)
    {
        return res;
    }

    char normalized[1024];
    normalizeGuestPath(name, normalized, sizeof(normalized));
    if (normalized[0] && strcmp(normalized, name) != 0)
    {
        res = guestPackageFindResource(s_guestPackage, normalized);
        if (res)
        {
            return res;
        }
    }

    const char* base = pathBaseName(name);
    if (base && base[0] && strcmp(base, name) != 0)
    {
        return guestPackageFindResource(s_guestPackage, base);
    }

    return NULL;
}

uint32_t fsys_fopen(const char* name, const char* mode)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_filesystemProfile.fopenCalls++;
    fsysProfileTick();

    if (name == NULL || mode == NULL)
    {
        return 0;
    }

    if (!modeAllowsWrites(mode) && s_guestPackage)
    {
        GuestResourceEntry* res = tryOpenResource(name);
        if (res && res->offset <= s_guestPackage->file_size &&
            res->size <= s_guestPackage->file_size - res->offset)
        {
            const uint8_t* data = guestPackageResourceData(s_guestPackage, res);
            if (!data)
            {
                return 0;
            }
            uint32_t index = allocateFileSlot();
            if (index == 0)
            {
                return 0;
            }
            s_fileMap[index].type = VIRTUAL_FILE_TYPE_RESOURCE;
            s_fileMap[index].data = data;
            s_fileMap[index].resource = res;
            s_fileMap[index].size = res->size;
            s_fileMap[index].offset = 0;
            s_fileMap[index].xorKey = 0;
            if (shouldUseBlockBreakerBinView(name, data, res->size))
            {
                uint32_t viewSize = 0;
                uint8_t* view = createBlockBreakerBinView(data, res->size, &viewSize);
                if (view)
                {
                    s_fileMap[index].data = view;
                    s_fileMap[index].ownedData = view;
                    s_fileMap[index].size = viewSize;
                    printf("fsys: applied Block Breaker .bin resource view name=%s original=0x%08x view=0x%08x\n",
                        name, res->size, viewSize);
                }
            }
            s_filesystemProfile.resourceOpens++;
            if (res->decoded_data || !res->xorKey)
            {
                s_filesystemProfile.resourceCachedOpens++;
            }
            if (runtimeResourceMonitorIsCapturing())
            {
                runtimeResourceMonitorRecordOpen(
                    RUNTIME_RESOURCE_MONITOR_SOURCE_FSYS,
                    name,
                    res,
                    res->decoded_data || !res->xorKey);
            }
            if (traceFsEnabled() || traceFsOpenEnabled())
            {
                printf("trace-fs: fopen name=%s mode=%s -> %u resource=%s offset=0x%08x size=0x%08x xor=0x%02x\n",
                    name, mode, index, res->name, res->offset, res->size, res->xorKey);
                guestPackageTraceResourceCandidates(s_guestPackage, name);
            }
            return index;
        }
    }

    bool saveTransaction = false;
    char saveTransactionName[1024] = {};
    FILE* fp = tryOpenHostFile(name, mode, &saveTransaction,
        saveTransactionName, sizeof(saveTransactionName));
    if (fp)
    {
        uint32_t index = allocateFileSlot();
        if (index == 0)
        {
            fclose(fp);
            return 0;
        }
        s_fileMap[index].type = VIRTUAL_FILE_TYPE_HOST;
        s_fileMap[index].saveTransaction = saveTransaction;
        snprintf(s_fileMap[index].saveTransactionName,
            sizeof(s_fileMap[index].saveTransactionName), "%s",
            saveTransactionName);
        s_fileMap[index].writable = modeAllowsWrites(mode);
        setVirtualFileRequestName(&s_fileMap[index], name);
        if (shouldCacheHostFile(name, mode))
        {
            uint32_t fileSize = 0;
            uint8_t* fileData = readWholeHostFile(fp, &fileSize);
            if (fileData)
            {
                s_fileMap[index].data = fileData;
                s_fileMap[index].ownedData = fileData;
                s_fileMap[index].size = fileSize;
                s_fileMap[index].offset = 0;
                s_fileMap[index].isGuestPackage =
                    hostFileMatchesGuestPackage(fileData, fileSize);
                fclose(fp);
                fp = NULL;
            }
        }
        s_fileMap[index].fp = fp;
        s_filesystemProfile.hostOpens++;
        if (traceFsEnabled() || traceFsOpenEnabled())
        {
            printf("trace-fs: fopen name=%s mode=%s -> %u host%s size=0x%08x\n",
                name, mode, index, s_fileMap[index].ownedData ? "-cached" : "",
                s_fileMap[index].size);
        }
        return index;
    }

    if (traceFsEnabled() || traceFsOpenEnabled())
    {
        printf("trace-fs: fopen name=%s mode=%s -> 0\n", name, mode);
    }
    recordOpenFailure(name, mode);
    return 0;
}

GuestResourceEntry* fsys_stream_resource(uint32_t stream)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return NULL;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    return entry->type == VIRTUAL_FILE_TYPE_RESOURCE ? entry->resource : NULL;
}

bool fsys_stream_is_guest_package(uint32_t stream)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return false;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    return entry->type == VIRTUAL_FILE_TYPE_HOST && entry->isGuestPackage;
}

bool fsys_stream_is_external_file(uint32_t stream)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return false;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    return entry->type == VIRTUAL_FILE_TYPE_HOST && !entry->isGuestPackage;
}

uint32_t fsys_stream_position(uint32_t stream)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return 0;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    if (entry->type == VIRTUAL_FILE_TYPE_HOST && !entry->ownedData && entry->fp)
    {
        long pos = ftell(entry->fp);
        return pos >= 0 ? (uint32_t)pos : 0;
    }
    return entry->offset;
}

const char* fsys_stream_request_name(uint32_t stream)
{
    static thread_local std::string requestName;
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        requestName.clear();
        return requestName.c_str();
    }

    requestName = s_fileMap[stream].requestName;
    return requestName.c_str();
}

void fsys_record_load_to_guest(
    uint32_t stream,
    uint32_t guestAddress,
    const void* hostData,
    uint32_t positionBefore)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    // Resource monitor rows represent bytes that have actually entered guest memory.
    if (!runtimeResourceMonitorIsCapturing() || !hostData)
    {
        return;
    }

    uint32_t positionAfter = fsys_stream_position(stream);
    uint32_t bytesLoaded = positionAfter >= positionBefore ?
        positionAfter - positionBefore : 0;
    if (bytesLoaded == 0)
    {
        return;
    }

    GuestResourceEntry* resource = fsys_stream_resource(stream);
    if (resource)
    {
        runtimeResourceMonitorRecordLoadContent(
            RUNTIME_RESOURCE_MONITOR_SOURCE_FSYS,
            resource,
            guestAddress,
            hostData,
            bytesLoaded,
            positionAfter);
    }
    else if (fsys_stream_is_guest_package(stream))
    {
        runtimeResourceMonitorRecordPackageLoadContent(
            fsys_stream_request_name(stream),
            positionBefore,
            guestAddress,
            hostData,
            bytesLoaded,
            positionAfter);
    }
    else if (fsys_stream_is_external_file(stream))
    {
        runtimeResourceMonitorRecordExternalLoadContent(
            fsys_stream_request_name(stream),
            positionBefore,
            guestAddress,
            hostData,
            bytesLoaded,
            positionAfter);
    }
}

static bool checkedReadSize(uint32_t size, uint32_t count, uint32_t* requested)
{
    if (!requested)
    {
        return false;
    }

    if (size == 0 || count == 0)
    {
        *requested = 0;
        return true;
    }

    if (count > UINT32_MAX / size)
    {
        return false;
    }

    *requested = size * count;
    return true;
}

uint32_t vm_fread(void* ptr, uint32_t size, uint32_t count, uint32_t stream)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_filesystemProfile.freadCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]) || ptr == NULL || size == 0 || count == 0)
    {
        return 0;
    }

    uint32_t requested = 0;
    if (!checkedReadSize(size, count, &requested))
    {
        return (uint32_t)-1;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    if (entry->type == VIRTUAL_FILE_TYPE_HOST)
    {
        if (entry->ownedData)
        {
            uint32_t available = (entry->offset < entry->size) ? (entry->size - entry->offset) : 0;
            uint32_t bytesToRead = requested < available ? requested : available;
            if (bytesToRead > 0)
            {
                memcpy(ptr, entry->data + entry->offset, bytesToRead);
                entry->offset += bytesToRead;
            }
            s_filesystemProfile.freadBytes += bytesToRead;
            s_filesystemProfile.hostReadCalls++;
            s_filesystemProfile.hostReadBytes += bytesToRead;
            if (fsysInFastHleCall())
            {
                s_filesystemProfile.fastFreadCalls++;
                s_filesystemProfile.fastFreadBytes += bytesToRead;
            }
            else
            {
                s_filesystemProfile.slowFreadCalls++;
                s_filesystemProfile.slowFreadBytes += bytesToRead;
            }
            uint32_t ret = bytesToRead / size;
            if (traceFsEnabled())
            {
                printf("trace-fs: fread stream=%u type=host-cached size=%u count=%u bytes=%u ret=%u offset=0x%08x\n",
                    stream, size, count, bytesToRead, ret, entry->offset);
            }
            return ret;
        }
        uint32_t ret = (uint32_t)fread(ptr, size, count, entry->fp);
        s_filesystemProfile.freadBytes += (uint64_t)ret * size;
        s_filesystemProfile.hostReadCalls++;
        s_filesystemProfile.hostReadBytes += (uint64_t)ret * size;
        if (fsysInFastHleCall())
        {
            s_filesystemProfile.fastFreadCalls++;
            s_filesystemProfile.fastFreadBytes += (uint64_t)ret * size;
        }
        else
        {
            s_filesystemProfile.slowFreadCalls++;
            s_filesystemProfile.slowFreadBytes += (uint64_t)ret * size;
        }
        if (traceFsEnabled())
        {
            printf("trace-fs: fread stream=%u type=host size=%u count=%u ret=%u\n",
                stream, size, count, ret);
        }
        return ret;
    }

    if (entry->type == VIRTUAL_FILE_TYPE_RESOURCE)
    {
        uint32_t available = (entry->offset < entry->size) ? (entry->size - entry->offset) : 0;
        uint32_t bytesToRead = requested < available ? requested : available;
        if (bytesToRead > 0)
        {
            memcpy(ptr, entry->data + entry->offset, bytesToRead);
            entry->offset += bytesToRead;
        }
        s_filesystemProfile.freadBytes += bytesToRead;
        s_filesystemProfile.resourceReadCalls++;
        s_filesystemProfile.resourceReadBytes += bytesToRead;
        if (fsysInFastHleCall())
        {
            s_filesystemProfile.fastFreadCalls++;
            s_filesystemProfile.fastFreadBytes += bytesToRead;
        }
        else
        {
            s_filesystemProfile.slowFreadCalls++;
            s_filesystemProfile.slowFreadBytes += bytesToRead;
        }
        uint32_t ret = bytesToRead / size;
        if (traceFsEnabled())
        {
            printf("trace-fs: fread stream=%u type=resource size=%u count=%u bytes=%u ret=%u offset=0x%08x\n",
                stream, size, count, bytesToRead, ret, entry->offset);
        }
        return ret;
    }

    return 0;
}

uint32_t fsys_fclose(uint32_t stream)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_filesystemProfile.fcloseCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return 0;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    uint32_t ret = 0;
    if (entry->ownedData)
    {
        free(entry->ownedData);
        entry->ownedData = NULL;
    }
    bool shouldRecordResourceEvent = runtimeResourceMonitorIsCapturing();
    if (entry->type == VIRTUAL_FILE_TYPE_RESOURCE)
    {
        if (shouldRecordResourceEvent)
        {
            runtimeResourceMonitorRecordClose(
                RUNTIME_RESOURCE_MONITOR_SOURCE_FSYS,
                entry->resource);
        }
    }
    else if (entry->type == VIRTUAL_FILE_TYPE_HOST)
    {
        if (shouldRecordResourceEvent && entry->isGuestPackage)
        {
            runtimeResourceMonitorRecordPackageClose(entry->requestName);
        }
        else if (shouldRecordResourceEvent)
        {
            runtimeResourceMonitorRecordExternalClose(entry->requestName);
        }
    }
    if (entry->type == VIRTUAL_FILE_TYPE_HOST && entry->fp)
    {
        ret = guestSaveCloseFile(s_saveDirectory,
            entry->saveTransactionName, entry->fp, entry->saveTransaction);
        if (ret == 0 && entry->writable && entry->wroteData)
        {
            s_successfulSaveWrite.store(true);
        }
    }

    if (traceFsEnabled() || traceFsOpenEnabled())
    {
        printf("trace-fs: fclose stream=%u type=%s ret=%u\n", stream, virtualFileTypeName(entry->type), ret);
    }
    memset(entry, 0x00, sizeof(*entry));
    return ret;
}

uint32_t fsys_fseek(uint32_t stream, uint32_t offset, uint32_t origin)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_filesystemProfile.fseekCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return 0;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    if (entry->type == VIRTUAL_FILE_TYPE_HOST)
    {
        if (entry->ownedData)
        {
            int64_t base = 0;
            if (origin == 0)
            {
                base = 0;
            }
            else if (origin == 1)
            {
                base = entry->offset;
            }
            else if (origin == 2)
            {
                base = entry->size;
            }
            else
            {
                return (uint32_t)-1;
            }

            int64_t next = base + (int32_t)offset;
            if (next < 0 || next > entry->size)
            {
                return (uint32_t)-1;
            }
            entry->offset = (uint32_t)next;
            s_filesystemProfile.hostSeekCalls++;
            if (fsysInFastHleCall())
            {
                s_filesystemProfile.fastFseekCalls++;
            }
            else
            {
                s_filesystemProfile.slowFseekCalls++;
            }
            if (traceFsEnabled())
            {
                printf("trace-fs: fseek stream=%u type=host-cached offset=%d origin=%u ret=0 next=0x%08x\n",
                    stream, (int32_t)offset, origin, entry->offset);
            }
            return 0;
        }
        uint32_t ret = fseek(entry->fp, (long)(int32_t)offset, (int)origin);
        s_filesystemProfile.hostSeekCalls++;
        if (fsysInFastHleCall())
        {
            s_filesystemProfile.fastFseekCalls++;
        }
        else
        {
            s_filesystemProfile.slowFseekCalls++;
        }
        if (traceFsEnabled())
        {
            printf("trace-fs: fseek stream=%u type=host offset=%d origin=%u ret=%u\n",
                stream, (int32_t)offset, origin, ret);
        }
        return ret;
    }

    if (entry->type == VIRTUAL_FILE_TYPE_RESOURCE)
    {
        int64_t base = 0;
        if (origin == 0)
        {
            base = 0;
        }
        else if (origin == 1)
        {
            base = entry->offset;
        }
        else if (origin == 2)
        {
            base = entry->size;
        }
        else
        {
            return (uint32_t)-1;
        }

        int64_t next = base + (int32_t)offset;
        if (next < 0 || next > entry->size)
        {
            return (uint32_t)-1;
        }
        entry->offset = (uint32_t)next;
        s_filesystemProfile.resourceSeekCalls++;
        if (runtimeResourceMonitorIsCapturing())
        {
            runtimeResourceMonitorRecordSeek(
                RUNTIME_RESOURCE_MONITOR_SOURCE_FSYS,
                entry->resource,
                entry->offset);
        }
        if (fsysInFastHleCall())
        {
            s_filesystemProfile.fastFseekCalls++;
        }
        else
        {
            s_filesystemProfile.slowFseekCalls++;
        }
        if (traceFsEnabled())
        {
            printf("trace-fs: fseek stream=%u type=resource offset=%d origin=%u ret=0 next=0x%08x\n",
                stream, (int32_t)offset, origin, entry->offset);
        }
        return 0;
    }

    return 0;
}

static bool vfileSeekMemory(VirtualFileEntry* entry, uint32_t offset, uint32_t origin, uint32_t* ret)
{
    if (!entry || !ret)
    {
        return false;
    }

    int64_t base = 0;
    if (origin == 0)
    {
        base = 0;
    }
    else if (origin == 1)
    {
        base = entry->offset;
    }
    else if (origin == 2)
    {
        base = entry->size;
    }
    else
    {
        *ret = (uint32_t)-1;
        return true;
    }

    int64_t next = base + (int32_t)offset;
    if (next < 0 || next > entry->size)
    {
        *ret = (uint32_t)-1;
        return true;
    }

    entry->offset = (uint32_t)next;
    *ret = 0;
    return true;
}

bool fsys_seek_cached(uint32_t stream, uint32_t offset, uint32_t origin, uint32_t* ret)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    if (!ret || stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return false;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    if ((entry->type == VIRTUAL_FILE_TYPE_HOST && entry->ownedData) || entry->type == VIRTUAL_FILE_TYPE_RESOURCE)
    {
        s_filesystemProfile.fseekCalls++;
        fsysProfileTick();
        bool ok = vfileSeekMemory(entry, offset, origin, ret);
        if (ok && *ret == 0)
        {
            if (entry->type == VIRTUAL_FILE_TYPE_RESOURCE)
            {
                s_filesystemProfile.resourceSeekCalls++;
                if (runtimeResourceMonitorIsCapturing())
                {
                    runtimeResourceMonitorRecordSeek(
                        RUNTIME_RESOURCE_MONITOR_SOURCE_FSYS,
                        entry->resource,
                        entry->offset);
                }
            }
            else
            {
                s_filesystemProfile.hostSeekCalls++;
            }
            if (fsysInFastHleCall())
            {
                s_filesystemProfile.fastFseekCalls++;
            }
            else
            {
                s_filesystemProfile.slowFseekCalls++;
            }
        }
        if (traceFsEnabled())
        {
            printf("trace-fs: cached-fseek stream=%u type=%s offset=%d origin=%u ret=%u next=0x%08x\n",
                stream, virtualFileTypeName(entry->type), (int32_t)offset, origin, *ret, entry->offset);
        }
        return ok;
    }

    return false;
}

bool fsys_read_cached(uint32_t stream, uint32_t size, uint32_t count, const uint8_t** data, uint32_t* bytesRead, uint32_t* itemsRead)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    if (!data || !bytesRead || !itemsRead ||
        stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]) ||
        size == 0 || count == 0)
    {
        return false;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    if (!((entry->type == VIRTUAL_FILE_TYPE_HOST && entry->ownedData) || entry->type == VIRTUAL_FILE_TYPE_RESOURCE))
    {
        return false;
    }

    uint32_t requested = 0;
    if (!checkedReadSize(size, count, &requested))
    {
        return false;
    }

    uint32_t available = (entry->offset < entry->size) ? (entry->size - entry->offset) : 0;
    uint32_t copySize = requested < available ? requested : available;
    *data = entry->data + entry->offset;
    *bytesRead = copySize;
    *itemsRead = copySize / size;
    entry->offset += copySize;

    s_filesystemProfile.freadCalls++;
    fsysProfileTick();
    s_filesystemProfile.freadBytes += copySize;
    if (entry->type == VIRTUAL_FILE_TYPE_RESOURCE)
    {
        s_filesystemProfile.resourceReadCalls++;
        s_filesystemProfile.resourceReadBytes += copySize;
    }
    else
    {
        s_filesystemProfile.hostReadCalls++;
        s_filesystemProfile.hostReadBytes += copySize;
    }
    if (fsysInFastHleCall())
    {
        s_filesystemProfile.fastFreadCalls++;
        s_filesystemProfile.fastFreadBytes += copySize;
    }
    else
    {
        s_filesystemProfile.slowFreadCalls++;
        s_filesystemProfile.slowFreadBytes += copySize;
    }
    if (traceFsEnabled())
    {
        printf("trace-fs: cached-fread stream=%u type=%s size=%u count=%u bytes=%u ret=%u offset=0x%08x\n",
            stream, virtualFileTypeName(entry->type), size, count, copySize, *itemsRead, entry->offset);
    }
    return true;
}

uint32_t fsys_ftell(uint32_t stream)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_filesystemProfile.ftellCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return 0;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    if (entry->type == VIRTUAL_FILE_TYPE_HOST)
    {
        if (entry->ownedData)
        {
            return entry->offset;
        }
        return (uint32_t)ftell(entry->fp);
    }
    if (entry->type == VIRTUAL_FILE_TYPE_RESOURCE)
    {
        return entry->offset;
    }
    return 0;
}

uint32_t fsys_fwrite(void* ptr, uint32_t size, uint32_t count, uint32_t stream)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return 0;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    if (entry->type == VIRTUAL_FILE_TYPE_HOST)
    {
        uint32_t written = (uint32_t)fwrite(ptr, size, count, entry->fp);
        if (written > 0)
        {
            entry->wroteData = true;
        }
        return written;
    }

    return 0;
}

uint32_t fsys_feof(uint32_t stream)
{
    std::lock_guard<std::recursive_mutex> lock(s_filesystemMutex);
    s_filesystemProfile.feofCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_fileMap) / sizeof(s_fileMap[0]))
    {
        return 0;
    }

    VirtualFileEntry* entry = &s_fileMap[stream];
    if (entry->type == VIRTUAL_FILE_TYPE_HOST)
    {
        if (entry->ownedData)
        {
            return entry->offset >= entry->size;
        }
        return (uint32_t)feof(entry->fp);
    }
    if (entry->type == VIRTUAL_FILE_TYPE_RESOURCE)
    {
        return entry->offset >= entry->size;
    }
    return 0;
}

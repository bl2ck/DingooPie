#include "guest_filesystem.h"
#include "compat_profile.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <atomic>

typedef enum {
    vfile_type_none,
    vfile_type_host,
    vfile_type_resource
} vfile_type_e;

typedef struct {
    vfile_type_e type;
    FILE* fp;
    const uint8_t* data;
    uint8_t* ownedData;
    app_resource_entry* resource;
    uint32_t size;
    uint32_t offset;
    uint8_t xor_key;
} vfile_entry_t;

static vfile_entry_t s_FILE_Map[128];
static app* s_fsys_app = NULL;
static const char* s_fsys_app_sha256 = "";

typedef struct {
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
} fsys_profile_t;

static fsys_profile_t s_fsys_profile = {};
static thread_local int s_fastHleCallDepth = 0;
static std::atomic<bool> s_fsysProfileEnabled(false);

static uint64_t fsysNowMs(void)
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static bool fsysProfileEnabled(void)
{
    return s_fsysProfileEnabled.load();
}

void fsys_set_profile_enabled(bool enabled)
{
    s_fsysProfileEnabled.store(enabled);
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
    if (now - lastTicks < 1000)
    {
        return;
    }

    printf("profile fsys: fopen=%llu host=%llu resource=%llu cached=%llu fread=%llu/%llub fseek=%llu fast=%llu/%llub/%llu slow=%llu/%llub/%llu host_io=%llu/%llub/%llu resource_io=%llu/%llub/%llu ftell=%llu feof=%llu fclose=%llu\n",
        (unsigned long long)s_fsys_profile.fopenCalls,
        (unsigned long long)s_fsys_profile.hostOpens,
        (unsigned long long)s_fsys_profile.resourceOpens,
        (unsigned long long)s_fsys_profile.resourceCachedOpens,
        (unsigned long long)s_fsys_profile.freadCalls,
        (unsigned long long)s_fsys_profile.freadBytes,
        (unsigned long long)s_fsys_profile.fseekCalls,
        (unsigned long long)s_fsys_profile.fastFreadCalls,
        (unsigned long long)s_fsys_profile.fastFreadBytes,
        (unsigned long long)s_fsys_profile.fastFseekCalls,
        (unsigned long long)s_fsys_profile.slowFreadCalls,
        (unsigned long long)s_fsys_profile.slowFreadBytes,
        (unsigned long long)s_fsys_profile.slowFseekCalls,
        (unsigned long long)s_fsys_profile.hostReadCalls,
        (unsigned long long)s_fsys_profile.hostReadBytes,
        (unsigned long long)s_fsys_profile.hostSeekCalls,
        (unsigned long long)s_fsys_profile.resourceReadCalls,
        (unsigned long long)s_fsys_profile.resourceReadBytes,
        (unsigned long long)s_fsys_profile.resourceSeekCalls,
        (unsigned long long)s_fsys_profile.ftellCalls,
        (unsigned long long)s_fsys_profile.feofCalls,
        (unsigned long long)s_fsys_profile.fcloseCalls);

    memset(&s_fsys_profile, 0x00, sizeof(s_fsys_profile));
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

static const char* vfileTypeName(vfile_type_e type)
{
    switch (type)
    {
    case vfile_type_host:
        return "host";
    case vfile_type_resource:
        return "resource";
    default:
        return "none";
    }
}

void fsys_set_app(app* inApp)
{
    s_fsys_app = inApp;
}

void fsys_set_app_identity(const char* sha256Hex)
{
    s_fsys_app_sha256 = sha256Hex ? sha256Hex : "";
}

static bool isBlockBreakerApp(void)
{
    return compatShouldUseBinResourceView(s_fsys_app_sha256);
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

static uint32_t alloc_file_slot(void)
{
    for (uint32_t index = 1; index < sizeof(s_FILE_Map) / sizeof(s_FILE_Map[0]); ++index)
    {
        if (s_FILE_Map[index].type == vfile_type_none)
        {
            return index;
        }
    }

    printf("Failed s_FILE_Map with error : %u\n", errno);
    assert(0);
    return 0;
}

static int mode_writes(const char* mode)
{
    return mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'));
}

static bool normalize_host_file_mode(const char* mode, char* out, size_t outSize)
{
    if (!mode || !out || outSize == 0)
    {
        return false;
    }

    bool changed = false;
    size_t pos = 0;
    for (const char* p = mode; *p; ++p)
    {
        if (*p == 's')
        {
            changed = true;
            continue;
        }
        if (pos + 1 >= outSize)
        {
            out[0] = 0;
            return false;
        }
        out[pos++] = *p;
    }
    out[pos] = 0;
    return changed && pos > 0;
}

static const char* path_basename(const char* path)
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

static void normalize_guest_path(const char* in, char* out, size_t outSize)
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

static FILE* fopen_guest_mode(const char* name, const char* mode)
{
    FILE* fp = fopen(name, mode);
    if (fp)
    {
        return fp;
    }

    char hostMode[16];
    if (normalize_host_file_mode(mode, hostMode, sizeof(hostMode)))
    {
        fp = fopen(name, hostMode);
    }
    return fp;
}

static FILE* try_host_open(const char* name, const char* mode)
{
    FILE* fp = fopen_guest_mode(name, mode);
    if (fp)
    {
        return fp;
    }

    char normalized[1024];
    normalize_guest_path(name, normalized, sizeof(normalized));
    if (normalized[0] && strcmp(normalized, name) != 0)
    {
        fp = fopen_guest_mode(normalized, mode);
        if (fp)
        {
            return fp;
        }
    }

    const char* base = path_basename(name);
    if (base && base[0] && strcmp(base, name) != 0)
    {
        fp = fopen_guest_mode(base, mode);
        if (fp)
        {
            return fp;
        }
    }

    return NULL;
}

static bool shouldCacheHostFile(const char* name, const char* mode)
{
    if (mode_writes(mode))
    {
        return false;
    }

    const char* value = getenv("DINGOO_PIE_CACHE_HOST_FILES");
    if (value && value[0] && strcmp(value, "0") == 0)
    {
        return false;
    }

    const char* base = path_basename(name);
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

static app_resource_entry* try_resource_open(const char* name)
{
    if (!s_fsys_app)
    {
        return NULL;
    }

    app_resource_entry* res = app_find_resource(s_fsys_app, name);
    if (res)
    {
        return res;
    }

    char normalized[1024];
    normalize_guest_path(name, normalized, sizeof(normalized));
    if (normalized[0] && strcmp(normalized, name) != 0)
    {
        res = app_find_resource(s_fsys_app, normalized);
        if (res)
        {
            return res;
        }
    }

    const char* base = path_basename(name);
    if (base && base[0] && strcmp(base, name) != 0)
    {
        return app_find_resource(s_fsys_app, base);
    }

    return NULL;
}

uint32_t fsys_fopen(const char* name, const char* mode)
{
    s_fsys_profile.fopenCalls++;
    fsysProfileTick();

    if (name == NULL || mode == NULL)
    {
        return 0;
    }

    if (!mode_writes(mode) && s_fsys_app)
    {
        app_resource_entry* res = try_resource_open(name);
        if (res && res->offset <= s_fsys_app->file_size && res->size <= s_fsys_app->file_size - res->offset)
        {
            const uint8_t* data = app_resource_data(s_fsys_app, res);
            if (!data)
            {
                return 0;
            }
            uint32_t index = alloc_file_slot();
            s_FILE_Map[index].type = vfile_type_resource;
            s_FILE_Map[index].data = data;
            s_FILE_Map[index].resource = res;
            s_FILE_Map[index].size = res->size;
            s_FILE_Map[index].offset = 0;
            s_FILE_Map[index].xor_key = 0;
            if (shouldUseBlockBreakerBinView(name, data, res->size))
            {
                uint32_t viewSize = 0;
                uint8_t* view = createBlockBreakerBinView(data, res->size, &viewSize);
                if (view)
                {
                    s_FILE_Map[index].data = view;
                    s_FILE_Map[index].ownedData = view;
                    s_FILE_Map[index].size = viewSize;
                    printf("fsys: applied Block Breaker .bin resource view name=%s original=0x%08x view=0x%08x\n",
                        name, res->size, viewSize);
                }
            }
            s_fsys_profile.resourceOpens++;
            if (res->decoded_data || !res->xor_key)
            {
                s_fsys_profile.resourceCachedOpens++;
            }
        if (traceFsEnabled() || traceFsOpenEnabled())
        {
            printf("trace-fs: fopen name=%s mode=%s -> %u resource=%s offset=0x%08x size=0x%08x xor=0x%02x\n",
                name, mode, index, res->name, res->offset, res->size, res->xor_key);
            app_trace_resource_candidates(s_fsys_app, name);
            }
            return index;
        }
    }

    FILE* fp = try_host_open(name, mode);
    if (fp)
    {
        uint32_t index = alloc_file_slot();
        s_FILE_Map[index].type = vfile_type_host;
        if (shouldCacheHostFile(name, mode))
        {
            uint32_t fileSize = 0;
            uint8_t* fileData = readWholeHostFile(fp, &fileSize);
            if (fileData)
            {
                s_FILE_Map[index].data = fileData;
                s_FILE_Map[index].ownedData = fileData;
                s_FILE_Map[index].size = fileSize;
                s_FILE_Map[index].offset = 0;
                fclose(fp);
                fp = NULL;
            }
        }
        s_FILE_Map[index].fp = fp;
        s_fsys_profile.hostOpens++;
        if (traceFsEnabled() || traceFsOpenEnabled())
        {
            printf("trace-fs: fopen name=%s mode=%s -> %u host%s size=0x%08x\n",
                name, mode, index, s_FILE_Map[index].ownedData ? "-cached" : "",
                s_FILE_Map[index].size);
        }
        return index;
    }

    if (traceFsEnabled() || traceFsOpenEnabled())
    {
        printf("trace-fs: fopen name=%s mode=%s -> 0\n", name, mode);
    }
    return 0;
}

uint32_t vm_fread(void* ptr, uint32_t size, uint32_t count, uint32_t stream)
{
    s_fsys_profile.freadCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_FILE_Map) / sizeof(s_FILE_Map[0]) || ptr == NULL || size == 0 || count == 0)
    {
        return 0;
    }

    vfile_entry_t* entry = &s_FILE_Map[stream];
    if (entry->type == vfile_type_host)
    {
        if (entry->ownedData)
        {
            uint32_t requested = size * count;
            uint32_t available = (entry->offset < entry->size) ? (entry->size - entry->offset) : 0;
            uint32_t bytesToRead = requested < available ? requested : available;
            if (bytesToRead > 0)
            {
                memcpy(ptr, entry->data + entry->offset, bytesToRead);
                entry->offset += bytesToRead;
            }
            s_fsys_profile.freadBytes += bytesToRead;
            s_fsys_profile.hostReadCalls++;
            s_fsys_profile.hostReadBytes += bytesToRead;
            if (fsysInFastHleCall())
            {
                s_fsys_profile.fastFreadCalls++;
                s_fsys_profile.fastFreadBytes += bytesToRead;
            }
            else
            {
                s_fsys_profile.slowFreadCalls++;
                s_fsys_profile.slowFreadBytes += bytesToRead;
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
        s_fsys_profile.freadBytes += (uint64_t)ret * size;
        s_fsys_profile.hostReadCalls++;
        s_fsys_profile.hostReadBytes += (uint64_t)ret * size;
        if (fsysInFastHleCall())
        {
            s_fsys_profile.fastFreadCalls++;
            s_fsys_profile.fastFreadBytes += (uint64_t)ret * size;
        }
        else
        {
            s_fsys_profile.slowFreadCalls++;
            s_fsys_profile.slowFreadBytes += (uint64_t)ret * size;
        }
        if (traceFsEnabled())
        {
            printf("trace-fs: fread stream=%u type=host size=%u count=%u ret=%u\n",
                stream, size, count, ret);
        }
        return ret;
    }

    if (entry->type == vfile_type_resource)
    {
        uint32_t requested = size * count;
        uint32_t available = (entry->offset < entry->size) ? (entry->size - entry->offset) : 0;
        uint32_t bytesToRead = requested < available ? requested : available;
        if (bytesToRead > 0)
        {
            memcpy(ptr, entry->data + entry->offset, bytesToRead);
            entry->offset += bytesToRead;
        }
        s_fsys_profile.freadBytes += bytesToRead;
        s_fsys_profile.resourceReadCalls++;
        s_fsys_profile.resourceReadBytes += bytesToRead;
        if (fsysInFastHleCall())
        {
            s_fsys_profile.fastFreadCalls++;
            s_fsys_profile.fastFreadBytes += bytesToRead;
        }
        else
        {
            s_fsys_profile.slowFreadCalls++;
            s_fsys_profile.slowFreadBytes += bytesToRead;
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
    s_fsys_profile.fcloseCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_FILE_Map) / sizeof(s_FILE_Map[0]))
    {
        return 0;
    }

    vfile_entry_t* entry = &s_FILE_Map[stream];
    uint32_t ret = 0;
    if (entry->ownedData)
    {
        free(entry->ownedData);
        entry->ownedData = NULL;
    }
    if (entry->type == vfile_type_host && entry->fp)
    {
        ret = fclose(entry->fp);
    }

    if (traceFsEnabled() || traceFsOpenEnabled())
    {
        printf("trace-fs: fclose stream=%u type=%s ret=%u\n", stream, vfileTypeName(entry->type), ret);
    }
    memset(entry, 0x00, sizeof(*entry));
    return ret;
}

uint32_t fsys_fseek(uint32_t stream, uint32_t offset, uint32_t origin)
{
    s_fsys_profile.fseekCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_FILE_Map) / sizeof(s_FILE_Map[0]))
    {
        return 0;
    }

    vfile_entry_t* entry = &s_FILE_Map[stream];
    if (entry->type == vfile_type_host)
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
            s_fsys_profile.hostSeekCalls++;
            if (fsysInFastHleCall())
            {
                s_fsys_profile.fastFseekCalls++;
            }
            else
            {
                s_fsys_profile.slowFseekCalls++;
            }
            if (traceFsEnabled())
            {
                printf("trace-fs: fseek stream=%u type=host-cached offset=%d origin=%u ret=0 next=0x%08x\n",
                    stream, (int32_t)offset, origin, entry->offset);
            }
            return 0;
        }
        uint32_t ret = fseek(entry->fp, (long)(int32_t)offset, (int)origin);
        s_fsys_profile.hostSeekCalls++;
        if (fsysInFastHleCall())
        {
            s_fsys_profile.fastFseekCalls++;
        }
        else
        {
            s_fsys_profile.slowFseekCalls++;
        }
        if (traceFsEnabled())
        {
            printf("trace-fs: fseek stream=%u type=host offset=%d origin=%u ret=%u\n",
                stream, (int32_t)offset, origin, ret);
        }
        return ret;
    }

    if (entry->type == vfile_type_resource)
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
        s_fsys_profile.resourceSeekCalls++;
        if (fsysInFastHleCall())
        {
            s_fsys_profile.fastFseekCalls++;
        }
        else
        {
            s_fsys_profile.slowFseekCalls++;
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

static bool vfileSeekMemory(vfile_entry_t* entry, uint32_t offset, uint32_t origin, uint32_t* ret)
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
    if (!ret || stream == 0 || stream >= sizeof(s_FILE_Map) / sizeof(s_FILE_Map[0]))
    {
        return false;
    }

    vfile_entry_t* entry = &s_FILE_Map[stream];
    if ((entry->type == vfile_type_host && entry->ownedData) || entry->type == vfile_type_resource)
    {
        s_fsys_profile.fseekCalls++;
        fsysProfileTick();
        bool ok = vfileSeekMemory(entry, offset, origin, ret);
        if (ok && *ret == 0)
        {
            if (entry->type == vfile_type_resource)
            {
                s_fsys_profile.resourceSeekCalls++;
            }
            else
            {
                s_fsys_profile.hostSeekCalls++;
            }
            if (fsysInFastHleCall())
            {
                s_fsys_profile.fastFseekCalls++;
            }
            else
            {
                s_fsys_profile.slowFseekCalls++;
            }
        }
        if (traceFsEnabled())
        {
            printf("trace-fs: cached-fseek stream=%u type=%s offset=%d origin=%u ret=%u next=0x%08x\n",
                stream, vfileTypeName(entry->type), (int32_t)offset, origin, *ret, entry->offset);
        }
        return ok;
    }

    return false;
}

bool fsys_read_cached(uint32_t stream, uint32_t size, uint32_t count, const uint8_t** data, uint32_t* bytesRead, uint32_t* itemsRead)
{
    if (!data || !bytesRead || !itemsRead ||
        stream == 0 || stream >= sizeof(s_FILE_Map) / sizeof(s_FILE_Map[0]) ||
        size == 0 || count == 0)
    {
        return false;
    }

    vfile_entry_t* entry = &s_FILE_Map[stream];
    if (!((entry->type == vfile_type_host && entry->ownedData) || entry->type == vfile_type_resource))
    {
        return false;
    }

    uint32_t requested = size * count;
    if (size != 0 && requested / size != count)
    {
        return false;
    }

    uint32_t available = (entry->offset < entry->size) ? (entry->size - entry->offset) : 0;
    uint32_t copySize = requested < available ? requested : available;
    *data = entry->data + entry->offset;
    *bytesRead = copySize;
    *itemsRead = copySize / size;
    entry->offset += copySize;

    s_fsys_profile.freadCalls++;
    fsysProfileTick();
    s_fsys_profile.freadBytes += copySize;
    if (entry->type == vfile_type_resource)
    {
        s_fsys_profile.resourceReadCalls++;
        s_fsys_profile.resourceReadBytes += copySize;
    }
    else
    {
        s_fsys_profile.hostReadCalls++;
        s_fsys_profile.hostReadBytes += copySize;
    }
    if (fsysInFastHleCall())
    {
        s_fsys_profile.fastFreadCalls++;
        s_fsys_profile.fastFreadBytes += copySize;
    }
    else
    {
        s_fsys_profile.slowFreadCalls++;
        s_fsys_profile.slowFreadBytes += copySize;
    }
    if (traceFsEnabled())
    {
        printf("trace-fs: cached-fread stream=%u type=%s size=%u count=%u bytes=%u ret=%u offset=0x%08x\n",
            stream, vfileTypeName(entry->type), size, count, copySize, *itemsRead, entry->offset);
    }
    return true;
}

uint32_t fsys_ftell(uint32_t stream)
{
    s_fsys_profile.ftellCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_FILE_Map) / sizeof(s_FILE_Map[0]))
    {
        return 0;
    }

    vfile_entry_t* entry = &s_FILE_Map[stream];
    if (entry->type == vfile_type_host)
    {
        if (entry->ownedData)
        {
            return entry->offset;
        }
        return (uint32_t)ftell(entry->fp);
    }
    if (entry->type == vfile_type_resource)
    {
        return entry->offset;
    }
    return 0;
}

uint32_t fsys_fwrite(void* ptr, uint32_t size, uint32_t count, uint32_t stream)
{
    if (stream == 0 || stream >= sizeof(s_FILE_Map) / sizeof(s_FILE_Map[0]))
    {
        return 0;
    }

    vfile_entry_t* entry = &s_FILE_Map[stream];
    if (entry->type == vfile_type_host)
    {
        return (uint32_t)fwrite(ptr, size, count, entry->fp);
    }

    return 0;
}

uint32_t fsys_feof(uint32_t stream)
{
    s_fsys_profile.feofCalls++;
    fsysProfileTick();

    if (stream == 0 || stream >= sizeof(s_FILE_Map) / sizeof(s_FILE_Map[0]))
    {
        return 0;
    }

    vfile_entry_t* entry = &s_FILE_Map[stream];
    if (entry->type == vfile_type_host)
    {
        if (entry->ownedData)
        {
            return entry->offset >= entry->size;
        }
        return (uint32_t)feof(entry->fp);
    }
    if (entry->type == vfile_type_resource)
    {
        return entry->offset >= entry->size;
    }
    return 0;
}


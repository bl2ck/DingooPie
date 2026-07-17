#include "runtime_resource_monitor.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>

static pthread_mutex_t g_resourceMonitorMutex = PTHREAD_MUTEX_INITIALIZER;
static RuntimeResourceMonitorSnapshot g_resourceMonitorSnapshot;
static std::atomic<bool> g_resourceMonitorActive(false);
static const uint32_t kResourceMonitorPreviewBytes = 32;

struct RuntimePackageResourceLookup
{
    std::string name;
    uint32_t offset;
    uint32_t size;
};

static std::vector<RuntimePackageResourceLookup> g_packageResourceLookup;

static bool resourceMonitorAutotestEnabled(void)
{
    static const bool enabled = []() {
        const char* value = getenv("DINGOO_PIE_RESOURCE_MONITOR_AUTOTEST");
        return value && value[0] && value[0] != '0';
    }();
    return enabled;
}

static bool resourceMonitorCaptureEnabled(void)
{
    return g_resourceMonitorActive.load(std::memory_order_acquire) ||
        resourceMonitorAutotestEnabled();
}

static const char* monitorSourceName(RuntimeResourceMonitorSource source)
{
    switch (source)
    {
    case RUNTIME_RESOURCE_MONITOR_SOURCE_DL_RES:
        return "dl_res";
    default:
        return "fsys";
    }
}

static RuntimeResourceMonitorEntry* findMonitorEntryLocked(const app_resource_entry* entry)
{
    if (!entry || !entry->name || !entry->name[0])
    {
        return NULL;
    }

    for (size_t i = 0; i < g_resourceMonitorSnapshot.entries.size(); ++i)
    {
        RuntimeResourceMonitorEntry& item = g_resourceMonitorSnapshot.entries[i];
        if (!item.appPackageSeen &&
            !item.externalFileSeen &&
            item.name == entry->name &&
            item.offset == entry->offset &&
            item.size == entry->size)
        {
            return &item;
        }
    }
    return NULL;
}

static RuntimeResourceMonitorEntry* ensureMonitorEntryLocked(const app_resource_entry* entry)
{
    if (!entry || !entry->name || !entry->name[0])
    {
        return NULL;
    }

    RuntimeResourceMonitorEntry* found = findMonitorEntryLocked(entry);
    if (found)
    {
        return found;
    }

    RuntimeResourceMonitorEntry item = {};
    item.name = entry->name;
    item.offset = entry->offset;
    item.size = entry->size;
    item.xorKey = entry->xor_key;
    item.firstRevision = g_resourceMonitorSnapshot.revision + 1;
    g_resourceMonitorSnapshot.entries.push_back(item);
    return &g_resourceMonitorSnapshot.entries.back();
}

static RuntimeResourceMonitorEntry* findPackageMonitorEntryLocked(
    uint32_t packageOffset,
    uint32_t size)
{
    for (size_t i = 0; i < g_resourceMonitorSnapshot.entries.size(); ++i)
    {
        RuntimeResourceMonitorEntry& item = g_resourceMonitorSnapshot.entries[i];
        if (item.appPackageSeen &&
            item.offset == packageOffset &&
            item.size == size)
        {
            return &item;
        }
    }
    return NULL;
}

static const RuntimePackageResourceLookup* findPackageResourceLocked(
    uint32_t packageOffset,
    uint32_t bytesRead)
{
    size_t low = 0;
    size_t high = g_packageResourceLookup.size();
    while (low < high)
    {
        size_t mid = low + (high - low) / 2;
        if (g_packageResourceLookup[mid].offset <= packageOffset)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }

    if (low == 0)
    {
        return NULL;
    }

    const RuntimePackageResourceLookup* item = &g_packageResourceLookup[low - 1];
    uint64_t itemEnd = (uint64_t)item->offset + item->size;
    uint64_t readEnd = (uint64_t)packageOffset + bytesRead;
    if ((uint64_t)packageOffset >= item->offset && readEnd <= itemEnd)
    {
        return item;
    }
    return NULL;
}

static RuntimeResourceMonitorEntry* ensurePackageMonitorEntryLocked(
    uint32_t packageOffset,
    uint32_t bytesRead)
{
    const RuntimePackageResourceLookup* packageResource =
        findPackageResourceLocked(packageOffset, bytesRead);
    uint32_t entryOffset = packageResource ? packageResource->offset : packageOffset;
    uint32_t entrySize = packageResource ? packageResource->size : bytesRead;
    RuntimeResourceMonitorEntry* found =
        findPackageMonitorEntryLocked(entryOffset, entrySize);
    if (found)
    {
        return found;
    }

    char name[48] = {};
    snprintf(name, sizeof(name), "app+0x%08x", packageOffset);

    RuntimeResourceMonitorEntry item = {};
    item.name = packageResource ? packageResource->name : name;
    item.offset = entryOffset;
    item.size = entrySize;
    item.firstRevision = g_resourceMonitorSnapshot.revision + 1;
    item.appPackageSeen = true;
    item.cached = true;
    g_resourceMonitorSnapshot.entries.push_back(item);
    return &g_resourceMonitorSnapshot.entries.back();
}

static RuntimeResourceMonitorEntry* findExternalMonitorEntryLocked(
    const char* requestName,
    uint32_t fileOffset,
    uint32_t size)
{
    const char* name = requestName ? requestName : "";
    for (size_t i = 0; i < g_resourceMonitorSnapshot.entries.size(); ++i)
    {
        RuntimeResourceMonitorEntry& item = g_resourceMonitorSnapshot.entries[i];
        if (item.externalFileSeen &&
            item.name == name &&
            item.offset == fileOffset &&
            item.size == size)
        {
            return &item;
        }
    }
    return NULL;
}

static RuntimeResourceMonitorEntry* ensureExternalMonitorEntryLocked(
    const char* requestName,
    uint32_t fileOffset,
    uint32_t size)
{
    RuntimeResourceMonitorEntry* found =
        findExternalMonitorEntryLocked(requestName, fileOffset, size);
    if (found)
    {
        return found;
    }

    RuntimeResourceMonitorEntry item = {};
    item.name = (requestName && requestName[0]) ? requestName : "(external)";
    item.offset = fileOffset;
    item.size = size;
    item.firstRevision = g_resourceMonitorSnapshot.revision + 1;
    item.externalFileSeen = true;
    g_resourceMonitorSnapshot.entries.push_back(item);
    return &g_resourceMonitorSnapshot.entries.back();
}

static void updateMonitorEntryMetadataLocked(
    RuntimeResourceMonitorEntry* item,
    const app_resource_entry* entry)
{
    if (!item || !entry)
    {
        return;
    }

    item->offset = entry->offset;
    item->size = entry->size;
    item->xorKey = entry->xor_key;
    item->cached = item->cached || entry->decoded_data || !entry->xor_key;
}

static uint32_t updateMonitorEntryLoadLocked(
    RuntimeResourceMonitorEntry* item,
    uint32_t guestAddress,
    const void* data,
    uint32_t bytesRead,
    uint32_t positionAfter)
{
    if (!item || !data || bytesRead == 0)
    {
        return 0;
    }

    item->readCalls++;
    item->readBytes += bytesRead;
    item->lastPosition = positionAfter;
    item->lastGuestAddress = guestAddress;
    item->lastLoadSize = bytesRead;
    if (positionAfter > item->maxPosition)
    {
        item->maxPosition = positionAfter;
    }

    uint32_t previewBytes = bytesRead < kResourceMonitorPreviewBytes ?
        bytesRead : kResourceMonitorPreviewBytes;
    item->lastPreview.resize(previewBytes);
    memcpy(item->lastPreview.data(), data, previewBytes);
    return previewBytes;
}

static uint64_t recordMonitorEntryActionLocked(
    RuntimeResourceMonitorEntry* item,
    RuntimeResourceMonitorSource source,
    const char* action)
{
    if (!item)
    {
        return g_resourceMonitorSnapshot.revision;
    }

    if (source == RUNTIME_RESOURCE_MONITOR_SOURCE_DL_RES)
    {
        item->dlResSeen = true;
    }
    else
    {
        item->fsysSeen = true;
    }
    item->lastAction = action ? action : monitorSourceName(source);
    item->lastRevision = ++g_resourceMonitorSnapshot.revision;
    return item->lastRevision;
}

static bool monitorEntryMatchesRequest(
    const RuntimeResourceMonitorEntry& item,
    const char* requestName)
{
    if (!requestName || !requestName[0])
    {
        return true;
    }
    return item.lastRequest == requestName || item.name == requestName;
}

static void markMonitorEntryClosedLocked(RuntimeResourceMonitorEntry* item)
{
    if (!item)
    {
        return;
    }

    item->closeCount++;
    if (item->activeHandles > 0)
    {
        item->activeHandles--;
    }
    item->lastAction = "close";
    item->lastCloseRevision = ++g_resourceMonitorSnapshot.revision;
    item->lastRevision = item->lastCloseRevision;
}

void runtimeResourceMonitorReset(const char* appPath, const char* appSha256)
{
    pthread_mutex_lock(&g_resourceMonitorMutex);
    g_resourceMonitorSnapshot.appPath = appPath ? appPath : "";
    g_resourceMonitorSnapshot.appSha256 = appSha256 ? appSha256 : "";
    g_resourceMonitorSnapshot.entries.clear();
    g_packageResourceLookup.clear();
    g_resourceMonitorSnapshot.revision++;
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorSetActive(bool active)
{
    g_resourceMonitorActive.store(active, std::memory_order_release);
    if (active)
    {
        return;
    }

    pthread_mutex_lock(&g_resourceMonitorMutex);
    g_resourceMonitorSnapshot.entries.clear();
    g_packageResourceLookup.clear();
    g_resourceMonitorSnapshot.revision++;
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

bool runtimeResourceMonitorIsCapturing(void)
{
    return resourceMonitorCaptureEnabled();
}

bool runtimeResourceMonitorMatchesApp(const char* appPath, const char* appSha256)
{
    const char* path = appPath ? appPath : "";
    const char* sha256 = appSha256 ? appSha256 : "";
    pthread_mutex_lock(&g_resourceMonitorMutex);
    bool matches = g_resourceMonitorSnapshot.appPath == path &&
        g_resourceMonitorSnapshot.appSha256 == sha256;
    pthread_mutex_unlock(&g_resourceMonitorMutex);
    return matches;
}

void runtimeResourceMonitorSetAppSha256(const char* appSha256)
{
    pthread_mutex_lock(&g_resourceMonitorMutex);
    g_resourceMonitorSnapshot.appSha256 = appSha256 ? appSha256 : "";
    g_resourceMonitorSnapshot.revision++;
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorSetAppResources(app* loadedApp)
{
    pthread_mutex_lock(&g_resourceMonitorMutex);
    uint32_t resourceCount = loadedApp ? loadedApp->resource_count : 0;
    uint32_t packageResourceCount = loadedApp ? loadedApp->package_resource_count : 0;
    g_packageResourceLookup.clear();
    if (loadedApp)
    {
        for (uint32_t i = 0; i < loadedApp->resource_count; ++i)
        {
            RuntimeResourceMonitorEntry* item =
                ensureMonitorEntryLocked(&loadedApp->resource_data[i]);
            updateMonitorEntryMetadataLocked(item, &loadedApp->resource_data[i]);
        }
        g_packageResourceLookup.reserve(loadedApp->package_resource_count);
        for (uint32_t i = 0; i < loadedApp->package_resource_count; ++i)
        {
            const app_package_resource_entry& packageResource =
                loadedApp->package_resource_data[i];
            if (!packageResource.name || !packageResource.name[0] ||
                packageResource.size == 0)
            {
                continue;
            }

            RuntimePackageResourceLookup item;
            item.name = packageResource.name;
            item.offset = packageResource.offset;
            item.size = packageResource.size;
            g_packageResourceLookup.push_back(item);
        }
    }
    g_resourceMonitorSnapshot.revision++;
    if (resourceMonitorAutotestEnabled())
    {
        printf("resource-monitor-autotest: seeded resources=%u package_resources=%u snapshot=%u revision=%llu\n",
            resourceCount,
            packageResourceCount,
            (unsigned)g_resourceMonitorSnapshot.entries.size(),
            (unsigned long long)g_resourceMonitorSnapshot.revision);
    }
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorRecordOpen(
    RuntimeResourceMonitorSource source,
    const char* requestName,
    const app_resource_entry* entry,
    bool cached)
{
    if (!resourceMonitorCaptureEnabled())
    {
        return;
    }

    pthread_mutex_lock(&g_resourceMonitorMutex);
    RuntimeResourceMonitorEntry* item = ensureMonitorEntryLocked(entry);
    if (item)
    {
        updateMonitorEntryMetadataLocked(item, entry);
        bool firstOpen = item->openCount == 0;
        item->lastRequest = requestName ? requestName : "";
        item->cached = item->cached || cached;
        item->activeHandles++;
        item->openCount++;
        uint64_t revision = recordMonitorEntryActionLocked(item, source, "open");
        if (firstOpen)
        {
            item->firstOpenRevision = revision;
        }
        item->lastOpenRevision = revision;
        if (resourceMonitorAutotestEnabled())
        {
            printf("resource-monitor-autotest: open name=%s first=%llu last=%llu opens=%llu\n",
                item->name.c_str(),
                (unsigned long long)item->firstOpenRevision,
                (unsigned long long)item->lastOpenRevision,
                (unsigned long long)item->openCount);
        }
    }
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorRecordLoadContent(
    RuntimeResourceMonitorSource source,
    const app_resource_entry* entry,
    uint32_t guestAddress,
    const void* data,
    uint32_t bytesRead,
    uint32_t positionAfter)
{
    if (!resourceMonitorCaptureEnabled() || !data || bytesRead == 0)
    {
        return;
    }

    pthread_mutex_lock(&g_resourceMonitorMutex);
    RuntimeResourceMonitorEntry* item = ensureMonitorEntryLocked(entry);
    if (item)
    {
        uint32_t previewBytes = updateMonitorEntryLoadLocked(
            item, guestAddress, data, bytesRead, positionAfter);
        item->lastReadRevision = recordMonitorEntryActionLocked(item, source, "load");
        if (resourceMonitorAutotestEnabled())
        {
            printf("resource-monitor-autotest: load name=%s revision=%llu guest=0x%08x bytes=%u preview=%u total=%llu calls=%llu\n",
                item->name.c_str(),
                (unsigned long long)item->lastReadRevision,
                guestAddress,
                bytesRead,
                previewBytes,
                (unsigned long long)item->readBytes,
                (unsigned long long)item->readCalls);
        }
    }
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorRecordPackageLoadContent(
    const char* requestName,
    uint32_t packageOffset,
    uint32_t guestAddress,
    const void* data,
    uint32_t bytesRead,
    uint32_t positionAfter)
{
    if (!resourceMonitorCaptureEnabled() || !data || bytesRead == 0)
    {
        return;
    }

    pthread_mutex_lock(&g_resourceMonitorMutex);
    RuntimeResourceMonitorEntry* item =
        ensurePackageMonitorEntryLocked(packageOffset, bytesRead);
    if (item)
    {
        item->appPackageSeen = true;
        item->lastRequest = requestName ? requestName : "";
        uint32_t previewBytes = updateMonitorEntryLoadLocked(
            item, guestAddress, data, bytesRead, positionAfter);
        item->lastAction = "load";
        item->lastReadRevision = ++g_resourceMonitorSnapshot.revision;
        item->lastRevision = item->lastReadRevision;
        if (resourceMonitorAutotestEnabled())
        {
            printf("resource-monitor-autotest: package-load name=%s revision=%llu guest=0x%08x offset=0x%08x bytes=%u preview=%u total=%llu calls=%llu\n",
                item->name.c_str(),
                (unsigned long long)item->lastReadRevision,
                guestAddress,
                packageOffset,
                bytesRead,
                previewBytes,
                (unsigned long long)item->readBytes,
                (unsigned long long)item->readCalls);
        }
    }
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorRecordExternalLoadContent(
    const char* requestName,
    uint32_t fileOffset,
    uint32_t guestAddress,
    const void* data,
    uint32_t bytesRead,
    uint32_t positionAfter)
{
    if (!resourceMonitorCaptureEnabled() || !data || bytesRead == 0)
    {
        return;
    }

    pthread_mutex_lock(&g_resourceMonitorMutex);
    RuntimeResourceMonitorEntry* item =
        ensureExternalMonitorEntryLocked(requestName, fileOffset, bytesRead);
    if (item)
    {
        item->externalFileSeen = true;
        item->lastRequest = requestName ? requestName : "";
        uint32_t previewBytes = updateMonitorEntryLoadLocked(
            item, guestAddress, data, bytesRead, positionAfter);
        item->lastAction = "load";
        item->lastReadRevision = ++g_resourceMonitorSnapshot.revision;
        item->lastRevision = item->lastReadRevision;
        if (resourceMonitorAutotestEnabled())
        {
            printf("resource-monitor-autotest: external-load name=%s revision=%llu guest=0x%08x offset=0x%08x bytes=%u preview=%u total=%llu calls=%llu\n",
                item->name.c_str(),
                (unsigned long long)item->lastReadRevision,
                guestAddress,
                fileOffset,
                bytesRead,
                previewBytes,
                (unsigned long long)item->readBytes,
                (unsigned long long)item->readCalls);
        }
    }
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorRecordSeek(
    RuntimeResourceMonitorSource source,
    const app_resource_entry* entry,
    uint32_t positionAfter)
{
    if (!resourceMonitorCaptureEnabled())
    {
        return;
    }

    pthread_mutex_lock(&g_resourceMonitorMutex);
    RuntimeResourceMonitorEntry* item = ensureMonitorEntryLocked(entry);
    if (item)
    {
        item->seekCalls++;
        item->lastPosition = positionAfter;
        if (positionAfter > item->maxPosition)
        {
            item->maxPosition = positionAfter;
        }
        recordMonitorEntryActionLocked(item, source, "seek");
    }
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorRecordPackageClose(const char* requestName)
{
    if (!resourceMonitorCaptureEnabled())
    {
        return;
    }

    pthread_mutex_lock(&g_resourceMonitorMutex);
    for (size_t i = 0; i < g_resourceMonitorSnapshot.entries.size(); ++i)
    {
        RuntimeResourceMonitorEntry& item = g_resourceMonitorSnapshot.entries[i];
        if (item.appPackageSeen && item.lastReadRevision &&
            monitorEntryMatchesRequest(item, requestName))
        {
            markMonitorEntryClosedLocked(&item);
        }
    }
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorRecordExternalClose(const char* requestName)
{
    if (!resourceMonitorCaptureEnabled())
    {
        return;
    }

    pthread_mutex_lock(&g_resourceMonitorMutex);
    for (size_t i = 0; i < g_resourceMonitorSnapshot.entries.size(); ++i)
    {
        RuntimeResourceMonitorEntry& item = g_resourceMonitorSnapshot.entries[i];
        if (item.externalFileSeen && item.lastReadRevision &&
            monitorEntryMatchesRequest(item, requestName))
        {
            markMonitorEntryClosedLocked(&item);
        }
    }
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

void runtimeResourceMonitorRecordClose(
    RuntimeResourceMonitorSource source,
    const app_resource_entry* entry)
{
    if (!resourceMonitorCaptureEnabled())
    {
        return;
    }

    pthread_mutex_lock(&g_resourceMonitorMutex);
    RuntimeResourceMonitorEntry* item = ensureMonitorEntryLocked(entry);
    if (item)
    {
        item->closeCount++;
        if (item->activeHandles > 0)
        {
            item->activeHandles--;
        }
        item->lastCloseRevision = recordMonitorEntryActionLocked(item, source, "close");
    }
    pthread_mutex_unlock(&g_resourceMonitorMutex);
}

RuntimeResourceMonitorSnapshot runtimeResourceMonitorGetSnapshot(void)
{
    pthread_mutex_lock(&g_resourceMonitorMutex);
    RuntimeResourceMonitorSnapshot snapshot = g_resourceMonitorSnapshot;
    pthread_mutex_unlock(&g_resourceMonitorMutex);
    return snapshot;
}

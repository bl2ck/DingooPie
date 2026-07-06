#ifndef DINGOO_PIE_RUNTIME_RESOURCE_MONITOR_H
#define DINGOO_PIE_RUNTIME_RESOURCE_MONITOR_H

#include "app_loader.h"

#include <stdint.h>
#include <string>
#include <vector>

enum RuntimeResourceMonitorSource
{
    RUNTIME_RESOURCE_MONITOR_SOURCE_FSYS,
    RUNTIME_RESOURCE_MONITOR_SOURCE_DL_RES
};

struct RuntimeResourceMonitorEntry
{
    std::string name;
    std::string lastRequest;
    uint32_t offset;
    uint32_t size;
    uint8_t xorKey;
    bool fsysSeen;
    bool dlResSeen;
    bool appPackageSeen;
    bool externalFileSeen;
    bool cached;
    uint32_t activeHandles;
    uint64_t openCount;
    uint64_t closeCount;
    uint64_t readCalls;
    uint64_t readBytes;
    uint64_t seekCalls;
    uint32_t lastPosition;
    uint32_t maxPosition;
    uint32_t lastGuestAddress;
    uint32_t lastLoadSize;
    uint64_t firstRevision;
    uint64_t firstOpenRevision;
    uint64_t lastOpenRevision;
    uint64_t lastReadRevision;
    uint64_t lastCloseRevision;
    uint64_t lastRevision;
    std::string lastAction;
    std::vector<uint8_t> lastPreview;
};

struct RuntimeResourceMonitorSnapshot
{
    std::string appPath;
    std::string appSha256;
    uint64_t revision;
    std::vector<RuntimeResourceMonitorEntry> entries;
};

void runtimeResourceMonitorReset(const char* appPath, const char* appSha256);
void runtimeResourceMonitorSetActive(bool active);
bool runtimeResourceMonitorIsCapturing(void);
void runtimeResourceMonitorSetAppSha256(const char* appSha256);
void runtimeResourceMonitorSetAppResources(app* loadedApp);
void runtimeResourceMonitorRecordOpen(
    RuntimeResourceMonitorSource source,
    const char* requestName,
    const app_resource_entry* entry,
    bool cached);
void runtimeResourceMonitorRecordLoadContent(
    RuntimeResourceMonitorSource source,
    const app_resource_entry* entry,
    uint32_t guestAddress,
    const void* data,
    uint32_t bytesRead,
    uint32_t positionAfter);
void runtimeResourceMonitorRecordPackageLoadContent(
    const char* requestName,
    uint32_t packageOffset,
    uint32_t guestAddress,
    const void* data,
    uint32_t bytesRead,
    uint32_t positionAfter);
void runtimeResourceMonitorRecordExternalLoadContent(
    const char* requestName,
    uint32_t fileOffset,
    uint32_t guestAddress,
    const void* data,
    uint32_t bytesRead,
    uint32_t positionAfter);
void runtimeResourceMonitorRecordPackageClose(const char* requestName);
void runtimeResourceMonitorRecordExternalClose(const char* requestName);
void runtimeResourceMonitorRecordSeek(
    RuntimeResourceMonitorSource source,
    const app_resource_entry* entry,
    uint32_t positionAfter);
void runtimeResourceMonitorRecordClose(
    RuntimeResourceMonitorSource source,
    const app_resource_entry* entry);
RuntimeResourceMonitorSnapshot runtimeResourceMonitorGetSnapshot(void);

#endif

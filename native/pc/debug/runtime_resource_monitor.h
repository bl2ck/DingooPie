#ifndef DINGOO_PIE_RUNTIME_RESOURCE_MONITOR_H
#define DINGOO_PIE_RUNTIME_RESOURCE_MONITOR_H

#include "shared/diagnostics/runtime_resource_events.h"
#include "shared/services/guest_package.h"

#include <stdint.h>
#include <string>
#include <vector>

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

RuntimeResourceMonitorSnapshot runtimeResourceMonitorGetSnapshot(void);

#endif

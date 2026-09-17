#ifndef DINGOO_PIE_SHARED_DIAGNOSTICS_RUNTIME_RESOURCE_EVENTS_H
#define DINGOO_PIE_SHARED_DIAGNOSTICS_RUNTIME_RESOURCE_EVENTS_H

#include <stdint.h>

struct GuestPackage;
struct GuestResourceEntry;

enum RuntimeResourceMonitorSource
{
    RUNTIME_RESOURCE_MONITOR_SOURCE_FSYS,
    RUNTIME_RESOURCE_MONITOR_SOURCE_DL_RES
};

void runtimeResourceMonitorReset(const char* appPath, const char* appSha256);
void runtimeResourceMonitorSetActive(bool active);
bool runtimeResourceMonitorIsCapturing(void);
bool runtimeResourceMonitorMatchesApp(const char* appPath, const char* appSha256);
void runtimeResourceMonitorSetAppSha256(const char* appSha256);
void runtimeResourceMonitorSetAppResources(GuestPackage* package);
void runtimeResourceMonitorSetGuestResources(GuestPackage* package);
void runtimeResourceMonitorRecordGuestOpen(
    const char* requestName, const GuestResourceEntry* entry, bool cached);
void runtimeResourceMonitorRecordGuestLoadContent(
    const GuestResourceEntry* entry, uint32_t guestAddress,
    const void* data, uint32_t bytesRead, uint32_t positionAfter);
void runtimeResourceMonitorRecordGuestClose(const GuestResourceEntry* entry);
void runtimeResourceMonitorRecordOpen(
    RuntimeResourceMonitorSource source,
    const char* requestName,
    const GuestResourceEntry* entry,
    bool cached);
void runtimeResourceMonitorRecordLoadContent(
    RuntimeResourceMonitorSource source,
    const GuestResourceEntry* entry,
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
    const GuestResourceEntry* entry,
    uint32_t positionAfter);
void runtimeResourceMonitorRecordClose(
    RuntimeResourceMonitorSource source,
    const GuestResourceEntry* entry);

#endif

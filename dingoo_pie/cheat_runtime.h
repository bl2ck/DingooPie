#ifndef DINGOO_PIE_CHEAT_RUNTIME_H
#define DINGOO_PIE_CHEAT_RUNTIME_H

#include "native_runtime.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

struct CheatRuntimeEntryView
{
    bool enabled;
    // Stable feature key from the .cht file. A key written as
    // "Chinese/English" is split into localized display names below.
    std::string name;
    std::string nameChinese;
    std::string nameEnglish;
};

struct CheatRuntimeStatus
{
    bool enabled;
    bool available;
    bool loaded;
    bool shaMismatch;
    uint32_t revision;
    std::string sourcePath;
    // Hash declared by app_sha256 in the .cht file. It is validation only;
    // file lookup is always based on the running app's base name.
    std::string appSha256;
    std::string currentAppSha256;
    // UI-facing feature rows. Multiple low-level patch lines with the same
    // prefix before ':' or a full-width colon are grouped into one entry.
    std::vector<CheatRuntimeEntryView> entries;
};

void cheatRuntimeSetEnabled(bool enabled);
bool cheatRuntimeEnabled(void);
uint32_t cheatRuntimeRevision(void);
void cheatRuntimeLoadForApp(
    const char* appSha256,
    const char* appPath,
    const std::vector<std::string>& enabledFeatureKeys);
CheatRuntimeStatus cheatRuntimeGetStatus(void);
bool cheatRuntimeSetEntryEnabled(size_t index, bool enabled);
void cheatRuntimeBind(NativeRuntime* runtime);
void cheatRuntimeUnbind(NativeRuntime* runtime);
void cheatRuntimeApplyNow(void);
void cheatRuntimeApplyStartup(NativeRuntime* runtime);
void cheatRuntimeApplyFrame(void);

#endif

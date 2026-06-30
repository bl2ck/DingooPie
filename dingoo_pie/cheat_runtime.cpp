#include "cheat_runtime.h"

#include "app_paths.h"
#include "cheat_engine.h"
#include "platform_win32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static std::mutex g_cheatMutex;
static CheatSet g_cheatSet;
static bool g_cheatLoaded = false;
static bool g_cheatEnabled = false;
static bool g_cheatRequestedEnabled = false;
static bool g_cheatShaMismatch = false;
static NativeRuntime* g_cheatRuntime = NULL;
static uint32_t g_lastFrameApplyCount = 0;
static uint32_t g_cheatRevision = 0;
static std::string g_currentAppSha256;
static bool g_manualApplyPending = false;

static bool runtimeReadCallback(void* userData, uint32_t address, void* out, size_t size)
{
    return nativeRuntimeReadRaw((NativeRuntime*)userData, address, out, size);
}

static bool runtimeWriteCallback(void* userData, uint32_t address, const void* in, size_t size)
{
    return nativeRuntimeWriteRaw((NativeRuntime*)userData, address, in, size);
}

static bool envEnabled(const char* name)
{
    const char* value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static bool fileExists(const std::string& path)
{
#ifdef _WIN32
    int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (size <= 0)
    {
        return false;
    }
    std::wstring widePath((size_t)size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &widePath[0], size);
    DWORD attrs = GetFileAttributesW(widePath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
    {
        return false;
    }
    fclose(file);
    return true;
#endif
}

static std::string executableDirectory(void)
{
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        std::string out = platformWideToUtf8(std::wstring(path, path + len));
        size_t slash = out.find_last_of("\\/");
        if (slash != std::string::npos)
        {
            out.resize(slash);
            return out;
        }
    }
#endif
    return ".";
}

static std::string joinPath(const std::string& a, const std::string& b)
{
    if (a.empty() || a == ".")
    {
        return b;
    }
    char last = a[a.size() - 1];
    if (last == '\\' || last == '/')
    {
        return a + b;
    }
#ifdef _WIN32
    return a + "\\" + b;
#else
    return a + "/" + b;
#endif
}

static std::string defaultCheatDirectory(void)
{
    const char* envDir = getenv("DINGOO_PIE_CHEAT_DIR");
    if (envDir && envDir[0])
    {
        return envDir;
    }
    return joinPath(executableDirectory(), "cheats");
}

static std::string normalizeSha(const char* appSha256)
{
    std::string value = appSha256 ? appSha256 : "";
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] >= 'a' && value[i] <= 'f')
        {
            value[i] = (char)(value[i] - 'a' + 'A');
        }
    }
    return value;
}

static std::string trimAscii(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
    {
        begin++;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
    {
        end--;
    }
    return text.substr(begin, end - begin);
}

struct CheatFeatureName
{
    std::string key;
    std::string chinese;
    std::string english;
};

static size_t findFeatureSeparator(const std::string& entryName)
{
    size_t asciiColon = entryName.find(':');
    size_t fullColon = entryName.find("\xEF\xBC\x9A");
    if (asciiColon != std::string::npos && fullColon != std::string::npos)
    {
        return asciiColon < fullColon ? asciiColon : fullColon;
    }
    if (asciiColon != std::string::npos)
    {
        return asciiColon;
    }
    return fullColon;
}

static CheatFeatureName parseFeatureName(const std::string& entryName)
{
    // A .cht line maps to one low-level patch; the UI groups lines that share
    // the same prefix so players see feature names, not implementation details.
    size_t sep = findFeatureSeparator(entryName);
    std::string key = sep == std::string::npos ?
        trimAscii(entryName) : trimAscii(entryName.substr(0, sep));
    if (key.empty())
    {
        key = trimAscii(entryName);
    }

    CheatFeatureName name;
    name.key = key;
    name.chinese = key;
    name.english = key;

    size_t slash = key.find('/');
    if (slash != std::string::npos)
    {
        std::string first = trimAscii(key.substr(0, slash));
        std::string second = trimAscii(key.substr(slash + 1));
        if (!first.empty())
        {
            name.chinese = first;
        }
        if (!second.empty())
        {
            name.english = second;
        }
    }

    return name;
}

struct CheatFeatureGroup
{
    std::string name;
    std::string nameChinese;
    std::string nameEnglish;
    std::vector<size_t> entryIndices;
};

static std::vector<CheatFeatureGroup> buildFeatureGroupsLocked(void)
{
    std::vector<CheatFeatureGroup> groups;
    std::unordered_map<std::string, size_t> groupIndexByName;
    for (size_t i = 0; i < g_cheatSet.entries.size(); ++i)
    {
        CheatFeatureName name = parseFeatureName(g_cheatSet.entries[i].name);
        std::unordered_map<std::string, size_t>::iterator found = groupIndexByName.find(name.key);
        size_t groupIndex = 0;
        if (found == groupIndexByName.end())
        {
            groupIndex = groups.size();
            CheatFeatureGroup group;
            group.name = name.key;
            group.nameChinese = name.chinese;
            group.nameEnglish = name.english;
            groups.push_back(group);
            groupIndexByName[name.key] = groupIndex;
        }
        else
        {
            groupIndex = found->second;
        }
        groups[groupIndex].entryIndices.push_back(i);
    }
    return groups;
}

static bool groupEnabledLocked(const CheatFeatureGroup& group)
{
    if (group.entryIndices.empty())
    {
        return false;
    }
    for (size_t i = 0; i < group.entryIndices.size(); ++i)
    {
        size_t entryIndex = group.entryIndices[i];
        if (entryIndex >= g_cheatSet.entries.size() || !g_cheatSet.entries[entryIndex].enabled)
        {
            return false;
        }
    }
    return true;
}

static bool setFeatureGroupEnabledLocked(const CheatFeatureGroup& group, bool enabled)
{
    bool changed = false;
    for (size_t i = 0; i < group.entryIndices.size(); ++i)
    {
        size_t entryIndex = group.entryIndices[i];
        if (entryIndex >= g_cheatSet.entries.size())
        {
            continue;
        }
        CheatEntry& entry = g_cheatSet.entries[entryIndex];
        if (entry.enabled != enabled)
        {
            entry.enabled = enabled;
            changed = true;
        }
    }
    if (changed)
    {
        g_cheatRevision++;
    }
    return true;
}

static bool setEnabledFeatureKeysLocked(const std::vector<std::string>& featureKeys)
{
    // Parsed .cht status is treated as metadata only; user selections from the
    // INI decide which feature groups are active for this app launch.
    std::unordered_set<std::string> enabledKeys;
    for (size_t i = 0; i < featureKeys.size(); ++i)
    {
        if (!featureKeys[i].empty())
        {
            enabledKeys.insert(featureKeys[i]);
        }
    }

    bool changed = false;
    std::vector<CheatFeatureGroup> groups = buildFeatureGroupsLocked();
    for (size_t i = 0; i < groups.size(); ++i)
    {
        bool enabled = enabledKeys.find(groups[i].name) != enabledKeys.end();
        bool wasEnabled = groupEnabledLocked(groups[i]);
        setFeatureGroupEnabledLocked(groups[i], enabled);
        changed = changed || wasEnabled != enabled;
    }
    return changed;
}

static void resetEntryEnabledStateLocked(void)
{
    // Keep every feature unchecked by default, even if the .cht line says "on".
    // The menu persists explicit user choices separately.
    for (size_t i = 0; i < g_cheatSet.entries.size(); ++i)
    {
        g_cheatSet.entries[i].enabled = false;
        g_cheatSet.entries[i].appliedOnce = false;
    }
}

static bool loadCandidate(const std::string& path, CheatSet* out)
{
    if (!fileExists(path))
    {
        return false;
    }

    std::string error;
    CheatSet loaded;
    if (!cheatLoadFile(path, &loaded, &error))
    {
        printf("cheat: failed to load %s: %s\n", path.c_str(), error.c_str());
        return false;
    }

    *out = loaded;
    return true;
}

static void logApplyStats(const char* reason, const CheatApplyStats& stats)
{
    if (stats.attempted == 0 && stats.skippedDisabled == 0)
    {
        return;
    }
    if (envEnabled("DINGOO_PIE_CHEAT_TRACE"))
    {
        printf("cheat: apply %s attempted=%u applied=%u disabled=%u once=%u compare=%u read_fail=%u write_fail=%u\n",
            reason ? reason : "runtime",
            stats.attempted,
            stats.applied,
            stats.skippedDisabled,
            stats.skippedOnce,
            stats.skippedCompare,
            stats.readFailures,
            stats.writeFailures);
    }
}

static bool cheatAvailableLocked(void)
{
    return g_cheatLoaded && !g_cheatShaMismatch && !g_cheatSet.entries.empty();
}

static bool cheatCanApplyLocked(NativeRuntime* runtime)
{
    return runtime && g_cheatEnabled && cheatAvailableLocked();
}

static void clearManualApplyLocked(void)
{
    g_manualApplyPending = false;
}

static void requestManualApplyLocked(void)
{
    g_manualApplyPending = true;
}

static bool consumeManualApplyLocked(void)
{
    bool pending = g_manualApplyPending;
    g_manualApplyPending = false;
    return pending;
}

static void refreshEffectiveEnabledLocked(void)
{
    g_cheatEnabled = g_cheatRequestedEnabled && cheatAvailableLocked();
}

static void finishApplyLocked(NativeRuntime* runtime, const CheatApplyStats& stats)
{
    if (stats.appliedOnce > 0)
    {
        g_cheatRevision++;
    }
    if (stats.applied > 0)
    {
        nativeRuntimeFlushCodeCache(runtime);
    }
}

static void applyLocked(NativeRuntime* runtime, CheatApplyPhase phase, const char* reason)
{
    if (!cheatCanApplyLocked(runtime))
    {
        return;
    }

    CheatApplyStats stats = cheatApply(&g_cheatSet, runtimeReadCallback, runtimeWriteCallback,
        runtime, phase);
    logApplyStats(reason, stats);
    finishApplyLocked(runtime, stats);
}

void cheatRuntimeSetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    bool oldEnabled = g_cheatEnabled;
    g_cheatRequestedEnabled = enabled;
    refreshEffectiveEnabledLocked();
    if (g_cheatEnabled != oldEnabled)
    {
        g_cheatRevision++;
    }
}

bool cheatRuntimeEnabled(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    return g_cheatEnabled;
}

uint32_t cheatRuntimeRevision(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    return g_cheatRevision;
}

void cheatRuntimeLoadForApp(
    const char* appSha256,
    const char* appPath,
    const std::vector<std::string>& enabledFeatureKeys)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    cheatClearSet(&g_cheatSet);
    g_cheatLoaded = false;
    g_cheatShaMismatch = false;
    g_lastFrameApplyCount = 0;
    clearManualApplyLocked();
    g_currentAppSha256 = normalizeSha(appSha256);
    g_cheatRevision++;

    std::string dir = defaultCheatDirectory();
    CheatSet loaded;

    if (appPath && appPath[0])
    {
        std::string cheatName = appCheatFileNameFromPath(appPath);
        std::string namePath = cheatName.empty() ? "" : joinPath(dir, cheatName);
        if (!namePath.empty() && loadCandidate(namePath, &loaded))
        {
            g_cheatSet = loaded;
            resetEntryEnabledStateLocked();
            g_cheatLoaded = true;
        }
    }

    if (g_cheatLoaded)
    {
        g_cheatShaMismatch = !cheatSetMatchesApp(g_cheatSet, appSha256);
        if (!g_cheatShaMismatch)
        {
            setEnabledFeatureKeysLocked(enabledFeatureKeys);
        }
    }
    refreshEffectiveEnabledLocked();

    if (g_cheatLoaded)
    {
        printf("cheat: loaded %u code(s), parse_errors=%u, enabled=%u, sha_mismatch=%u, source=%s\n",
            (unsigned int)g_cheatSet.entries.size(),
            g_cheatSet.parseErrors,
            g_cheatEnabled ? 1u : 0u,
            g_cheatShaMismatch ? 1u : 0u,
            g_cheatSet.sourcePath.c_str());
        if (g_cheatShaMismatch)
        {
            printf("cheat: app_sha256 mismatch, cheats disabled for this game: cheat_sha256=%s current_sha256=%s\n",
                g_cheatSet.appSha256.empty() ? "(none)" : g_cheatSet.appSha256.c_str(),
                g_currentAppSha256.empty() ? "(none)" : g_currentAppSha256.c_str());
        }
    }
    else if (envEnabled("DINGOO_PIE_CHEAT_TRACE"))
    {
        printf("cheat: no same-name cheat file for app=%s dir=%s\n",
            appPath && appPath[0] ? appPath : "(none)", dir.c_str());
    }
}

CheatRuntimeStatus cheatRuntimeGetStatus(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    CheatRuntimeStatus status;
    status.enabled = g_cheatEnabled;
    status.available = cheatAvailableLocked();
    status.loaded = g_cheatLoaded;
    status.shaMismatch = g_cheatShaMismatch;
    status.revision = g_cheatRevision;
    status.sourcePath = g_cheatSet.sourcePath;
    status.appSha256 = g_cheatSet.appSha256;
    status.currentAppSha256 = g_currentAppSha256;
    std::vector<CheatFeatureGroup> groups = buildFeatureGroupsLocked();
    status.entries.reserve(groups.size());
    for (size_t i = 0; i < groups.size(); ++i)
    {
        CheatRuntimeEntryView view;
        view.enabled = groupEnabledLocked(groups[i]);
        view.name = groups[i].name;
        view.nameChinese = groups[i].nameChinese;
        view.nameEnglish = groups[i].nameEnglish;
        status.entries.push_back(view);
    }
    return status;
}

bool cheatRuntimeSetEntryEnabled(size_t index, bool enabled)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    if (!cheatAvailableLocked())
    {
        return false;
    }
    std::vector<CheatFeatureGroup> groups = buildFeatureGroupsLocked();
    if (index >= groups.size())
    {
        return false;
    }

    return setFeatureGroupEnabledLocked(groups[index], enabled);
}

void cheatRuntimeBind(NativeRuntime* runtime)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    g_cheatRuntime = runtime;
}

void cheatRuntimeUnbind(NativeRuntime* runtime)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    if (!runtime || g_cheatRuntime == runtime)
    {
        g_cheatRuntime = NULL;
    }
}

void cheatRuntimeApplyStartup(NativeRuntime* runtime)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    applyLocked(runtime, CHEAT_APPLY_STARTUP, "startup");
}

void cheatRuntimeApplyNow(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    if (!cheatCanApplyLocked(g_cheatRuntime))
    {
        return;
    }

    // Menu commands run on the frontend thread while the IR JIT may still own
    // its block cache. Defer the actual memory writes to the runtime frame
    // boundary, which is the same place periodic cheat application already
    // runs and the save-state restore gate is observed.
    requestManualApplyLocked();
}

void cheatRuntimeApplyFrame(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    NativeRuntime* runtime = g_cheatRuntime;
    if (!cheatCanApplyLocked(runtime))
    {
        return;
    }

    bool manualApply = consumeManualApplyLocked();
    CheatApplyStats stats = cheatApply(&g_cheatSet, runtimeReadCallback, runtimeWriteCallback,
        runtime, CHEAT_APPLY_FRAME);
    g_lastFrameApplyCount += stats.applied;
    finishApplyLocked(runtime, stats);
    if (manualApply)
    {
        logApplyStats("menu", stats);
    }
    else if (envEnabled("DINGOO_PIE_CHEAT_TRACE") && g_lastFrameApplyCount >= 60)
    {
        logApplyStats("frame", stats);
        g_lastFrameApplyCount = 0;
    }
}

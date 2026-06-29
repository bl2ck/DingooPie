#ifndef DINGOO_PIE_CHEAT_ENGINE_H
#define DINGOO_PIE_CHEAT_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

enum CheatWidth
{
    CHEAT_WIDTH_U8 = 1,
    CHEAT_WIDTH_U16 = 2,
    CHEAT_WIDTH_U32 = 4
};

enum CheatApplyPhase
{
    CHEAT_APPLY_STARTUP,
    CHEAT_APPLY_FRAME
};

struct CheatEntry
{
    bool enabled;
    bool once;
    bool appliedOnce;
    std::string name;
    CheatWidth width;
    uint32_t address;
    uint32_t value;
    bool hasCompare;
    uint32_t compareValue;
};

struct CheatSet
{
    std::string sourcePath;
    std::string appSha256;
    std::vector<CheatEntry> entries;
    uint32_t parseErrors;
};

struct CheatApplyStats
{
    uint32_t attempted;
    uint32_t applied;
    uint32_t skippedDisabled;
    uint32_t skippedOnce;
    uint32_t skippedCompare;
    uint32_t readFailures;
    uint32_t writeFailures;
    uint32_t appliedOnce;
};

typedef bool (*CheatReadCallback)(void* userData, uint32_t address, void* out, size_t size);
typedef bool (*CheatWriteCallback)(void* userData, uint32_t address, const void* in, size_t size);

void cheatClearSet(CheatSet* set);
bool cheatParseText(const std::string& text, const std::string& sourcePath, CheatSet* out, std::string* error);
bool cheatLoadFile(const std::string& path, CheatSet* out, std::string* error);
bool cheatSetMatchesApp(const CheatSet& set, const char* appSha256);
CheatApplyStats cheatApply(CheatSet* set, CheatReadCallback readCallback, CheatWriteCallback writeCallback,
    void* userData, CheatApplyPhase phase);
uint32_t cheatWidthBytes(CheatWidth width);
const char* cheatWidthName(CheatWidth width);

#endif

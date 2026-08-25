#include "cheat_engine.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef _WIN32
static std::wstring utf8ToWide(const std::string& text)
{
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (size <= 0)
    {
        return L"";
    }

    std::wstring out((size_t)size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], size);
    return out;
}
#endif

static std::string trimText(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isspace((unsigned char)text[begin]))
    {
        begin++;
    }
    while (end > begin && isspace((unsigned char)text[end - 1]))
    {
        end--;
    }
    return text.substr(begin, end - begin);
}

static std::string lowerText(const std::string& text)
{
    std::string out = text;
    for (size_t i = 0; i < out.size(); ++i)
    {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

static bool equalsIgnoreCase(const std::string& a, const std::string& b)
{
    return lowerText(a) == lowerText(b);
}

static bool isHexSha256(const std::string& value)
{
    if (value.size() != 64)
    {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (!isxdigit((unsigned char)value[i]))
        {
            return false;
        }
    }
    return true;
}

static std::string stripInlineComment(const std::string& line)
{
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '"')
        {
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && line[i] == '#')
        {
            return line.substr(0, i);
        }
    }
    return line;
}

static std::vector<std::string> splitPipe(const std::string& line)
{
    std::vector<std::string> parts;
    size_t begin = 0;
    for (;;)
    {
        size_t sep = line.find('|', begin);
        if (sep == std::string::npos)
        {
            parts.push_back(trimText(line.substr(begin)));
            return parts;
        }
        parts.push_back(trimText(line.substr(begin, sep - begin)));
        begin = sep + 1;
    }
}

static bool parseUint32(const std::string& text, uint32_t* out)
{
    if (!out)
    {
        return false;
    }

    std::string value = trimText(text);
    if (value.empty())
    {
        return false;
    }
    if (value[0] == '$')
    {
        value = "0x" + value.substr(1);
    }

    char* end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value.c_str(), &end, 0);
    if (end == value.c_str() || *end != '\0' || errno == ERANGE || parsed > 0xfffffffful)
    {
        return false;
    }

    *out = (uint32_t)parsed;
    return true;
}

static bool parseWidth(const std::string& text, CheatWidth* out)
{
    if (!out)
    {
        return false;
    }

    std::string value = lowerText(trimText(text));
    if (value == "u8" || value == "8" || value == "byte")
    {
        *out = CHEAT_WIDTH_U8;
        return true;
    }
    if (value == "u16" || value == "16" || value == "half" || value == "halfword")
    {
        *out = CHEAT_WIDTH_U16;
        return true;
    }
    if (value == "u32" || value == "32" || value == "word")
    {
        *out = CHEAT_WIDTH_U32;
        return true;
    }
    return false;
}

static bool valueFitsWidth(CheatWidth width, uint32_t value)
{
    switch (width)
    {
    case CHEAT_WIDTH_U8:
        return value <= 0xffu;
    case CHEAT_WIDTH_U16:
        return value <= 0xffffu;
    case CHEAT_WIDTH_U32:
        return true;
    default:
        return false;
    }
}

static bool addressAligned(CheatWidth width, uint32_t address)
{
    uint32_t bytes = cheatWidthBytes(width);
    return bytes == 1 || (address % bytes) == 0;
}

static uint32_t readGuestLe(const uint8_t* bytes, CheatWidth width)
{
    switch (width)
    {
    case CHEAT_WIDTH_U8:
        return bytes[0];
    case CHEAT_WIDTH_U16:
        return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
    case CHEAT_WIDTH_U32:
        return (uint32_t)bytes[0] |
            ((uint32_t)bytes[1] << 8) |
            ((uint32_t)bytes[2] << 16) |
            ((uint32_t)bytes[3] << 24);
    default:
        return 0;
    }
}

static void writeGuestLe(uint8_t* bytes, CheatWidth width, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffu);
    if (width == CHEAT_WIDTH_U8)
    {
        return;
    }
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    if (width == CHEAT_WIDTH_U16)
    {
        return;
    }
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
}

static bool parseCheatLine(const std::string& line, CheatEntry* out, std::string* error)
{
    std::vector<std::string> parts = splitPipe(line);
    if (parts.size() != 5 && parts.size() != 6)
    {
        if (error)
        {
            *error = "expected status|name|width|address|value or status|name|width|address|value|compare";
        }
        return false;
    }
    if (!out)
    {
        if (error)
        {
            *error = "missing output entry";
        }
        return false;
    }

    CheatEntry entry = {};
    entry.enabled = false;
    entry.once = false;
    entry.appliedOnce = false;
    entry.width = CHEAT_WIDTH_U8;

    std::string status = lowerText(parts[0]);
    if (status == "on" || status == "enabled" || status == "enable" || status == "1")
    {
        entry.enabled = true;
    }
    else if (status == "once")
    {
        entry.enabled = true;
        entry.once = true;
    }
    else if (status == "off" || status == "disabled" || status == "disable" || status == "0")
    {
        entry.enabled = false;
    }
    else
    {
        if (error)
        {
            *error = "unknown status";
        }
        return false;
    }

    entry.name = parts[1];
    if (entry.name.empty())
    {
        if (error)
        {
            *error = "missing cheat name";
        }
        return false;
    }
    if (!parseWidth(parts[2], &entry.width))
    {
        if (error)
        {
            *error = "unknown width";
        }
        return false;
    }
    if (!parseUint32(parts[3], &entry.address))
    {
        if (error)
        {
            *error = "invalid address";
        }
        return false;
    }
    if (!addressAligned(entry.width, entry.address))
    {
        if (error)
        {
            *error = "address is not aligned for width";
        }
        return false;
    }
    if (!parseUint32(parts[4], &entry.value) || !valueFitsWidth(entry.width, entry.value))
    {
        if (error)
        {
            *error = "invalid value for width";
        }
        return false;
    }
    if (parts.size() == 6)
    {
        entry.hasCompare = true;
        if (!parseUint32(parts[5], &entry.compareValue) || !valueFitsWidth(entry.width, entry.compareValue))
        {
            if (error)
            {
                *error = "invalid compare value for width";
            }
            return false;
        }
    }

    *out = entry;
    return true;
}

void cheatClearSet(CheatSet* set)
{
    if (!set)
    {
        return;
    }
    set->sourcePath.clear();
    set->appSha256.clear();
    set->entries.clear();
    set->parseErrors = 0;
}

bool cheatParseText(const std::string& text, const std::string& sourcePath, CheatSet* out, std::string* error)
{
    if (!out)
    {
        if (error)
        {
            *error = "missing output set";
        }
        return false;
    }

    cheatClearSet(out);
    out->sourcePath = sourcePath;

    size_t pos = 0;
    uint32_t lineNo = 0;
    while (pos <= text.size())
    {
        size_t lineEnd = text.find('\n', pos);
        std::string line = lineEnd == std::string::npos ? text.substr(pos) : text.substr(pos, lineEnd - pos);
        pos = lineEnd == std::string::npos ? text.size() + 1 : lineEnd + 1;
        lineNo++;

        line = trimText(stripInlineComment(line));
        if (line.empty())
        {
            continue;
        }

        size_t eq = line.find('=');
        size_t pipe = line.find('|');
        if (eq != std::string::npos && (pipe == std::string::npos || eq < pipe))
        {
            std::string key = lowerText(trimText(line.substr(0, eq)));
            std::string value = trimText(line.substr(eq + 1));
            if (key == "app_sha256")
            {
                // app_sha256 is validation only. Runtime lookup stays tied to
                // the same-name .cht file so stale hash-named files are ignored.
                if (!isHexSha256(value))
                {
                    out->parseErrors++;
                    printf("cheat: %s:%u invalid app_sha256\n",
                        sourcePath.empty() ? "<memory>" : sourcePath.c_str(), lineNo);
                    continue;
                }
                out->appSha256 = value;
            }
            else if (key != "format")
            {
                out->parseErrors++;
                printf("cheat: %s:%u unknown metadata key: %s\n",
                    sourcePath.empty() ? "<memory>" : sourcePath.c_str(), lineNo, key.c_str());
            }
            continue;
        }

        CheatEntry entry;
        std::string lineError;
        if (!parseCheatLine(line, &entry, &lineError))
        {
            out->parseErrors++;
            printf("cheat: %s:%u ignored bad code: %s\n",
                sourcePath.empty() ? "<memory>" : sourcePath.c_str(), lineNo, lineError.c_str());
            continue;
        }
        out->entries.push_back(entry);
    }

    if (out->entries.empty())
    {
        if (error)
        {
            *error = out->parseErrors ? "no valid cheat codes after parse errors" : "no cheat codes";
        }
        return false;
    }

    if (error)
    {
        error->clear();
    }
    return true;
}

bool cheatLoadFile(const std::string& path, CheatSet* out, std::string* error)
{
#ifdef _WIN32
    FILE* file = _wfopen(utf8ToWide(path).c_str(), L"rb");
#else
    FILE* file = fopen(path.c_str(), "rb");
#endif
    if (!file)
    {
        if (error)
        {
            *error = "failed to open file";
        }
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        if (error)
        {
            *error = "failed to seek file";
        }
        return false;
    }
    long size = ftell(file);
    if (size < 0 || size > 1024 * 1024)
    {
        fclose(file);
        if (error)
        {
            *error = "file size is invalid";
        }
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        if (error)
        {
            *error = "failed to rewind file";
        }
        return false;
    }

    std::string text;
    text.assign((size_t)size, '\0');
    if (size > 0 && fread(&text[0], 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        if (error)
        {
            *error = "failed to read file";
        }
        return false;
    }
    fclose(file);

    if (text.size() >= 3 &&
        (uint8_t)text[0] == 0xef && (uint8_t)text[1] == 0xbb && (uint8_t)text[2] == 0xbf)
    {
        text.erase(0, 3);
    }

    return cheatParseText(text, path, out, error);
}

bool cheatSetMatchesApp(const CheatSet& set, const char* appSha256)
{
    if (set.appSha256.empty())
    {
        return true;
    }
    if (!appSha256 || !appSha256[0])
    {
        return false;
    }
    return equalsIgnoreCase(set.appSha256, appSha256);
}

CheatApplyStats cheatApply(CheatSet* set, CheatReadCallback readCallback, CheatWriteCallback writeCallback,
    void* userData, CheatApplyPhase phase)
{
    (void)phase;
    CheatApplyStats stats;
    memset(&stats, 0x00, sizeof(stats));
    if (!set || !readCallback || !writeCallback)
    {
        return stats;
    }

    for (size_t i = 0; i < set->entries.size(); ++i)
    {
        CheatEntry& entry = set->entries[i];
        if (!entry.enabled)
        {
            stats.skippedDisabled++;
            continue;
        }
        if (entry.once && entry.appliedOnce)
        {
            stats.skippedOnce++;
            continue;
        }

        uint32_t widthBytes = cheatWidthBytes(entry.width);
        uint8_t bytes[4] = {};
        stats.attempted++;

        if (entry.hasCompare)
        {
            if (!readCallback(userData, entry.address, bytes, widthBytes))
            {
                stats.readFailures++;
                continue;
            }
            uint32_t current = readGuestLe(bytes, entry.width);
            if (current != entry.compareValue)
            {
                stats.skippedCompare++;
                continue;
            }
        }

        writeGuestLe(bytes, entry.width, entry.value);
        if (!writeCallback(userData, entry.address, bytes, widthBytes))
        {
            stats.writeFailures++;
            continue;
        }
        if (entry.once)
        {
            entry.appliedOnce = true;
            stats.appliedOnce++;
        }
        stats.applied++;
    }

    return stats;
}

uint32_t cheatWidthBytes(CheatWidth width)
{
    switch (width)
    {
    case CHEAT_WIDTH_U8:
        return 1;
    case CHEAT_WIDTH_U16:
        return 2;
    case CHEAT_WIDTH_U32:
        return 4;
    default:
        return 0;
    }
}

const char* cheatWidthName(CheatWidth width)
{
    switch (width)
    {
    case CHEAT_WIDTH_U8:
        return "u8";
    case CHEAT_WIDTH_U16:
        return "u16";
    case CHEAT_WIDTH_U32:
        return "u32";
    default:
        return "unknown";
    }
}

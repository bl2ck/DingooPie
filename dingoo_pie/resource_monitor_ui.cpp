#include "resource_monitor_ui.h"

#ifdef _WIN32

#include "emulator_core.h"
#include "platform_win32.h"
#include "resource_ids.h"
#include "runtime_resource_monitor.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <wchar.h>
#include <windows.h>

static HWND g_resourceMonitorWindow = NULL;
static HWND g_resourceMonitorStatus = NULL;
static HBRUSH g_resourceMonitorBackgroundBrush = NULL;
static HFONT g_resourceMonitorFont = NULL;
static bool g_resourceMonitorOwnFont = false;
static UiLanguage g_resourceMonitorLanguage = UI_LANGUAGE_ENGLISH;
static uint64_t g_resourceMonitorDisplayedRevision = UINT64_MAX;
static uint64_t g_resourceMonitorBaselineRevision = UINT64_MAX;
static uint64_t g_resourceMonitorHighlightUntil = 0;
static std::string g_resourceMonitorBaselineAppSha256;
static std::map<std::string, uint64_t> g_resourceMonitorObservedLoadedRevisions;

enum ResourceMonitorListSlot
{
    RESOURCE_MONITOR_LIST_SLOT_LOADED,
    RESOURCE_MONITOR_LIST_SLOT_UNLOADED,
    RESOURCE_MONITOR_LIST_SLOT_COUNT
};

enum ResourceMonitorHighlightKind
{
    RESOURCE_MONITOR_HIGHLIGHT_NONE,
    RESOURCE_MONITOR_HIGHLIGHT_NEW,
    RESOURCE_MONITOR_HIGHLIGHT_UPDATE,
    RESOURCE_MONITOR_HIGHLIGHT_CLOSED
};

struct ResourceMonitorDisplayRow
{
    RuntimeResourceMonitorEntry entry;
    ResourceMonitorHighlightKind highlightKind;
};

struct ResourceMonitorListLayout
{
    int labelY;
    int listY;
    int listHeight;
    int controlId;
};

struct ResourceMonitorColumnSpec
{
    const wchar_t* title;
    int width;
};

enum ResourceMonitorColumn
{
    RESOURCE_MONITOR_COLUMN_RESOURCE,
    RESOURCE_MONITOR_COLUMN_SOURCE,
    RESOURCE_MONITOR_COLUMN_GUEST_ADDRESS,
    RESOURCE_MONITOR_COLUMN_READ_COUNT,
    RESOURCE_MONITOR_COLUMN_RESOURCE_SIZE,
    RESOURCE_MONITOR_COLUMN_LOADED_BYTES,
    RESOURCE_MONITOR_COLUMN_FILE_OFFSET,
    RESOURCE_MONITOR_COLUMN_READ_POSITION,
    RESOURCE_MONITOR_COLUMN_PREVIEW,
    RESOURCE_MONITOR_COLUMN_REQUEST,
    RESOURCE_MONITOR_COLUMN_ACTION,
    RESOURCE_MONITOR_COLUMN_COUNT
};

struct ResourceMonitorStats
{
    unsigned count;
    unsigned packageCount;
    unsigned internalCount;
    unsigned externalCount;
    uint64_t readCalls;
    uint64_t readBytes;
};

static HWND g_resourceMonitorLists[RESOURCE_MONITOR_LIST_SLOT_COUNT] = {};
static std::vector<ResourceMonitorDisplayRow> g_resourceMonitorRows[RESOURCE_MONITOR_LIST_SLOT_COUNT];

static const int kResourceMonitorWindowWidth = 980;
static const int kResourceMonitorWindowHeight = 760;
static const int kResourceMonitorMargin = 18;
static const int kResourceMonitorRightX = 944;
static const int kResourceMonitorButtonY = 10;
static const int kResourceMonitorButtonGap = 10;
static const int kResourceMonitorButtonWidth = 74;
static const int kResourceMonitorButtonHeight = 28;
static const int kResourceMonitorCloseButtonX = kResourceMonitorRightX - kResourceMonitorButtonWidth;
static const int kResourceMonitorRefreshButtonX =
    kResourceMonitorCloseButtonX - kResourceMonitorButtonGap - kResourceMonitorButtonWidth;
static const int kResourceMonitorExportButtonX =
    kResourceMonitorRefreshButtonX - kResourceMonitorButtonGap - kResourceMonitorButtonWidth;
static const int kResourceMonitorTopTextY = 18;
static const int kResourceMonitorTopTextWidth =
    kResourceMonitorExportButtonX - kResourceMonitorButtonGap - kResourceMonitorMargin;
static const int kResourceMonitorSectionLabelHeight = 22;
static const int kResourceMonitorListWidth = kResourceMonitorRightX - kResourceMonitorMargin;
static const int kResourceMonitorStatusY = 690;
static const int kResourceMonitorBottomTextHeight = 22;
static const int kResourceMonitorTimerId = 1;
static const int kResourceMonitorTimerMs = 500;
static const uint64_t kResourceMonitorHighlightMs = 3000;

static const int kResourceMonitorIdExport = 47001;
static const int kResourceMonitorIdRefresh = 47002;
static const int kResourceMonitorIdClose = 47003;
static const int kResourceMonitorIdLoadedList = 47004;
static const int kResourceMonitorIdUnloadedList = 47005;
static const int kResourceMonitorIdStatus = 47006;

static const ResourceMonitorListLayout kResourceMonitorListLayouts[RESOURCE_MONITOR_LIST_SLOT_COUNT] = {
    { 54, 78, 384, kResourceMonitorIdLoadedList },
    { 474, 498, 180, kResourceMonitorIdUnloadedList }
};

static ResourceMonitorListSlot g_resourceMonitorPreferredExportSlot =
    RESOURCE_MONITOR_LIST_SLOT_LOADED;
static bool g_resourceMonitorHasPreferredExportSlot = false;

static bool resourceMonitorChinese(void)
{
    return g_resourceMonitorLanguage == UI_LANGUAGE_CHINESE;
}

static const wchar_t* resourceMonitorTitle(void)
{
    return resourceMonitorChinese() ? L"\u8d44\u6e90\u76d1\u89c6\u5668" : L"Resource Monitor";
}

static const wchar_t* resourceMonitorListTitle(ResourceMonitorListSlot slot)
{
    switch (slot)
    {
    case RESOURCE_MONITOR_LIST_SLOT_LOADED:
        return resourceMonitorChinese() ?
            L"\u5df2\u52a0\u8f7d\u8d44\u6e90" : L"Loaded resources";
    case RESOURCE_MONITOR_LIST_SLOT_UNLOADED:
        return resourceMonitorChinese() ?
            L"\u5df2\u5378\u8f7d\u8d44\u6e90" : L"Unloaded resources";
    default:
        return L"";
    }
}

static bool resourceMonitorListSlotForControlId(int controlId, ResourceMonitorListSlot* out)
{
    for (int i = 0; i < RESOURCE_MONITOR_LIST_SLOT_COUNT; ++i)
    {
        if (kResourceMonitorListLayouts[i].controlId == controlId)
        {
            if (out)
            {
                *out = (ResourceMonitorListSlot)i;
            }
            return true;
        }
    }
    return false;
}

static void resetResourceMonitorDisplayTracking(void)
{
    g_resourceMonitorDisplayedRevision = UINT64_MAX;
    g_resourceMonitorBaselineRevision = UINT64_MAX;
    g_resourceMonitorHighlightUntil = 0;
    g_resourceMonitorBaselineAppSha256.clear();
    g_resourceMonitorObservedLoadedRevisions.clear();
    g_resourceMonitorHasPreferredExportSlot = false;
}

static HICON loadResourceMonitorIcon(int size)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED);
}

static void applyResourceMonitorIcon(HWND window)
{
    if (!window)
    {
        return;
    }
    HICON largeIcon = loadResourceMonitorIcon(32);
    HICON smallIcon = loadResourceMonitorIcon(16);
    if (largeIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    }
    if (smallIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    }
}

static HFONT resourceMonitorFont(void)
{
    if (g_resourceMonitorFont)
    {
        return g_resourceMonitorFont;
    }

    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        g_resourceMonitorFont = CreateFontIndirectW(&metrics.lfMessageFont);
        g_resourceMonitorOwnFont = g_resourceMonitorFont != NULL;
    }
    if (!g_resourceMonitorFont)
    {
        g_resourceMonitorFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_resourceMonitorOwnFont = false;
    }
    return g_resourceMonitorFont;
}

static HWND createResourceMonitorChild(const wchar_t* className, const wchar_t* text,
    DWORD exStyle, DWORD style, int x, int y, int w, int h, int id)
{
    DWORD childStyle = (DWORD)platformWin32NormalizeChildStyle(className, style);
    HWND child = CreateWindowExW(exStyle, className, text,
        WS_CHILD | WS_VISIBLE | childStyle,
        x, y, w, h, g_resourceMonitorWindow, (HMENU)(INT_PTR)id,
        GetModuleHandleW(NULL), NULL);
    if (child)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)resourceMonitorFont(), TRUE);
    }
    return child;
}

static HWND createResourceMonitorButton(const wchar_t* text, int x, int y, int w, int id)
{
    return createResourceMonitorChild(L"BUTTON", text, 0, 0,
        x, y, w, kResourceMonitorButtonHeight, id);
}

static void setResourceMonitorStatus(const wchar_t* text)
{
    if (!g_resourceMonitorStatus)
    {
        return;
    }
    SetWindowTextW(g_resourceMonitorStatus, text ? text : L"");
    InvalidateRect(g_resourceMonitorStatus, NULL, TRUE);
}

static void setResourceMonitorListText(HWND list, int row, int column, const wchar_t* text)
{
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = column;
    item.pszText = (LPWSTR)text;
    SendMessageW(list, LVM_SETITEMW, 0, (LPARAM)&item);
}

static void clearResourceMonitorRowHighlights(void)
{
    bool changed = false;
    for (int slot = 0; slot < RESOURCE_MONITOR_LIST_SLOT_COUNT; ++slot)
    {
        std::vector<ResourceMonitorDisplayRow>& rows = g_resourceMonitorRows[slot];
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].highlightKind != RESOURCE_MONITOR_HIGHLIGHT_NONE)
            {
                rows[i].highlightKind = RESOURCE_MONITOR_HIGHLIGHT_NONE;
                changed = true;
            }
        }
    }

    if (changed)
    {
        for (int slot = 0; slot < RESOURCE_MONITOR_LIST_SLOT_COUNT; ++slot)
        {
            if (g_resourceMonitorLists[slot])
            {
                InvalidateRect(g_resourceMonitorLists[slot], NULL, TRUE);
            }
        }
    }
    g_resourceMonitorHighlightUntil = 0;
}

static void formatResourceMonitorHex32(uint32_t value, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }
    swprintf(out, outCount, L"0x%08X", value);
}

static void formatResourceMonitorU64(uint64_t value, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }
    swprintf(out, outCount, L"%llu", (unsigned long long)value);
}

static void formatResourceMonitorBytes(uint64_t value, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }

    static const wchar_t* kUnits[] = { L"B", L"KB", L"MB", L"GB" };
    double scaled = (double)value;
    size_t unit = 0;
    while (scaled >= 1024.0 && unit + 1 < sizeof(kUnits) / sizeof(kUnits[0]))
    {
        scaled /= 1024.0;
        unit++;
    }

    if (unit == 0)
    {
        swprintf(out, outCount, L"%llu %ls", (unsigned long long)value, kUnits[unit]);
    }
    else if (scaled >= 100.0)
    {
        swprintf(out, outCount, L"%.0f %ls", scaled, kUnits[unit]);
    }
    else
    {
        swprintf(out, outCount, L"%.1f %ls", scaled, kUnits[unit]);
    }
}

static bool resourceMonitorHasHighBitBytes(const std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (((unsigned char)text[i]) >= 0x80)
        {
            return true;
        }
    }
    return false;
}

static bool resourceMonitorConvertCodePage(
    const std::string& text,
    UINT codePage,
    DWORD flags,
    std::wstring* out)
{
    if (!out)
    {
        return false;
    }
    if (text.empty())
    {
        out->clear();
        return true;
    }

    int size = MultiByteToWideChar(
        codePage,
        flags,
        text.c_str(),
        (int)text.size(),
        NULL,
        0);
    if (size <= 0)
    {
        return false;
    }

    std::wstring converted((size_t)size, L'\0');
    int written = MultiByteToWideChar(
        codePage,
        flags,
        text.c_str(),
        (int)text.size(),
        &converted[0],
        size);
    if (written != size)
    {
        return false;
    }
    *out = converted;
    return true;
}

static std::wstring resourceMonitorNameText(const std::string& text)
{
    // Resource names come from guest app metadata and may be UTF-8 or GBK bytes.
    std::wstring out;
    if (resourceMonitorConvertCodePage(text, CP_UTF8, MB_ERR_INVALID_CHARS, &out))
    {
        return out;
    }
    if (resourceMonitorHasHighBitBytes(text) &&
        resourceMonitorConvertCodePage(text, 936, 0, &out))
    {
        return out;
    }
    if (resourceMonitorConvertCodePage(text, CP_ACP, 0, &out))
    {
        return out;
    }
    return platformUtf8ToWide(text);
}

static std::wstring resourceMonitorPreviewText(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty())
    {
        return L"";
    }

    std::wstring text;
    wchar_t byteText[4] = {};
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        if (i)
        {
            text += L" ";
        }
        swprintf(byteText, sizeof(byteText) / sizeof(byteText[0]), L"%02X", bytes[i]);
        text += byteText;
    }
    return text;
}

static std::string resourceMonitorAppDirectory(const std::string& appPath)
{
    size_t slash = appPath.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : appPath.substr(0, slash);
}

static std::string resourceMonitorPathBasename(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::string resourceMonitorNormalizeGuestPath(const std::string& path)
{
    const char* in = path.c_str();
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

    std::string normalized;
    while (*in)
    {
        normalized += (*in == '\\') ? '/' : *in;
        in++;
    }
    return normalized;
}

static void resourceMonitorAppendUniquePath(
    std::vector<std::string>* paths,
    const std::string& path)
{
    if (!paths || path.empty())
    {
        return;
    }
    if (std::find(paths->begin(), paths->end(), path) == paths->end())
    {
        paths->push_back(path);
    }
}

static FILE* resourceMonitorOpenInputPath(const std::string& path)
{
    if (path.empty())
    {
        return NULL;
    }

    FILE* file = _wfopen(platformUtf8ToWide(path).c_str(), L"rb");
    if (!file)
    {
        file = fopen(path.c_str(), "rb");
    }
    return file;
}

static FILE* resourceMonitorOpenExternalInput(
    const RuntimeResourceMonitorSnapshot& snapshot,
    const RuntimeResourceMonitorEntry& entry)
{
    std::vector<std::string> paths;
    std::string appDir = resourceMonitorAppDirectory(snapshot.appPath);
    std::string normalized = resourceMonitorNormalizeGuestPath(entry.name);
    std::string base = resourceMonitorPathBasename(entry.name);
    resourceMonitorAppendUniquePath(&paths, entry.name);
    resourceMonitorAppendUniquePath(&paths, normalized);
    if (!appDir.empty())
    {
        resourceMonitorAppendUniquePath(&paths, appDir + "\\" + entry.name);
        resourceMonitorAppendUniquePath(&paths, appDir + "\\" + normalized);
        resourceMonitorAppendUniquePath(&paths, appDir + "\\" + base);
    }
    resourceMonitorAppendUniquePath(&paths, base);

    for (size_t i = 0; i < paths.size(); ++i)
    {
        FILE* file = resourceMonitorOpenInputPath(paths[i]);
        if (file)
        {
            return file;
        }
    }
    return NULL;
}

static bool resourceMonitorCopyRange(
    FILE* input,
    FILE* output,
    uint32_t offset,
    uint32_t size,
    uint8_t xorKey)
{
    if (!input || !output || size == 0)
    {
        return false;
    }
    if (_fseeki64(input, offset, SEEK_SET) != 0)
    {
        return false;
    }

    uint8_t buffer[64 * 1024];
    uint32_t remaining = size;
    while (remaining)
    {
        size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        size_t read = fread(buffer, 1, chunk, input);
        if (read != chunk)
        {
            return false;
        }
        if (xorKey)
        {
            for (size_t i = 0; i < read; ++i)
            {
                buffer[i] ^= xorKey;
            }
        }
        if (fwrite(buffer, 1, read, output) != read)
        {
            return false;
        }
        remaining -= (uint32_t)read;
    }
    return true;
}

static bool resourceMonitorExportEntryToFile(
    const RuntimeResourceMonitorSnapshot& snapshot,
    const RuntimeResourceMonitorEntry& entry,
    const std::wstring& outPath,
    std::wstring* error)
{
    if (entry.size == 0)
    {
        if (error)
        {
            *error = resourceMonitorChinese() ?
                L"\u8d44\u6e90\u5927\u5c0f\u4e3a 0\u3002" : L"Resource size is 0.";
        }
        return false;
    }

    FILE* input = NULL;
    uint8_t xorKey = 0;
    if (entry.externalFileSeen)
    {
        input = resourceMonitorOpenExternalInput(snapshot, entry);
    }
    else
    {
        input = resourceMonitorOpenInputPath(snapshot.appPath);
        xorKey = entry.appPackageSeen ? 0 : entry.xorKey;
    }
    if (!input)
    {
        if (error)
        {
            *error = resourceMonitorChinese() ?
                L"\u65e0\u6cd5\u6253\u5f00\u8d44\u6e90\u6765\u6e90\u6587\u4ef6\u3002" :
                L"Could not open the resource source file.";
        }
        return false;
    }

    FILE* output = _wfopen(outPath.c_str(), L"wb");
    if (!output)
    {
        fclose(input);
        if (error)
        {
            *error = resourceMonitorChinese() ?
                L"\u65e0\u6cd5\u521b\u5efa\u5bfc\u51fa\u6587\u4ef6\u3002" :
                L"Could not create the export file.";
        }
        return false;
    }

    bool ok = resourceMonitorCopyRange(input, output, entry.offset, entry.size, xorKey);
    fclose(output);
    fclose(input);
    if (!ok)
    {
        _wremove(outPath.c_str());
        if (error)
        {
            *error = resourceMonitorChinese() ?
                L"\u8bfb\u53d6\u6216\u5199\u5165\u8d44\u6e90\u6570\u636e\u5931\u8d25\u3002" :
                L"Failed to read or write resource data.";
        }
    }
    return ok;
}

static std::wstring resourceMonitorDefaultExportFileName(
    const RuntimeResourceMonitorEntry& entry)
{
    std::wstring name = resourceMonitorNameText(entry.name);
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        name = name.substr(slash + 1);
    }
    for (size_t i = 0; i < name.size(); ++i)
    {
        wchar_t c = name[i];
        if (c < 32 || wcschr(L"<>:\"/\\|?*", c))
        {
            name[i] = L'_';
        }
    }
    while (!name.empty() && (name[name.size() - 1] == L'.' ||
        name[name.size() - 1] == L' '))
    {
        name.erase(name.size() - 1);
    }
    if (name.empty() || name == L"(unnamed)")
    {
        wchar_t fallback[32] = {};
        swprintf(fallback, sizeof(fallback) / sizeof(fallback[0]),
            L"resource_%08X", entry.offset);
        name = fallback;
    }
    if (name.find_last_of(L'.') == std::wstring::npos)
    {
        name += L".bin";
    }
    return name;
}

static bool resourceMonitorPathHasExtension(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    return dot != std::wstring::npos &&
        (slash == std::wstring::npos || dot > slash);
}

static bool resourceMonitorPickExportPath(
    const RuntimeResourceMonitorEntry& entry,
    std::wstring* out)
{
    if (!out)
    {
        return false;
    }

    wchar_t fileName[MAX_PATH] = {};
    std::wstring defaultName = resourceMonitorDefaultExportFileName(entry);
    wcsncpy(fileName, defaultName.c_str(), MAX_PATH - 1);

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_resourceMonitorWindow;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = resourceMonitorChinese() ?
        L"\u4e8c\u8fdb\u5236\u8d44\u6e90 (*.bin)\0*.bin\0\u6240\u6709\u6587\u4ef6 (*.*)\0*.*\0" :
        L"Binary resource (*.bin)\0*.bin\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = resourceMonitorChinese() ?
        L"\u5bfc\u51fa\u4e8c\u8fdb\u5236\u8d44\u6e90" : L"Export Binary Resource";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn))
    {
        return false;
    }

    std::wstring path(fileName);
    if (ofn.nFilterIndex == 1 && !resourceMonitorPathHasExtension(path))
    {
        path += L".bin";
    }
    *out = path;
    return true;
}

static std::wstring resourceMonitorSourceText(const RuntimeResourceMonitorEntry& entry)
{
    if (entry.appPackageSeen)
    {
        return resourceMonitorChinese() ? L"App \u5305" : L"app package";
    }
    if (entry.externalFileSeen)
    {
        return resourceMonitorChinese() ? L"\u5916\u90e8\u6587\u4ef6" : L"external";
    }
    if (!entry.fsysSeen && !entry.dlResSeen)
    {
        return resourceMonitorChinese() ? L"\u672a\u8bbf\u95ee" : L"pending";
    }
    if (entry.fsysSeen || entry.dlResSeen)
    {
        return resourceMonitorChinese() ? L"\u5185\u90e8\u8d44\u6e90" : L"internal";
    }
    return resourceMonitorChinese() ? L"\u672a\u8bbf\u95ee" : L"pending";
}

static bool resourceMonitorEntryTouched(const RuntimeResourceMonitorEntry& entry)
{
    return entry.openCount || entry.readCalls || entry.seekCalls || entry.closeCount ||
        entry.fsysSeen || entry.dlResSeen || entry.appPackageSeen || entry.externalFileSeen;
}

static bool resourceMonitorEntryLoaded(const RuntimeResourceMonitorEntry& entry)
{
    return entry.lastLoadSize != 0 || entry.lastReadRevision != 0;
}

static bool resourceMonitorEntryClosedAfterLoad(const RuntimeResourceMonitorEntry& entry)
{
    return entry.activeHandles == 0 &&
        entry.lastCloseRevision &&
        entry.lastCloseRevision > entry.lastReadRevision;
}

static std::string resourceMonitorEntryKey(const RuntimeResourceMonitorEntry& entry)
{
    char suffix[64] = {};
    char source = 'R';
    if (entry.appPackageSeen)
    {
        source = 'P';
    }
    else if (entry.externalFileSeen)
    {
        source = 'E';
    }
    snprintf(suffix, sizeof(suffix), "|%c|%08x|%08x",
        source, entry.offset, entry.size);
    return entry.name + suffix;
}

static void resourceMonitorEnsureDisplayBaseline(
    const RuntimeResourceMonitorSnapshot& snapshot)
{
    if (g_resourceMonitorBaselineRevision != UINT64_MAX &&
        g_resourceMonitorBaselineAppSha256 == snapshot.appSha256)
    {
        return;
    }

    g_resourceMonitorBaselineRevision = snapshot.revision;
    g_resourceMonitorBaselineAppSha256 = snapshot.appSha256;
    g_resourceMonitorObservedLoadedRevisions.clear();
}

static uint64_t resourceMonitorEntryObservedLoadedRevision(
    const RuntimeResourceMonitorEntry& entry)
{
    std::string key = resourceMonitorEntryKey(entry);
    std::map<std::string, uint64_t>::const_iterator found =
        g_resourceMonitorObservedLoadedRevisions.find(key);
    return found == g_resourceMonitorObservedLoadedRevisions.end() ?
        UINT64_MAX : found->second;
}

static void resourceMonitorRememberLoadedEntry(
    const RuntimeResourceMonitorEntry& entry,
    uint64_t snapshotRevision)
{
    std::string key = resourceMonitorEntryKey(entry);
    if (g_resourceMonitorObservedLoadedRevisions.find(key) ==
        g_resourceMonitorObservedLoadedRevisions.end())
    {
        g_resourceMonitorObservedLoadedRevisions[key] = snapshotRevision;
    }
}

static bool resourceMonitorEntryShouldShowUnloaded(
    const RuntimeResourceMonitorEntry& entry)
{
    // A startup snapshot often contains resources whose read handle is already closed.
    // Show those as loaded first; only later observed closes should move below.
    uint64_t observedRevision = resourceMonitorEntryObservedLoadedRevision(entry);
    return resourceMonitorEntryClosedAfterLoad(entry) &&
        g_resourceMonitorBaselineRevision != UINT64_MAX &&
        entry.lastCloseRevision > g_resourceMonitorBaselineRevision &&
        observedRevision != UINT64_MAX &&
        entry.lastCloseRevision > observedRevision;
}

static ResourceMonitorListSlot resourceMonitorListSlotForEntry(
    const RuntimeResourceMonitorEntry& entry)
{
    return resourceMonitorEntryShouldShowUnloaded(entry) ?
        RESOURCE_MONITOR_LIST_SLOT_UNLOADED : RESOURCE_MONITOR_LIST_SLOT_LOADED;
}

static uint64_t resourceMonitorEntrySortRevision(const RuntimeResourceMonitorEntry& entry)
{
    return entry.lastCloseRevision > entry.lastReadRevision ?
        entry.lastCloseRevision : entry.lastReadRevision;
}

static std::wstring resourceMonitorActionText(const RuntimeResourceMonitorEntry& entry)
{
    if (!resourceMonitorEntryTouched(entry))
    {
        return resourceMonitorChinese() ? L"\u7b49\u5f85\u8bbf\u95ee" : L"waiting";
    }
    if (resourceMonitorChinese())
    {
        if (entry.lastAction == "open")
        {
            return L"\u6253\u5f00";
        }
        if (entry.lastAction == "load")
        {
            return L"\u52a0\u8f7d";
        }
        if (entry.lastAction == "seek")
        {
            return L"\u5b9a\u4f4d";
        }
        if (entry.lastAction == "close")
        {
            return L"\u5173\u95ed";
        }
    }
    return platformUtf8ToWide(entry.lastAction);
}

static bool resourceMonitorEntrySortBefore(
    const RuntimeResourceMonitorEntry& a,
    const RuntimeResourceMonitorEntry& b)
{
    uint64_t aRevision = resourceMonitorEntrySortRevision(a);
    uint64_t bRevision = resourceMonitorEntrySortRevision(b);
    if (aRevision != bRevision)
    {
        return aRevision > bRevision;
    }
    if (a.lastRevision != b.lastRevision)
    {
        return a.lastRevision > b.lastRevision;
    }
    if (a.offset != b.offset)
    {
        return a.offset < b.offset;
    }
    if (a.size != b.size)
    {
        return a.size < b.size;
    }
    return a.name < b.name;
}

static ResourceMonitorStats collectResourceMonitorStats(
    const std::vector<ResourceMonitorDisplayRow>& rows)
{
    ResourceMonitorStats stats = {};
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const RuntimeResourceMonitorEntry& entry = rows[i].entry;
        stats.count++;
        if (entry.appPackageSeen)
        {
            stats.packageCount++;
        }
        else if (entry.externalFileSeen)
        {
            stats.externalCount++;
        }
        else
        {
            stats.internalCount++;
        }
        stats.readCalls += entry.readCalls;
        stats.readBytes += entry.readBytes;
    }
    return stats;
}

static void setupResourceMonitorList(HWND list)
{
    DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
    ListView_SetExtendedListViewStyle(list, exStyle);

    const ResourceMonitorColumnSpec columns[RESOURCE_MONITOR_COLUMN_COUNT] = {
        { resourceMonitorChinese() ? L"\u8d44\u6e90\u540d" : L"Resource", 180 },
        { resourceMonitorChinese() ? L"\u6765\u6e90" : L"Source", 76 },
        { resourceMonitorChinese() ? L"\u76ee\u6807\u5730\u5740" : L"Dst Addr", 92 },
        { resourceMonitorChinese() ? L"\u8bfb\u8c03\u7528" : L"Read Calls", 76 },
        { resourceMonitorChinese() ? L"\u6761\u76ee\u5927\u5c0f" : L"Entry Size", 96 },
        { resourceMonitorChinese() ? L"\u52a0\u8f7d\u5b57\u8282" : L"Loaded Bytes", 96 },
        { resourceMonitorChinese() ? L"\u6587\u4ef6\u504f\u79fb" : L"File Offset", 90 },
        { resourceMonitorChinese() ? L"\u6d41\u4f4d\u7f6e" : L"Stream Pos", 90 },
        { resourceMonitorChinese() ? L"\u5b57\u8282\u9884\u89c8" : L"Byte Preview", 230 },
        { resourceMonitorChinese() ? L"\u8bf7\u6c42\u540d" : L"Request", 150 },
        { resourceMonitorChinese() ? L"\u52a8\u4f5c" : L"Action", 64 }
    };

    for (int i = 0; i < RESOURCE_MONITOR_COLUMN_COUNT; ++i)
    {
        LVCOLUMNW column;
        memset(&column, 0, sizeof(column));
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = (LPWSTR)columns[i].title;
        column.cx = columns[i].width;
        column.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&column);
    }
}

static void appendResourceMonitorRow(HWND list, int row, const RuntimeResourceMonitorEntry& entry)
{
    std::wstring name = resourceMonitorNameText(entry.name);
    std::wstring request = resourceMonitorNameText(entry.lastRequest);
    std::wstring source = resourceMonitorSourceText(entry);
    std::wstring action = resourceMonitorActionText(entry);
    std::wstring preview = resourceMonitorPreviewText(entry.lastPreview);
    wchar_t guest[32] = {};
    wchar_t readCalls[32] = {};
    wchar_t bytes[32] = {};
    wchar_t size[32] = {};
    wchar_t offset[32] = {};
    wchar_t position[32] = {};
    formatResourceMonitorHex32(entry.lastGuestAddress, guest, sizeof(guest) / sizeof(guest[0]));
    formatResourceMonitorU64(entry.readCalls, readCalls, sizeof(readCalls) / sizeof(readCalls[0]));
    formatResourceMonitorBytes(entry.readBytes, bytes, sizeof(bytes) / sizeof(bytes[0]));
    formatResourceMonitorBytes(entry.size, size, sizeof(size) / sizeof(size[0]));
    formatResourceMonitorHex32(entry.offset, offset, sizeof(offset) / sizeof(offset[0]));
    formatResourceMonitorHex32(entry.lastPosition, position, sizeof(position) / sizeof(position[0]));

    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = RESOURCE_MONITOR_COLUMN_RESOURCE;
    item.pszText = (LPWSTR)(name.empty() ? L"(unnamed)" : name.c_str());
    SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_SOURCE, source.c_str());
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_GUEST_ADDRESS, guest);
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_READ_COUNT, readCalls);
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_RESOURCE_SIZE, size);
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_LOADED_BYTES, bytes);
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_FILE_OFFSET, offset);
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_READ_POSITION, position);
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_PREVIEW, preview.c_str());
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_REQUEST, request.c_str());
    setResourceMonitorListText(list, row, RESOURCE_MONITOR_COLUMN_ACTION, action.c_str());
}

static void refreshResourceMonitorList(
    HWND list,
    const std::vector<ResourceMonitorDisplayRow>& rows)
{
    if (!list)
    {
        return;
    }

    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        appendResourceMonitorRow(list, (int)i, rows[i].entry);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, NULL, TRUE);
}

static bool resourceMonitorSelectedRow(ResourceMonitorListSlot* outSlot, int* outRow)
{
    if (g_resourceMonitorHasPreferredExportSlot)
    {
        int slot = (int)g_resourceMonitorPreferredExportSlot;
        HWND list = g_resourceMonitorLists[slot];
        int row = list ? ListView_GetNextItem(list, -1, LVNI_SELECTED) : -1;
        if (row >= 0 && (size_t)row < g_resourceMonitorRows[slot].size())
        {
            if (outSlot)
            {
                *outSlot = (ResourceMonitorListSlot)slot;
            }
            if (outRow)
            {
                *outRow = row;
            }
            return true;
        }
    }

    HWND focused = GetFocus();
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int slot = 0; slot < RESOURCE_MONITOR_LIST_SLOT_COUNT; ++slot)
        {
            HWND list = g_resourceMonitorLists[slot];
            if (!list)
            {
                continue;
            }
            if (pass == 0 && focused != list)
            {
                continue;
            }

            int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
            if (row >= 0 && (size_t)row < g_resourceMonitorRows[slot].size())
            {
                if (outSlot)
                {
                    *outSlot = (ResourceMonitorListSlot)slot;
                }
                if (outRow)
                {
                    *outRow = row;
                }
                return true;
            }
        }
    }
    return false;
}

static void resourceMonitorRememberExportSlot(ResourceMonitorListSlot slot)
{
    g_resourceMonitorPreferredExportSlot = slot;
    g_resourceMonitorHasPreferredExportSlot = true;
}

static void exportSelectedResourceMonitorRow(void)
{
    ResourceMonitorListSlot slot = RESOURCE_MONITOR_LIST_SLOT_LOADED;
    int row = -1;
    if (!resourceMonitorSelectedRow(&slot, &row))
    {
        MessageBoxW(g_resourceMonitorWindow,
            resourceMonitorChinese() ?
            L"\u8bf7\u5148\u9009\u62e9\u8981\u5bfc\u51fa\u7684\u8d44\u6e90\u3002" :
            L"Select a resource to export first.",
            resourceMonitorTitle(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    RuntimeResourceMonitorEntry entry = g_resourceMonitorRows[slot][row].entry;
    std::wstring outPath;
    if (!resourceMonitorPickExportPath(entry, &outPath))
    {
        return;
    }

    RuntimeResourceMonitorSnapshot snapshot = runtimeResourceMonitorGetSnapshot();
    std::wstring error;
    bool ok = resourceMonitorExportEntryToFile(snapshot, entry, outPath, &error);
    MessageBoxW(g_resourceMonitorWindow,
        ok ? (resourceMonitorChinese() ?
            L"\u4e8c\u8fdb\u5236\u8d44\u6e90\u5df2\u5bfc\u51fa\u3002" :
            L"Binary resource exported.") :
            (error.empty() ? (resourceMonitorChinese() ?
                L"\u5bfc\u51fa\u4e8c\u8fdb\u5236\u8d44\u6e90\u5931\u8d25\u3002" :
                L"Failed to export binary resource.") : error.c_str()),
        resourceMonitorTitle(),
        ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
}

static void refreshResourceMonitorWindow(bool force)
{
    if (!g_resourceMonitorWindow)
    {
        return;
    }
    for (int slot = 0; slot < RESOURCE_MONITOR_LIST_SLOT_COUNT; ++slot)
    {
        if (!g_resourceMonitorLists[slot])
        {
            return;
        }
    }

    RuntimeResourceMonitorSnapshot snapshot = runtimeResourceMonitorGetSnapshot();
    resourceMonitorEnsureDisplayBaseline(snapshot);
    uint64_t now = GetTickCount64();
    if (!force && snapshot.revision == g_resourceMonitorDisplayedRevision)
    {
        if (g_resourceMonitorHighlightUntil && now >= g_resourceMonitorHighlightUntil)
        {
            refreshResourceMonitorWindow(true);
        }
        return;
    }
    uint64_t previousRevision = g_resourceMonitorDisplayedRevision;
    g_resourceMonitorDisplayedRevision = snapshot.revision;

    std::stable_sort(snapshot.entries.begin(), snapshot.entries.end(), resourceMonitorEntrySortBefore);
    for (int slot = 0; slot < RESOURCE_MONITOR_LIST_SLOT_COUNT; ++slot)
    {
        g_resourceMonitorRows[slot].clear();
        g_resourceMonitorRows[slot].reserve(snapshot.entries.size());
    }
    bool hasHighlight = false;
    for (size_t i = 0; i < snapshot.entries.size(); ++i)
    {
        const RuntimeResourceMonitorEntry& entry = snapshot.entries[i];
        if (!resourceMonitorEntryLoaded(entry))
        {
            continue;
        }
        ResourceMonitorListSlot slot = resourceMonitorListSlotForEntry(entry);
        ResourceMonitorDisplayRow row;
        row.entry = entry;
        row.highlightKind = RESOURCE_MONITOR_HIGHLIGHT_NONE;
        if (slot == RESOURCE_MONITOR_LIST_SLOT_UNLOADED)
        {
            if (previousRevision != UINT64_MAX &&
                row.entry.lastCloseRevision > previousRevision)
            {
                row.highlightKind = RESOURCE_MONITOR_HIGHLIGHT_CLOSED;
            }
        }
        else if (previousRevision != UINT64_MAX &&
            row.entry.lastReadRevision > previousRevision)
        {
            row.highlightKind = row.entry.readCalls <= 1 ?
                RESOURCE_MONITOR_HIGHLIGHT_NEW : RESOURCE_MONITOR_HIGHLIGHT_UPDATE;
        }
        hasHighlight = hasHighlight ||
            row.highlightKind != RESOURCE_MONITOR_HIGHLIGHT_NONE;
        g_resourceMonitorRows[slot].push_back(row);
        if (slot == RESOURCE_MONITOR_LIST_SLOT_LOADED)
        {
            resourceMonitorRememberLoadedEntry(entry, snapshot.revision);
        }
    }
    g_resourceMonitorHighlightUntil = hasHighlight ?
        now + kResourceMonitorHighlightMs : 0;

    for (int slot = 0; slot < RESOURCE_MONITOR_LIST_SLOT_COUNT; ++slot)
    {
        refreshResourceMonitorList(g_resourceMonitorLists[slot], g_resourceMonitorRows[slot]);
    }

    EmulatorRuntimeAppInfo appInfo;
    bool running = emulatorRuntimeGetAppInfo(&appInfo);
    wchar_t status[320] = {};
    if (!running)
    {
        swprintf(status, sizeof(status) / sizeof(status[0]),
            resourceMonitorChinese() ?
            L"\u5f53\u524d\u6ca1\u6709\u8fd0\u884c\u4e2d\u7684 App\u3002\u4e0a\u65b9/\u4e0b\u65b9\u5217\u8868\u5206\u522b\u663e\u793a\u5df2\u52a0\u8f7d\u548c\u5df2\u5378\u8f7d\u7684\u8d44\u6e90\u6761\u76ee\u3002" :
            L"No app is running. The upper/lower lists show loaded and unloaded resource entries.");
    }
    else
    {
        ResourceMonitorStats loadedStats =
            collectResourceMonitorStats(g_resourceMonitorRows[RESOURCE_MONITOR_LIST_SLOT_LOADED]);
        ResourceMonitorStats unloadedStats =
            collectResourceMonitorStats(g_resourceMonitorRows[RESOURCE_MONITOR_LIST_SLOT_UNLOADED]);
        std::wstring appName = platformUtf8ToWide(appInfo.fileName);
        unsigned totalCount = loadedStats.count + unloadedStats.count;
        if (!totalCount)
        {
            swprintf(status, sizeof(status) / sizeof(status[0]),
                resourceMonitorChinese() ?
                L"App: %ls | \u5305\u7d22\u5f15: %u | \u8d44\u6e90\u8868: %u | \u5df2\u52a0\u8f7d: 0 | \u5df2\u5378\u8f7d: 0 | App \u5305/\u5185\u90e8/\u5916\u90e8: 0/0/0" :
                L"App: %ls | package index: %u | table: %u | loaded: 0 | unloaded: 0 | app/internal/external: 0/0/0",
                appName.empty() ? L"(unnamed)" : appName.c_str(),
                (unsigned)appInfo.packageResourceCount,
                (unsigned)appInfo.resourceCount);
        }
        else
        {
            uint64_t readCalls = loadedStats.readCalls + unloadedStats.readCalls;
            uint64_t readBytes = loadedStats.readBytes + unloadedStats.readBytes;
            unsigned packageCount = loadedStats.packageCount + unloadedStats.packageCount;
            unsigned internalCount = loadedStats.internalCount + unloadedStats.internalCount;
            unsigned externalCount = loadedStats.externalCount + unloadedStats.externalCount;
            wchar_t readBytesText[32] = {};
            formatResourceMonitorBytes(readBytes, readBytesText,
                sizeof(readBytesText) / sizeof(readBytesText[0]));
            swprintf(status, sizeof(status) / sizeof(status[0]),
                resourceMonitorChinese() ?
                L"App: %ls | \u5305\u7d22\u5f15: %u | \u8d44\u6e90\u8868: %u | \u5df2\u52a0\u8f7d: %u | \u5df2\u5378\u8f7d: %u | App \u5305/\u5185\u90e8/\u5916\u90e8: %u/%u/%u | \u8bfb\u53d6\u6b21\u6570: %llu | \u8bfb\u53d6\u5b57\u8282: %ls" :
                L"App: %ls | package index: %u | table: %u | loaded: %u | unloaded: %u | app/internal/external: %u/%u/%u | read count: %llu | read bytes: %ls",
                appName.empty() ? L"(unnamed)" : appName.c_str(),
                (unsigned)appInfo.packageResourceCount,
                (unsigned)appInfo.resourceCount,
                loadedStats.count,
                unloadedStats.count,
                packageCount,
                internalCount,
                externalCount,
                (unsigned long long)readCalls,
                readBytesText);
        }
    }
    setResourceMonitorStatus(status);
}

static void createResourceMonitorContents(void)
{
    createResourceMonitorButton(resourceMonitorChinese() ? L"\u5bfc\u51fa" : L"Export",
        kResourceMonitorExportButtonX, kResourceMonitorButtonY,
        kResourceMonitorButtonWidth, kResourceMonitorIdExport);
    createResourceMonitorButton(resourceMonitorChinese() ? L"\u5237\u65b0" : L"Refresh",
        kResourceMonitorRefreshButtonX, kResourceMonitorButtonY,
        kResourceMonitorButtonWidth, kResourceMonitorIdRefresh);
    createResourceMonitorButton(resourceMonitorChinese() ? L"\u5173\u95ed" : L"Close",
        kResourceMonitorCloseButtonX, kResourceMonitorButtonY,
        kResourceMonitorButtonWidth, kResourceMonitorIdClose);

    createResourceMonitorChild(L"STATIC",
        resourceMonitorChinese() ?
        L"\u4e0a\u65b9\u663e\u793a\u5df2\u52a0\u8f7d\u7684\u8d44\u6e90\u6761\u76ee\uff1b\u4e0b\u65b9\u4fdd\u7559\u5df2\u5378\u8f7d\u7684\u8d44\u6e90\u6761\u76ee\u3002" :
        L"Upper list: loaded resource entries. Lower list: unloaded resource entries.",
        0, 0, kResourceMonitorMargin, kResourceMonitorTopTextY,
        kResourceMonitorTopTextWidth, kResourceMonitorBottomTextHeight, -1);

    for (int slot = 0; slot < RESOURCE_MONITOR_LIST_SLOT_COUNT; ++slot)
    {
        const ResourceMonitorListLayout& layout = kResourceMonitorListLayouts[slot];
        createResourceMonitorChild(L"STATIC",
            resourceMonitorListTitle((ResourceMonitorListSlot)slot),
            0, 0, kResourceMonitorMargin, layout.labelY,
            kResourceMonitorListWidth, kResourceMonitorSectionLabelHeight, -1);

        g_resourceMonitorLists[slot] = createResourceMonitorChild(WC_LISTVIEWW, L"",
            WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            kResourceMonitorMargin, layout.listY,
            kResourceMonitorListWidth, layout.listHeight, layout.controlId);
        setupResourceMonitorList(g_resourceMonitorLists[slot]);
    }

    g_resourceMonitorStatus = createResourceMonitorChild(L"STATIC", L"",
        0, SS_ENDELLIPSIS | SS_NOPREFIX, kResourceMonitorMargin, kResourceMonitorStatusY,
        kResourceMonitorListWidth, kResourceMonitorBottomTextHeight, kResourceMonitorIdStatus);

    refreshResourceMonitorWindow(true);
}

static LRESULT CALLBACK resourceMonitorWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        switch (id)
        {
        case kResourceMonitorIdExport:
            exportSelectedResourceMonitorRow();
            return 0;
        case kResourceMonitorIdRefresh:
            refreshResourceMonitorWindow(true);
            return 0;
        case kResourceMonitorIdClose:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_TIMER:
        if (wParam == kResourceMonitorTimerId)
        {
            refreshResourceMonitorWindow(false);
            return 0;
        }
        break;
    case WM_NOTIFY:
    {
        NMHDR* header = (NMHDR*)lParam;
        ResourceMonitorListSlot slot = RESOURCE_MONITOR_LIST_SLOT_LOADED;
        if (header &&
            resourceMonitorListSlotForControlId((int)header->idFrom, &slot))
        {
            if (header->code == NM_CLICK || header->code == NM_SETFOCUS)
            {
                resourceMonitorRememberExportSlot(slot);
            }
            else if (header->code == LVN_ITEMCHANGED)
            {
                NMLISTVIEW* changed = (NMLISTVIEW*)lParam;
                if (changed->uNewState & LVIS_SELECTED)
                {
                    resourceMonitorRememberExportSlot(slot);
                }
            }
            if (header->code != NM_CUSTOMDRAW)
            {
                break;
            }

            NMLVCUSTOMDRAW* draw = (NMLVCUSTOMDRAW*)lParam;
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
            {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
            {
                const std::vector<ResourceMonitorDisplayRow>& rows = g_resourceMonitorRows[slot];
                size_t row = (size_t)draw->nmcd.dwItemSpec;
                if (row < rows.size() &&
                    rows[row].highlightKind != RESOURCE_MONITOR_HIGHLIGHT_NONE)
                {
                    if (rows[row].highlightKind == RESOURCE_MONITOR_HIGHLIGHT_NEW)
                    {
                        draw->clrTextBk = RGB(218, 245, 226);
                    }
                    else if (rows[row].highlightKind == RESOURCE_MONITOR_HIGHLIGHT_UPDATE)
                    {
                        draw->clrTextBk = RGB(255, 248, 196);
                    }
                    else if (rows[row].highlightKind == RESOURCE_MONITOR_HIGHLIGHT_CLOSED)
                    {
                        draw->clrTextBk = RGB(255, 226, 226);
                    }
                }
                return CDRF_DODEFAULT;
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)(g_resourceMonitorBackgroundBrush ?
            g_resourceMonitorBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_resourceMonitorWindow == hwnd)
        {
            KillTimer(hwnd, kResourceMonitorTimerId);
            g_resourceMonitorWindow = NULL;
            for (int slot = 0; slot < RESOURCE_MONITOR_LIST_SLOT_COUNT; ++slot)
            {
                g_resourceMonitorLists[slot] = NULL;
                g_resourceMonitorRows[slot].clear();
            }
            g_resourceMonitorStatus = NULL;
            resetResourceMonitorDisplayTracking();
            runtimeResourceMonitorSetActive(false);
        }
        if (g_resourceMonitorFont && g_resourceMonitorOwnFont)
        {
            DeleteObject(g_resourceMonitorFont);
        }
        g_resourceMonitorFont = NULL;
        g_resourceMonitorOwnFont = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensureResourceMonitorWindowClass(void)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = resourceMonitorWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DingooPieResourceMonitorWindow";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = g_resourceMonitorBackgroundBrush;
    RegisterClassW(&wc);
    registered = true;
}

void resourceMonitorOpenWindow(HWND owner, UiLanguage language)
{
    (void)owner;
    g_resourceMonitorLanguage = language;
    if (g_resourceMonitorWindow)
    {
        resetResourceMonitorDisplayTracking();
        emulatorRuntimeEnableResourceMonitor();
        ShowWindow(g_resourceMonitorWindow, SW_SHOWNOACTIVATE);
        refreshResourceMonitorWindow(true);
        return;
    }

    INITCOMMONCONTROLSEX controls;
    memset(&controls, 0, sizeof(controls));
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    g_resourceMonitorBackgroundBrush = GetSysColorBrush(COLOR_BTNFACE);
    ensureResourceMonitorWindowClass();

    g_resourceMonitorWindow = CreateWindowExW(WS_EX_CONTROLPARENT,
        L"DingooPieResourceMonitorWindow",
        resourceMonitorTitle(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, kResourceMonitorWindowWidth, kResourceMonitorWindowHeight,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_resourceMonitorWindow)
    {
        return;
    }

    resetResourceMonitorDisplayTracking();
    emulatorRuntimeEnableResourceMonitor();
    applyResourceMonitorIcon(g_resourceMonitorWindow);
    createResourceMonitorContents();
    SetTimer(g_resourceMonitorWindow, kResourceMonitorTimerId, kResourceMonitorTimerMs, NULL);

    ShowWindow(g_resourceMonitorWindow, SW_SHOWNOACTIVATE);
    UpdateWindow(g_resourceMonitorWindow);
}

#endif

#include "memory_searcher_ui.h"

#ifdef _WIN32

#include "emulator_core.h"
#include "resource_ids.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <wchar.h>
#include <windows.h>
#include <uxtheme.h>

#include "platform_win32.h"

typedef EmulatorRuntimeMemorySearchCandidate MemorySearcherCandidate;

struct MemorySearcherSavedAddress
{
    MemorySearcherCandidate candidate;
    int width;
};

struct MemorySearcherButtonSpec
{
    const wchar_t* text;
    int id;
};

enum MemorySearcherFilter
{
    MEMORY_SEARCHER_FILTER_EQUAL,
    MEMORY_SEARCHER_FILTER_INCREASED,
    MEMORY_SEARCHER_FILTER_DECREASED,
    MEMORY_SEARCHER_FILTER_UNCHANGED
};

enum MemorySearcherSelectionSource
{
    MEMORY_SEARCHER_SELECTION_NONE,
    MEMORY_SEARCHER_SELECTION_CANDIDATE,
    MEMORY_SEARCHER_SELECTION_SAVED
};

static HWND g_memorySearcherWindow = NULL;
static HWND g_memorySearcherList = NULL;
static HWND g_memorySearcherSavedList = NULL;
static HWND g_memorySearcherAppInfoList = NULL;
static HBRUSH g_memorySearcherBackgroundBrush = NULL;
static HFONT g_memorySearcherFont = NULL;
static bool g_memorySearcherOwnFont = false;
static UiLanguage g_memorySearcherLanguage = UI_LANGUAGE_ENGLISH;
static std::vector<MemorySearcherCandidate> g_memorySearcherCandidates;
static std::vector<MemorySearcherSavedAddress> g_memorySearcherSavedAddresses;
static bool g_memorySearcherHasScan = false;
static bool g_memorySearcherCapped = false;
static int g_memorySearcherScanWidth = 1;
static MemorySearcherSelectionSource g_memorySearcherSelectionSource = MEMORY_SEARCHER_SELECTION_NONE;

static const size_t kMemorySearcherMaxCandidates = 250000;
static const size_t kMemorySearcherDisplayLimit = 3000;
static const uint32_t kMemorySearcherScanBegin = 0x80000000u;
static const uint32_t kMemorySearcherScanEnd = 0xb0000000u;

static const int kMemorySearcherWindowWidth = 980;
static const int kMemorySearcherWindowHeight = 760;
static const int kMemorySearcherMarginX = 18;
static const int kMemorySearcherRightX = 944;
static const int kMemorySearcherListY = 264;
static const int kMemorySearcherListWidth = kMemorySearcherRightX - kMemorySearcherMarginX;
static const int kMemorySearcherListHeight = 280;
static const int kMemorySearcherSavedListY = 560;
static const int kMemorySearcherSavedListHeight = 118;
static const int kMemorySearcherStatusY = 690;
static const int kMemorySearcherResultBaselineDividerX = 614;
static const int kMemorySearcherAppInfoX = kMemorySearcherResultBaselineDividerX;
static const int kMemorySearcherAppInfoLabelY = 42;
static const int kMemorySearcherAppInfoY = 66;
static const int kMemorySearcherAppInfoWidth = kMemorySearcherRightX - kMemorySearcherAppInfoX;
static const int kMemorySearcherAppInfoHeight = 174;
static const int kMemorySearcherSectionX = 18;
static const int kMemorySearcherControlX = 146;
static const int kMemorySearcherTypeLabelWidth = 48;
static const int kMemorySearcherWidthComboX = kMemorySearcherControlX + kMemorySearcherTypeLabelWidth;
static const int kMemorySearcherWidthComboWidth = 86;
static const int kMemorySearcherButtonGap = 10;
static const int kMemorySearcherFilterButtonWidth = 92;
static const int kMemorySearcherActionButtonWidth = 126;
static const int kMemorySearcherCloseButtonWidth = 74;
static const int kMemorySearcherTopTextWidth =
    kMemorySearcherAppInfoX - kMemorySearcherMarginX - kMemorySearcherButtonGap;
static const int kMemorySearcherButtonRowRight =
    kMemorySearcherControlX + (kMemorySearcherActionButtonWidth * 3) + (kMemorySearcherButtonGap * 2);
static const int kMemorySearcherSearchButtonWidth = kMemorySearcherFilterButtonWidth;
static const int kMemorySearcherSearchButtonX = kMemorySearcherButtonRowRight - kMemorySearcherSearchButtonWidth;
static const int kMemorySearcherValueLabelX = 298;
static const int kMemorySearcherValueLabelWidth = 46;
static const int kMemorySearcherValueEditX = 344;
static const int kMemorySearcherValueEditWidth =
    kMemorySearcherSearchButtonX - kMemorySearcherButtonGap - kMemorySearcherValueEditX;

static const int kMemorySearcherIdWidth = 43001;
static const int kMemorySearcherIdValue = 43002;
static const int kMemorySearcherIdNewScan = 43003;
static const int kMemorySearcherIdEqual = 43004;
static const int kMemorySearcherIdIncreased = 43005;
static const int kMemorySearcherIdDecreased = 43006;
static const int kMemorySearcherIdUnchanged = 43007;
static const int kMemorySearcherIdReset = 43008;
static const int kMemorySearcherIdRefresh = 43009;
static const int kMemorySearcherIdClose = 43010;
static const int kMemorySearcherIdStatus = 43011;
static const int kMemorySearcherIdList = 43012;
static const int kMemorySearcherIdPoke = 43013;
static const int kMemorySearcherIdCopyCheat = 43014;
static const int kMemorySearcherIdSaveAddress = 43015;
static const int kMemorySearcherIdRefreshSaved = 43016;
static const int kMemorySearcherIdClearSaved = 43017;
static const int kMemorySearcherIdSavedList = 43018;

static bool memorySearcherChinese(void)
{
    return g_memorySearcherLanguage == UI_LANGUAGE_CHINESE;
}

static const wchar_t* memorySearcherTitle(void)
{
    return memorySearcherChinese() ? L"\u5185\u5b58\u641c\u7d22\u5668" : L"Memory Searcher";
}

static const wchar_t* memorySearcherReadFailedText(void)
{
    return memorySearcherChinese() ? L"\u8bfb\u53d6\u5931\u8d25" : L"Read failed";
}

static HICON loadMemorySearcherIcon(int size)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED);
}

static void applyMemorySearcherIcon(HWND window)
{
    if (!window)
    {
        return;
    }

    HICON largeIcon = loadMemorySearcherIcon(32);
    HICON smallIcon = loadMemorySearcherIcon(16);
    if (largeIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    }
    if (smallIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    }
}

static HFONT memorySearcherFont(void)
{
    if (g_memorySearcherFont)
    {
        return g_memorySearcherFont;
    }

    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0))
    {
        g_memorySearcherFont = CreateFontIndirectW(&metrics.lfMessageFont);
        g_memorySearcherOwnFont = g_memorySearcherFont != NULL;
    }
    if (!g_memorySearcherFont)
    {
        g_memorySearcherFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_memorySearcherOwnFont = false;
    }
    return g_memorySearcherFont;
}

static HWND memorySearcherItem(int id)
{
    return g_memorySearcherWindow ? GetDlgItem(g_memorySearcherWindow, id) : NULL;
}

static HWND createMemorySearcherChild(HWND parent, const wchar_t* className, const wchar_t* text,
    DWORD exStyle, DWORD style, int x, int y, int w, int h, int id)
{
    DWORD childStyle = (DWORD)platformWin32NormalizeChildStyle(className, style);
    HWND child = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | childStyle,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (child)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)memorySearcherFont(), TRUE);
        SetWindowTheme(child, L"Explorer", NULL);
    }
    return child;
}

static HWND createMemorySearcherLabel(const wchar_t* text, int x, int y, int w, int h)
{
    return createMemorySearcherChild(g_memorySearcherWindow, L"STATIC", text,
        0, 0, x, y, w, h, -1);
}

static HWND createMemorySearcherButton(const wchar_t* text, int x, int y, int w, int h, int id)
{
    return createMemorySearcherChild(g_memorySearcherWindow, L"BUTTON", text,
        0, 0, x, y, w, h, id);
}

static void createMemorySearcherButtonRow(const MemorySearcherButtonSpec* buttons, int count,
    int x, int y, int width, int height)
{
    if (!buttons || count <= 0)
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        createMemorySearcherButton(buttons[i].text, x, y, width, height, buttons[i].id);
        x += width + kMemorySearcherButtonGap;
    }
}

static void setWindowTextAndRedraw(HWND control, const wchar_t* text)
{
    if (!control)
    {
        return;
    }

    SendMessageW(control, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(control, L"");
    SetWindowTextW(control, text ? text : L"");
    SendMessageW(control, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(control, NULL, TRUE);
    UpdateWindow(control);
}

static void setMemorySearcherStatus(const wchar_t* text)
{
    setWindowTextAndRedraw(memorySearcherItem(kMemorySearcherIdStatus), text);
}

static int selectedMemorySearcherWidth(void)
{
    HWND combo = memorySearcherItem(kMemorySearcherIdWidth);
    int index = combo ? (int)SendMessageW(combo, CB_GETCURSEL, 0, 0) : 0;
    if (index == 1)
    {
        return 2;
    }
    if (index == 2)
    {
        return 4;
    }
    return 1;
}

static int activeMemorySearcherWidth(void)
{
    return g_memorySearcherHasScan ? g_memorySearcherScanWidth : selectedMemorySearcherWidth();
}

static bool parseMemorySearcherValue(uint32_t* out)
{
    if (!out)
    {
        return false;
    }

    wchar_t text[64] = {};
    GetWindowTextW(memorySearcherItem(kMemorySearcherIdValue), text, (int)(sizeof(text) / sizeof(text[0])));
    wchar_t* p = text;
    while (*p == L' ' || *p == L'\t')
    {
        p++;
    }
    if (!*p)
    {
        return false;
    }

    wchar_t* end = NULL;
    unsigned long parsed = wcstoul(p, &end, 0);
    while (end && (*end == L' ' || *end == L'\t'))
    {
        end++;
    }
    if (!end || *end)
    {
        return false;
    }

    *out = (uint32_t)parsed;
    return true;
}

static uint32_t maskForWidth(int width)
{
    if (width == 1)
    {
        return 0xffu;
    }
    if (width == 2)
    {
        return 0xffffu;
    }
    return 0xffffffffu;
}

static uint32_t readLeValue(const uint8_t* bytes, int width)
{
    uint32_t value = bytes[0];
    if (width >= 2)
    {
        value |= ((uint32_t)bytes[1]) << 8;
    }
    if (width >= 4)
    {
        value |= ((uint32_t)bytes[2]) << 16;
        value |= ((uint32_t)bytes[3]) << 24;
    }
    return value;
}

static bool readRuntimeValue(uint32_t address, int width, uint32_t* out)
{
    uint8_t bytes[4] = {};
    if (!out || !emulatorRuntimeReadMemory(address, bytes, (size_t)width))
    {
        return false;
    }
    *out = readLeValue(bytes, width);
    return true;
}

static void formatValue(uint32_t value, int width, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }
    if (width == 1)
    {
        swprintf(out, outCount, L"%u / 0x%02X", value & 0xffu, value & 0xffu);
    }
    else if (width == 2)
    {
        swprintf(out, outCount, L"%u / 0x%04X", value & 0xffffu, value & 0xffffu);
    }
    else
    {
        swprintf(out, outCount, L"%u / 0x%08X", value, value);
    }
}

static std::wstring compactAppInfoText(const std::string& text, size_t maxChars)
{
    std::wstring wide = platformUtf8ToWide(text);
    if (wide.size() <= maxChars || maxChars < 8)
    {
        return wide;
    }

    size_t head = (maxChars - 3) / 2;
    size_t tail = maxChars - 3 - head;
    return wide.substr(0, head) + L"..." + wide.substr(wide.size() - tail);
}

static std::wstring shortSha256Text(const std::string& sha256)
{
    if (sha256.size() <= 12)
    {
        return platformUtf8ToWide(sha256);
    }
    return platformUtf8ToWide(sha256.substr(0, 12) + "...");
}

static void formatHex32(uint32_t value, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }
    swprintf(out, outCount, L"0x%08X", value);
}

static void formatCapacity(uint32_t value, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }

    const wchar_t* unit = L"B";
    double amount = (double)value;
    if (amount >= 1024.0)
    {
        amount /= 1024.0;
        unit = L"KB";
    }
    if (amount >= 1024.0)
    {
        amount /= 1024.0;
        unit = L"MB";
    }
    if (amount >= 1024.0)
    {
        amount /= 1024.0;
        unit = L"GB";
    }

    if (unit[0] == L'B')
    {
        swprintf(out, outCount, L"%u B", value);
    }
    else
    {
        swprintf(out, outCount, L"%.2f %ls", amount, unit);
    }
}

static const wchar_t* memorySearcherWidthName(int width);
static void refreshMemorySearcherAppInfoList(void);

static void setListViewText(HWND list, int row, int column, const wchar_t* text)
{
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = column;
    item.pszText = (LPWSTR)text;
    SendMessageW(list, LVM_SETITEMW, 0, (LPARAM)&item);
}

static void insertListViewRow(HWND list, int row, const wchar_t* text)
{
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = 0;
    item.pszText = (LPWSTR)text;
    SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
}

static int memorySearcherListAvailableWidth(HWND list)
{
    RECT rect;
    memset(&rect, 0, sizeof(rect));
    if (!list || !GetClientRect(list, &rect))
    {
        return 0;
    }

    int width = rect.right - rect.left -
        GetSystemMetrics(SM_CXVSCROLL) - 4;
    return width > 0 ? width : 0;
}

static void addMemorySearcherListColumn(HWND list, int index, const wchar_t* text, int width)
{
    LVCOLUMNW column;
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = (LPWSTR)text;
    column.cx = width;
    column.iSubItem = index;
    SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&column);
}

static void setupFittedMemorySearcherColumns(HWND list,
    const wchar_t** headers, const int* weights, int count)
{
    if (!list || !headers || !weights || count <= 0)
    {
        return;
    }

    int totalWeight = 0;
    for (int i = 0; i < count; ++i)
    {
        totalWeight += weights[i] > 0 ? weights[i] : 1;
    }

    int availableWidth = memorySearcherListAvailableWidth(list);
    if (availableWidth <= 0)
    {
        availableWidth = totalWeight;
    }

    int usedWidth = 0;
    for (int i = 0; i < count; ++i)
    {
        int width = 0;
        if (i + 1 == count)
        {
            width = availableWidth - usedWidth;
        }
        else
        {
            int weight = weights[i] > 0 ? weights[i] : 1;
            width = (availableWidth * weight) / totalWeight;
        }
        if (width < 1)
        {
            width = 1;
        }
        if (i + 1 != count)
        {
            usedWidth += width;
        }
        addMemorySearcherListColumn(list, i, headers[i], width);
    }
}

static void setupMemorySearcherReportList(HWND list,
    const wchar_t** headers, const int* weights, int count)
{
    if (!list)
    {
        return;
    }

    DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
    ListView_SetExtendedListViewStyle(list, exStyle);
    setupFittedMemorySearcherColumns(list, headers, weights, count);
}

static void refreshMemorySearcherSavedList(void)
{
    if (!g_memorySearcherSavedList)
    {
        return;
    }

    int selectedRow = ListView_GetNextItem(g_memorySearcherSavedList, -1, LVNI_SELECTED);
    SendMessageW(g_memorySearcherSavedList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_memorySearcherSavedList);

    for (size_t i = 0; i < g_memorySearcherSavedAddresses.size(); ++i)
    {
        const MemorySearcherSavedAddress& saved = g_memorySearcherSavedAddresses[i];
        uint32_t current = 0;
        wchar_t addressText[32] = {};
        wchar_t valueText[64] = {};
        swprintf(addressText, sizeof(addressText) / sizeof(addressText[0]),
            L"0x%08X", saved.candidate.address);
        if (readRuntimeValue(saved.candidate.address, saved.width, &current))
        {
            formatValue(current, saved.width, valueText,
                sizeof(valueText) / sizeof(valueText[0]));
        }
        else
        {
            wcscpy(valueText, memorySearcherReadFailedText());
        }

        insertListViewRow(g_memorySearcherSavedList, (int)i, addressText);
        setListViewText(g_memorySearcherSavedList, (int)i, 1,
            memorySearcherWidthName(saved.width));
        setListViewText(g_memorySearcherSavedList, (int)i, 2, valueText);
    }

    if (selectedRow >= 0 && (size_t)selectedRow < g_memorySearcherSavedAddresses.size())
    {
        ListView_SetItemState(g_memorySearcherSavedList, selectedRow,
            LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        g_memorySearcherSelectionSource = MEMORY_SEARCHER_SELECTION_SAVED;
    }
    SendMessageW(g_memorySearcherSavedList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_memorySearcherSavedList, NULL, TRUE);
}

static void refreshMemorySearcherResults(void)
{
    if (!g_memorySearcherList)
    {
        return;
    }

    SendMessageW(g_memorySearcherList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_memorySearcherList);

    int width = activeMemorySearcherWidth();
    size_t shown = g_memorySearcherCandidates.size();
    if (shown > kMemorySearcherDisplayLimit)
    {
        shown = kMemorySearcherDisplayLimit;
    }

    for (size_t i = 0; i < shown; ++i)
    {
        const MemorySearcherCandidate& candidate = g_memorySearcherCandidates[i];
        wchar_t addressText[32] = {};
        wchar_t valueText[64] = {};
        wchar_t previousText[64] = {};
        uint32_t current = 0;
        swprintf(addressText, sizeof(addressText) / sizeof(addressText[0]), L"0x%08X", candidate.address);
        if (readRuntimeValue(candidate.address, width, &current))
        {
            formatValue(current, width, valueText, sizeof(valueText) / sizeof(valueText[0]));
        }
        else
        {
            wcscpy(valueText, memorySearcherReadFailedText());
        }
        formatValue(candidate.previous, width, previousText, sizeof(previousText) / sizeof(previousText[0]));

        insertListViewRow(g_memorySearcherList, (int)i, addressText);
        setListViewText(g_memorySearcherList, (int)i, 1, memorySearcherWidthName(width));
        setListViewText(g_memorySearcherList, (int)i, 2, valueText);
        setListViewText(g_memorySearcherList, (int)i, 3, previousText);
    }

    SendMessageW(g_memorySearcherList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_memorySearcherList, NULL, TRUE);

    wchar_t status[256] = {};
    if (!g_memorySearcherHasScan)
    {
        swprintf(status, sizeof(status) / sizeof(status[0]), memorySearcherChinese() ?
            L"\u5148\u9009 u8/u16/u32\uff0c\u8f93\u5165\u5f53\u524d\u6570\u503c\uff0c\u70b9\u51fb\u300c\u9996\u6b21\u641c\u7d22\u300d\u3002" :
            L"Choose u8/u16/u32, enter the current value, then click New Scan.");
    }
    else if (g_memorySearcherCapped)
    {
        swprintf(status, sizeof(status) / sizeof(status[0]), memorySearcherChinese() ?
            L"\u5019\u9009\u8fc7\u591a\uff0c\u5df2\u9650\u5236\u4e3a %u \u6761\uff1b\u663e\u793a\u524d %u \u6761\uff1b\u57fa\u51c6\u503c\u4f9b\u4e0b\u6b21\u7f29\u5c0f\u3002" :
            L"Capped at %u candidates; showing first %u. Baseline is for the next narrow.",
            (unsigned)kMemorySearcherMaxCandidates, (unsigned)shown);
    }
    else
    {
        swprintf(status, sizeof(status) / sizeof(status[0]), memorySearcherChinese() ?
            L"\u5019\u9009\uff1a%u \u6761\uff1b\u663e\u793a\uff1a%u \u6761\uff1b\u57fa\u51c6\u503c\u7528\u4e8e\u4e0b\u6b21\u7b5b\u9009\u3002" :
            L"Candidates: %u; showing: %u. Baseline is used by the next filter.",
            (unsigned)g_memorySearcherCandidates.size(), (unsigned)shown);
    }
    setMemorySearcherStatus(status);
    refreshMemorySearcherSavedList();
    refreshMemorySearcherAppInfoList();
}

static void memorySearcherNewScan(void)
{
    uint32_t target = 0;
    int width = selectedMemorySearcherWidth();
    if (!parseMemorySearcherValue(&target))
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u8bf7\u8f93\u5165\u5341\u8fdb\u5236\u6216 0x \u5341\u516d\u8fdb\u5236\u6570\u503c\u3002" :
            L"Enter a decimal value or 0x hexadecimal value.");
        return;
    }
    target &= maskForWidth(width);

    g_memorySearcherCandidates.clear();
    g_memorySearcherCapped = false;
    g_memorySearcherScanWidth = width;

    bool ok = emulatorRuntimeSearchMemoryValue(
        kMemorySearcherScanBegin,
        kMemorySearcherScanEnd,
        width,
        target,
        kMemorySearcherMaxCandidates,
        &g_memorySearcherCandidates,
        &g_memorySearcherCapped);
    g_memorySearcherHasScan = ok;
    if (!ok)
    {
        refreshMemorySearcherResults();
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u672a\u8fd0\u884c\u6e38\u620f\uff0c\u65e0\u6cd5\u641c\u7d22\u5185\u5b58\u3002" :
            L"No game is running; cannot scan memory.");
        return;
    }

    refreshMemorySearcherResults();
}

static EmulatorRuntimeMemorySearchFilter runtimeFilterForMemorySearcher(MemorySearcherFilter filter)
{
    switch (filter)
    {
    case MEMORY_SEARCHER_FILTER_EQUAL:
        return EMULATOR_RUNTIME_MEMORY_SEARCH_EQUAL;
    case MEMORY_SEARCHER_FILTER_INCREASED:
        return EMULATOR_RUNTIME_MEMORY_SEARCH_INCREASED;
    case MEMORY_SEARCHER_FILTER_DECREASED:
        return EMULATOR_RUNTIME_MEMORY_SEARCH_DECREASED;
    case MEMORY_SEARCHER_FILTER_UNCHANGED:
        return EMULATOR_RUNTIME_MEMORY_SEARCH_UNCHANGED;
    default:
        return EMULATOR_RUNTIME_MEMORY_SEARCH_EQUAL;
    }
}

static void memorySearcherFilter(MemorySearcherFilter filter)
{
    if (!g_memorySearcherHasScan)
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u8bf7\u5148\u8fdb\u884c\u9996\u6b21\u641c\u7d22\u3002" :
            L"Run a new scan first.");
        return;
    }

    int width = activeMemorySearcherWidth();
    uint32_t target = 0;
    if (filter == MEMORY_SEARCHER_FILTER_EQUAL)
    {
        if (!parseMemorySearcherValue(&target))
        {
            setMemorySearcherStatus(memorySearcherChinese() ?
                L"\u7b5b\u9009\u7b49\u4e8e\u65f6\u9700\u8981\u8f93\u5165\u6570\u503c\u3002" :
                L"Equal filtering needs a value.");
            return;
        }
        target &= maskForWidth(width);
    }

    size_t beforeCount = g_memorySearcherCandidates.size();
    if (!emulatorRuntimeFilterMemorySearchCandidates(
        width,
        target,
        runtimeFilterForMemorySearcher(filter),
        &g_memorySearcherCandidates))
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u6e38\u620f\u672a\u8fd0\u884c\uff0c\u65e0\u6cd5\u7f29\u5c0f\u7ed3\u679c\u3002" :
            L"No game is running; cannot narrow results.");
        return;
    }
    g_memorySearcherCapped = false;
    refreshMemorySearcherResults();

    wchar_t status[256] = {};
    size_t shown = g_memorySearcherCandidates.size();
    if (shown > kMemorySearcherDisplayLimit)
    {
        shown = kMemorySearcherDisplayLimit;
    }
    swprintf(status, sizeof(status) / sizeof(status[0]), memorySearcherChinese() ?
        L"\u7b5b\u9009\uff1a%u -> %u \u6761\uff1b\u663e\u793a\uff1a%u \u6761\uff1b\u57fa\u51c6\u503c\u5df2\u66f4\u65b0\u3002" :
        L"Filtered: %u -> %u candidates; showing: %u. Baseline updated.",
        (unsigned)beforeCount, (unsigned)g_memorySearcherCandidates.size(), (unsigned)shown);
    setMemorySearcherStatus(status);
}

static void memorySearcherReset(void)
{
    g_memorySearcherCandidates.clear();
    g_memorySearcherSavedAddresses.clear();
    g_memorySearcherHasScan = false;
    g_memorySearcherCapped = false;
    g_memorySearcherScanWidth = selectedMemorySearcherWidth();
    g_memorySearcherSelectionSource = MEMORY_SEARCHER_SELECTION_NONE;
    refreshMemorySearcherResults();
}

static void memorySearcherRefresh(void)
{
    refreshMemorySearcherResults();
    setMemorySearcherStatus(memorySearcherChinese() ?
        L"\u5df2\u5237\u65b0\u5f53\u524d\u7ed3\u679c\u3001\u4fdd\u5b58\u5217\u8868\u548c App \u4fe1\u606f\u3002" :
        L"Refreshed current results, saved list, and app info.");
}

static bool selectedMemorySearcherCandidate(MemorySearcherCandidate* out)
{
    if (!out || !g_memorySearcherList)
    {
        return false;
    }
    int row = ListView_GetNextItem(g_memorySearcherList, -1, LVNI_SELECTED);
    if (row < 0 || (size_t)row >= g_memorySearcherCandidates.size())
    {
        return false;
    }
    *out = g_memorySearcherCandidates[(size_t)row];
    return true;
}

static bool selectedMemorySearcherSavedCandidate(MemorySearcherCandidate* out, int* width, int* rowOut)
{
    if (!out || !width || !g_memorySearcherSavedList)
    {
        return false;
    }
    int row = ListView_GetNextItem(g_memorySearcherSavedList, -1, LVNI_SELECTED);
    if (row < 0 || (size_t)row >= g_memorySearcherSavedAddresses.size())
    {
        return false;
    }

    const MemorySearcherSavedAddress& saved = g_memorySearcherSavedAddresses[(size_t)row];
    *out = saved.candidate;
    *width = saved.width;
    if (rowOut)
    {
        *rowOut = row;
    }
    return true;
}

static bool activeMemorySearcherCandidate(MemorySearcherCandidate* out, int* width)
{
    if (!out || !width)
    {
        return false;
    }
    if (g_memorySearcherSelectionSource == MEMORY_SEARCHER_SELECTION_SAVED &&
        selectedMemorySearcherSavedCandidate(out, width, NULL))
    {
        return true;
    }
    if (g_memorySearcherSelectionSource == MEMORY_SEARCHER_SELECTION_CANDIDATE &&
        selectedMemorySearcherCandidate(out))
    {
        *width = activeMemorySearcherWidth();
        return true;
    }
    if (selectedMemorySearcherSavedCandidate(out, width, NULL))
    {
        return true;
    }
    if (selectedMemorySearcherCandidate(out))
    {
        *width = activeMemorySearcherWidth();
        return true;
    }
    return false;
}

static void writeLeValue(uint8_t* bytes, int width, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffu);
    if (width >= 2)
    {
        bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    }
    if (width >= 4)
    {
        bytes[2] = (uint8_t)((value >> 16) & 0xffu);
        bytes[3] = (uint8_t)((value >> 24) & 0xffu);
    }
}

static void updateMemorySearcherBaseline(uint32_t address, int width, uint32_t value)
{
    if (width == activeMemorySearcherWidth())
    {
        for (size_t i = 0; i < g_memorySearcherCandidates.size(); ++i)
        {
            if (g_memorySearcherCandidates[i].address == address)
            {
                g_memorySearcherCandidates[i].previous = value;
            }
        }
    }

    for (size_t i = 0; i < g_memorySearcherSavedAddresses.size(); ++i)
    {
        MemorySearcherSavedAddress& saved = g_memorySearcherSavedAddresses[i];
        if (saved.candidate.address == address && saved.width == width)
        {
            saved.candidate.previous = value;
        }
    }
}

static const wchar_t* memorySearcherWidthName(int width)
{
    if (width == 1)
    {
        return L"u8";
    }
    if (width == 2)
    {
        return L"u16";
    }
    return L"u32";
}

static void memorySearcherPokeSelected(void)
{
    MemorySearcherCandidate candidate;
    uint32_t value = 0;
    int width = 1;
    if (!activeMemorySearcherCandidate(&candidate, &width))
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u8bf7\u5148\u9009\u4e2d\u6216\u4fdd\u5b58\u4e00\u4e2a\u5730\u5740\u3002" :
            L"Select or save one address first.");
        return;
    }
    if (!parseMemorySearcherValue(&value))
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u8bf7\u8f93\u5165\u8981\u5199\u5165\u7684\u6570\u503c\u3002" :
            L"Enter the value to write.");
        return;
    }
    value &= maskForWidth(width);

    uint8_t bytes[4] = {};
    writeLeValue(bytes, width, value);
    if (!emulatorRuntimeWriteMemory(candidate.address, bytes, (size_t)width))
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u5199\u5165\u5931\u8d25\uff0c\u6e38\u620f\u53ef\u80fd\u5df2\u505c\u6b62\u6216\u5730\u5740\u5931\u6548\u3002" :
            L"Write failed; the game may have stopped or the address changed.");
        return;
    }

    updateMemorySearcherBaseline(candidate.address, width, value);
    refreshMemorySearcherResults();
    setMemorySearcherStatus(memorySearcherChinese() ?
        L"\u5df2\u5199\u5165\u5730\u5740\u3002" :
        L"Wrote the address.");
}

static bool setClipboardText(HWND owner, const wchar_t* text)
{
    if (!text || !OpenClipboard(owner))
    {
        return false;
    }

    EmptyClipboard();
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
    {
        CloseClipboard();
        return false;
    }
    void* locked = GlobalLock(memory);
    if (!locked)
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    memcpy(locked, text, bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory))
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

static void memorySearcherCopyCheatLine(void)
{
    MemorySearcherCandidate candidate;
    uint32_t value = 0;
    int width = 1;
    if (!activeMemorySearcherCandidate(&candidate, &width))
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u8bf7\u5148\u9009\u4e2d\u6216\u4fdd\u5b58\u4e00\u4e2a\u5730\u5740\u3002" :
            L"Select or save one address first.");
        return;
    }
    if (!parseMemorySearcherValue(&value))
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u8bf7\u8f93\u5165\u8981\u5199\u5165 .cht \u884c\u7684\u6570\u503c\u3002" :
            L"Enter the value to write into the .cht line.");
        return;
    }
    value &= maskForWidth(width);

    wchar_t line[256] = {};
    swprintf(line, sizeof(line) / sizeof(line[0]),
        L"on|\u65b0\u91d1\u624b\u6307/New Cheat|%ls|0x%08X|0x%X",
        memorySearcherWidthName(width),
        candidate.address,
        value);
    if (!setClipboardText(g_memorySearcherWindow, line))
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u590d\u5236\u5230\u526a\u8d34\u677f\u5931\u8d25\u3002" :
            L"Failed to copy to the clipboard.");
        return;
    }
    setMemorySearcherStatus(memorySearcherChinese() ?
        L"\u5df2\u590d\u5236 .cht \u884c\uff0c\u53ef\u7c98\u8d34\u5230\u5bf9\u5e94\u91d1\u624b\u6307\u6587\u4ef6\u3002" :
        L"Copied a .cht line; paste it into the matching cheat file.");
}

static void memorySearcherSaveSelected(void)
{
    MemorySearcherCandidate candidate;
    if (!selectedMemorySearcherCandidate(&candidate))
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u8bf7\u5148\u5728\u5217\u8868\u4e2d\u9009\u4e2d\u8981\u4fdd\u5b58\u7684\u5730\u5740\u3002" :
            L"Select the address to save first.");
        return;
    }

    int width = activeMemorySearcherWidth();
    int selectedRow = -1;
    for (size_t i = 0; i < g_memorySearcherSavedAddresses.size(); ++i)
    {
        if (g_memorySearcherSavedAddresses[i].candidate.address == candidate.address &&
            g_memorySearcherSavedAddresses[i].width == width)
        {
            g_memorySearcherSavedAddresses[i].candidate = candidate;
            selectedRow = (int)i;
            break;
        }
    }
    if (selectedRow < 0)
    {
        MemorySearcherSavedAddress saved;
        saved.candidate = candidate;
        saved.width = width;
        g_memorySearcherSavedAddresses.push_back(saved);
        selectedRow = (int)g_memorySearcherSavedAddresses.size() - 1;
    }

    refreshMemorySearcherSavedList();
    ListView_SetItemState(g_memorySearcherSavedList, selectedRow,
        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    g_memorySearcherSelectionSource = MEMORY_SEARCHER_SELECTION_SAVED;
    setMemorySearcherStatus(memorySearcherChinese() ?
        L"\u5df2\u5c06\u9009\u4e2d\u5730\u5740\u52a0\u5165\u4fdd\u5b58\u5217\u8868\u3002" :
        L"Added the selected address to the saved list.");
}

static void memorySearcherRefreshSaved(void)
{
    if (g_memorySearcherSavedAddresses.empty())
    {
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u4fdd\u5b58\u5217\u8868\u4e3a\u7a7a\u3002" :
            L"The saved list is empty.");
        return;
    }

    refreshMemorySearcherSavedList();
    setMemorySearcherStatus(memorySearcherChinese() ?
        L"\u5df2\u5237\u65b0\u4fdd\u5b58\u5217\u8868\u4e2d\u7684\u5f53\u524d\u503c\u3002" :
        L"Refreshed the saved list values.");
}

static void memorySearcherClearSaved(void)
{
    int row = g_memorySearcherSavedList ?
        ListView_GetNextItem(g_memorySearcherSavedList, -1, LVNI_SELECTED) : -1;
    if (row >= 0 && (size_t)row < g_memorySearcherSavedAddresses.size())
    {
        g_memorySearcherSavedAddresses.erase(g_memorySearcherSavedAddresses.begin() + row);
        if (g_memorySearcherSavedAddresses.empty())
        {
            g_memorySearcherSelectionSource = MEMORY_SEARCHER_SELECTION_NONE;
        }
        refreshMemorySearcherSavedList();
        setMemorySearcherStatus(memorySearcherChinese() ?
            L"\u5df2\u4ece\u4fdd\u5b58\u5217\u8868\u79fb\u9664\u9009\u4e2d\u5730\u5740\u3002" :
            L"Removed the selected saved address.");
        return;
    }

    g_memorySearcherSavedAddresses.clear();
    g_memorySearcherSelectionSource = MEMORY_SEARCHER_SELECTION_NONE;
    refreshMemorySearcherSavedList();
    setMemorySearcherStatus(memorySearcherChinese() ?
        L"\u5df2\u6e05\u7a7a\u4fdd\u5b58\u5217\u8868\u3002" :
        L"Cleared the saved list.");
}

static void setupMemorySearcherList(HWND list)
{
    const wchar_t* address = memorySearcherChinese() ? L"\u5730\u5740" : L"Address";
    const wchar_t* type = memorySearcherChinese() ? L"\u5bbd\u5ea6" : L"Width";
    const wchar_t* current = memorySearcherChinese() ? L"\u5f53\u524d\u503c" : L"Current Value";
    const wchar_t* previous = memorySearcherChinese() ? L"\u57fa\u51c6\u503c" : L"Baseline Value";
    const wchar_t* headers[] = { address, type, current, previous };
    int weights[] = { 170, 72, 260, 260 };
    setupMemorySearcherReportList(list, headers, weights, 4);
}

static void setupMemorySearcherSavedList(HWND list)
{
    const wchar_t* address = memorySearcherChinese() ? L"\u5730\u5740" : L"Address";
    const wchar_t* type = memorySearcherChinese() ? L"\u5bbd\u5ea6" : L"Width";
    const wchar_t* current = memorySearcherChinese() ? L"\u5f53\u524d\u503c" : L"Current Value";
    const wchar_t* headers[] = { address, type, current };
    int weights[] = { 170, 72, 520 };
    setupMemorySearcherReportList(list, headers, weights, 3);
}

static void setupMemorySearcherAppInfoList(HWND list)
{
    const wchar_t* field = memorySearcherChinese() ? L"\u9879" : L"Field";
    const wchar_t* value = memorySearcherChinese() ? L"\u503c" : L"Value";
    const wchar_t* headers[] = { field, value };
    int weights[] = { 92, 220 };
    setupMemorySearcherReportList(list, headers, weights, 2);
}

static void appendMemorySearcherAppInfoRow(int* row, const wchar_t* field, const std::wstring& value)
{
    if (!row || !g_memorySearcherAppInfoList)
    {
        return;
    }
    insertListViewRow(g_memorySearcherAppInfoList, *row, field);
    setListViewText(g_memorySearcherAppInfoList, *row, 1, value.c_str());
    (*row)++;
}

static void refreshMemorySearcherAppInfoList(void)
{
    if (!g_memorySearcherAppInfoList)
    {
        return;
    }

    SendMessageW(g_memorySearcherAppInfoList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_memorySearcherAppInfoList);

    int row = 0;
    EmulatorRuntimeAppInfo info;
    if (!emulatorRuntimeGetAppInfo(&info))
    {
        appendMemorySearcherAppInfoRow(&row,
            memorySearcherChinese() ? L"\u72b6\u6001" : L"Status",
            memorySearcherChinese() ? L"\u672a\u8fd0\u884c\u6e38\u620f" : L"No running game");
        SendMessageW(g_memorySearcherAppInfoList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_memorySearcherAppInfoList, NULL, TRUE);
        return;
    }

    wchar_t text[96] = {};
    appendMemorySearcherAppInfoRow(&row,
        memorySearcherChinese() ? L"\u6587\u4ef6" : L"File",
        compactAppInfoText(info.fileName, 34));
    appendMemorySearcherAppInfoRow(&row,
        memorySearcherChinese() ? L"\u8def\u5f84" : L"Path",
        compactAppInfoText(info.path, 38));
    appendMemorySearcherAppInfoRow(&row, L"SHA256", shortSha256Text(info.sha256));

    formatCapacity(info.fileSize, text, sizeof(text) / sizeof(text[0]));
    appendMemorySearcherAppInfoRow(&row,
        memorySearcherChinese() ? L"\u5927\u5c0f" : L"Size", text);

    formatHex32(info.origin, text, sizeof(text) / sizeof(text[0]));
    appendMemorySearcherAppInfoRow(&row,
        memorySearcherChinese() ? L"\u57fa\u5740" : L"Origin", text);

    formatHex32(info.bootEntry, text, sizeof(text) / sizeof(text[0]));
    appendMemorySearcherAppInfoRow(&row,
        memorySearcherChinese() ? L"\u5165\u53e3" : L"Entry", text);

    formatHex32(info.appMainEntry, text, sizeof(text) / sizeof(text[0]));
    appendMemorySearcherAppInfoRow(&row, L"AppMain", text);

    swprintf(text, sizeof(text) / sizeof(text[0]), L"I%u / E%u / R%u",
        info.importCount, info.exportCount, info.resourceCount);
    appendMemorySearcherAppInfoRow(&row,
        memorySearcherChinese() ? L"\u8868" : L"Tables", text);

    appendMemorySearcherAppInfoRow(&row,
        memorySearcherChinese() ? L"\u540e\u7aef" : L"Backend",
        platformUtf8ToWide(info.backend));
    appendMemorySearcherAppInfoRow(&row,
        memorySearcherChinese() ? L"\u517c\u5bb9" : L"Compat",
        compactAppInfoText(info.compatProfile, 32));

    SendMessageW(g_memorySearcherAppInfoList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_memorySearcherAppInfoList, NULL, TRUE);
}

static void createMemorySearcherIntro(bool zh)
{
    createMemorySearcherButton(zh ? L"\u5237\u65b0" : L"Refresh",
        kMemorySearcherRightX - (kMemorySearcherCloseButtonWidth * 2) - kMemorySearcherButtonGap,
        10, kMemorySearcherCloseButtonWidth, 28, kMemorySearcherIdRefresh);
    createMemorySearcherButton(zh ? L"\u5173\u95ed" : L"Close",
        kMemorySearcherRightX - kMemorySearcherCloseButtonWidth, 10,
        kMemorySearcherCloseButtonWidth, 28, kMemorySearcherIdClose);

    createMemorySearcherLabel(zh ?
        L"\u6b65\u9aa4\uff1a\u9996\u6b21\u641c\u7d22 -> \u6e38\u620f\u4e2d\u6539\u53d8\u6570\u503c -> \u7528\u53d8\u5927/\u53d8\u5c0f/\u4e0d\u53d8\u7f29\u5c0f\u7ed3\u679c" :
        L"Steps: New Scan -> change the in-game value -> narrow with Increased/Decreased/Unchanged",
        kMemorySearcherMarginX, 14, kMemorySearcherTopTextWidth, 22);
    createMemorySearcherLabel(zh ?
        L"\u627e\u5230\u53ef\u80fd\u5730\u5740\u540e\uff1a\u4fdd\u5b58\u5230\u4e0b\u65b9\u5217\u8868\uff0c\u518d\u5237\u65b0\u5f53\u524d\u503c\u6216\u590d\u5236 .cht \u884c\u3002" :
        L"Save likely rows below, refresh their values, or copy a .cht line.",
        kMemorySearcherMarginX, 38, kMemorySearcherTopTextWidth, 22);
    createMemorySearcherLabel(zh ?
        L"\u5bbd\u5ea6\u8303\u56f4\uff1au8=0~255\uff0cu16=0~65535\uff0cu32=0~4294967295\u3002\u4e0d\u786e\u5b9a\u65f6\u5148\u8bd5 u8\u3002" :
        L"Width ranges: u8=0-255, u16=0-65535, u32=0-4294967295. If unsure, try u8 first.",
        kMemorySearcherMarginX, 62, kMemorySearcherTopTextWidth, 22);
}

static void createMemorySearcherAppInfoControls(bool zh)
{
    createMemorySearcherLabel(zh ? L"\u5f53\u524d App" : L"Current App",
        kMemorySearcherAppInfoX, kMemorySearcherAppInfoLabelY,
        kMemorySearcherAppInfoWidth, 22);

    g_memorySearcherAppInfoList = createMemorySearcherChild(g_memorySearcherWindow, WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kMemorySearcherAppInfoX, kMemorySearcherAppInfoY,
        kMemorySearcherAppInfoWidth, kMemorySearcherAppInfoHeight,
        -1);
    setupMemorySearcherAppInfoList(g_memorySearcherAppInfoList);
    refreshMemorySearcherAppInfoList();
}

static void createMemorySearcherSearchControls(bool zh)
{
    createMemorySearcherLabel(zh ? L"1. \u641c\u7d22" : L"1. Search",
        kMemorySearcherSectionX, 96, 108, 22);
    createMemorySearcherLabel(zh ? L"\u5bbd\u5ea6" : L"Width",
        kMemorySearcherControlX, 96, kMemorySearcherTypeLabelWidth, 22);
    HWND widthCombo = createMemorySearcherChild(g_memorySearcherWindow, L"COMBOBOX", L"",
        0, CBS_DROPDOWNLIST,
        kMemorySearcherWidthComboX, 92, kMemorySearcherWidthComboWidth, 120, kMemorySearcherIdWidth);
    SendMessageW(widthCombo, CB_ADDSTRING, 0, (LPARAM)L"u8");
    SendMessageW(widthCombo, CB_ADDSTRING, 0, (LPARAM)L"u16");
    SendMessageW(widthCombo, CB_ADDSTRING, 0, (LPARAM)L"u32");
    SendMessageW(widthCombo, CB_SETCURSEL, 0, 0);

    createMemorySearcherLabel(zh ? L"\u6570\u503c" : L"Value",
        kMemorySearcherValueLabelX, 96, kMemorySearcherValueLabelWidth, 22);
    createMemorySearcherChild(g_memorySearcherWindow, L"EDIT", L"",
        WS_EX_CLIENTEDGE, ES_AUTOHSCROLL,
        kMemorySearcherValueEditX, 92, kMemorySearcherValueEditWidth, 24, kMemorySearcherIdValue);
    createMemorySearcherButton(zh ? L"\u9996\u6b21\u641c\u7d22" : L"New Scan",
        kMemorySearcherSearchButtonX, 90, kMemorySearcherSearchButtonWidth, 28, kMemorySearcherIdNewScan);
}

static void createMemorySearcherFilterControls(bool zh)
{
    createMemorySearcherLabel(zh ? L"2. \u7f29\u5c0f\u7ed3\u679c" : L"2. Narrow Results",
        kMemorySearcherSectionX, 136, 120, 22);
    const MemorySearcherButtonSpec buttons[] = {
        { zh ? L"\u7b49\u4e8e" : L"Equal", kMemorySearcherIdEqual },
        { zh ? L"\u53d8\u5927" : L"Increased", kMemorySearcherIdIncreased },
        { zh ? L"\u53d8\u5c0f" : L"Decreased", kMemorySearcherIdDecreased },
        { zh ? L"\u4e0d\u53d8" : L"Unchanged", kMemorySearcherIdUnchanged },
    };
    createMemorySearcherButtonRow(buttons, 4, kMemorySearcherControlX, 132,
        kMemorySearcherFilterButtonWidth, 28);
}

static void createMemorySearcherActionControls(bool zh)
{
    createMemorySearcherLabel(zh ? L"3. \u5199\u5165/.cht" : L"3. Write / .cht",
        kMemorySearcherSectionX, 176, 120, 22);
    const MemorySearcherButtonSpec actionButtons[] = {
        { zh ? L"\u5199\u5165\u9009\u4e2d\u5730\u5740" : L"Write Selected", kMemorySearcherIdPoke },
        { zh ? L"\u590d\u5236 .cht \u884c" : L"Copy .cht Line", kMemorySearcherIdCopyCheat },
        { zh ? L"\u91cd\u7f6e" : L"Reset", kMemorySearcherIdReset },
    };
    createMemorySearcherButtonRow(actionButtons, 3, kMemorySearcherControlX, 172,
        kMemorySearcherActionButtonWidth, 28);

    createMemorySearcherLabel(zh ? L"\u4fdd\u5b58\u5730\u5740" : L"Saved Addresses",
        kMemorySearcherSectionX, 216, 120, 22);
    const MemorySearcherButtonSpec saveButtons[] = {
        { zh ? L"\u4fdd\u5b58\u9009\u4e2d" : L"Save Selected", kMemorySearcherIdSaveAddress },
        { zh ? L"\u5237\u65b0\u4fdd\u5b58" : L"Refresh Saved", kMemorySearcherIdRefreshSaved },
        { zh ? L"\u6e05\u9664\u4fdd\u5b58" : L"Clear Saved", kMemorySearcherIdClearSaved },
    };
    createMemorySearcherButtonRow(saveButtons, 3, kMemorySearcherControlX, 212,
        kMemorySearcherActionButtonWidth, 28);
}

static void createMemorySearcherResultControls(void)
{
    createMemorySearcherChild(g_memorySearcherWindow, L"STATIC", L"",
        0, SS_ENDELLIPSIS | SS_NOPREFIX,
        kMemorySearcherMarginX, kMemorySearcherStatusY, kMemorySearcherListWidth, 22, kMemorySearcherIdStatus);

    g_memorySearcherList = createMemorySearcherChild(g_memorySearcherWindow, WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kMemorySearcherMarginX, kMemorySearcherListY, kMemorySearcherListWidth, kMemorySearcherListHeight,
        kMemorySearcherIdList);
    setupMemorySearcherList(g_memorySearcherList);

    g_memorySearcherSavedList = createMemorySearcherChild(g_memorySearcherWindow, WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kMemorySearcherMarginX, kMemorySearcherSavedListY, kMemorySearcherListWidth, kMemorySearcherSavedListHeight,
        kMemorySearcherIdSavedList);
    setupMemorySearcherSavedList(g_memorySearcherSavedList);
    refreshMemorySearcherSavedList();
}

static void createMemorySearcherContents(bool zh)
{
    createMemorySearcherIntro(zh);
    createMemorySearcherAppInfoControls(zh);
    createMemorySearcherSearchControls(zh);
    createMemorySearcherFilterControls(zh);
    createMemorySearcherActionControls(zh);
    createMemorySearcherResultControls();
}

static LRESULT CALLBACK memorySearcherWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        switch (id)
        {
        case kMemorySearcherIdRefresh:
            memorySearcherRefresh();
            return 0;
        case kMemorySearcherIdNewScan:
            memorySearcherNewScan();
            return 0;
        case kMemorySearcherIdEqual:
            memorySearcherFilter(MEMORY_SEARCHER_FILTER_EQUAL);
            return 0;
        case kMemorySearcherIdIncreased:
            memorySearcherFilter(MEMORY_SEARCHER_FILTER_INCREASED);
            return 0;
        case kMemorySearcherIdDecreased:
            memorySearcherFilter(MEMORY_SEARCHER_FILTER_DECREASED);
            return 0;
        case kMemorySearcherIdUnchanged:
            memorySearcherFilter(MEMORY_SEARCHER_FILTER_UNCHANGED);
            return 0;
        case kMemorySearcherIdPoke:
            memorySearcherPokeSelected();
            return 0;
        case kMemorySearcherIdCopyCheat:
            memorySearcherCopyCheatLine();
            return 0;
        case kMemorySearcherIdReset:
            memorySearcherReset();
            return 0;
        case kMemorySearcherIdSaveAddress:
            memorySearcherSaveSelected();
            return 0;
        case kMemorySearcherIdRefreshSaved:
            memorySearcherRefreshSaved();
            return 0;
        case kMemorySearcherIdClearSaved:
            memorySearcherClearSaved();
            return 0;
        case kMemorySearcherIdClose:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_NOTIFY:
    {
        NMHDR* header = (NMHDR*)lParam;
        if (header && header->code == LVN_ITEMCHANGED)
        {
            NMLISTVIEW* changed = (NMLISTVIEW*)lParam;
            if ((changed->uNewState & LVIS_SELECTED) != 0)
            {
                if (header->idFrom == kMemorySearcherIdList)
                {
                    g_memorySearcherSelectionSource = MEMORY_SEARCHER_SELECTION_CANDIDATE;
                }
                else if (header->idFrom == kMemorySearcherIdSavedList)
                {
                    g_memorySearcherSelectionSource = MEMORY_SEARCHER_SELECTION_SAVED;
                }
            }
        }
        break;
    }
    case WM_ACTIVATE:
        refreshMemorySearcherAppInfoList();
        break;
    case WM_CTLCOLORSTATIC:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        if (id == kMemorySearcherIdStatus)
        {
            SetBkMode((HDC)wParam, OPAQUE);
            SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
            SetTextColor((HDC)wParam, GetSysColor(COLOR_BTNTEXT));
            return (LRESULT)(g_memorySearcherBackgroundBrush ?
                g_memorySearcherBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
        }
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)(g_memorySearcherBackgroundBrush ?
            g_memorySearcherBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_memorySearcherWindow == hwnd)
        {
            g_memorySearcherWindow = NULL;
            g_memorySearcherList = NULL;
            g_memorySearcherSavedList = NULL;
            g_memorySearcherAppInfoList = NULL;
        }
        if (g_memorySearcherFont && g_memorySearcherOwnFont)
        {
            DeleteObject(g_memorySearcherFont);
        }
        g_memorySearcherFont = NULL;
        g_memorySearcherOwnFont = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensureMemorySearcherWindowClass(void)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = memorySearcherWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DingooPieMemorySearcherWindow";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = g_memorySearcherBackgroundBrush;
    RegisterClassW(&wc);
    registered = true;
}

void memorySearcherOpenWindow(HWND owner, UiLanguage language)
{
    (void)owner;
    g_memorySearcherLanguage = language;
    if (g_memorySearcherWindow)
    {
        ShowWindow(g_memorySearcherWindow, SW_SHOWNOACTIVATE);
        refreshMemorySearcherAppInfoList();
        return;
    }

    INITCOMMONCONTROLSEX controls;
    memset(&controls, 0, sizeof(controls));
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    bool zh = memorySearcherChinese();
    g_memorySearcherBackgroundBrush = GetSysColorBrush(COLOR_BTNFACE);
    ensureMemorySearcherWindowClass();

    g_memorySearcherWindow = CreateWindowExW(WS_EX_CONTROLPARENT,
        L"DingooPieMemorySearcherWindow",
        memorySearcherTitle(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, kMemorySearcherWindowWidth, kMemorySearcherWindowHeight,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_memorySearcherWindow)
    {
        return;
    }

    applyMemorySearcherIcon(g_memorySearcherWindow);
    createMemorySearcherContents(zh);
    refreshMemorySearcherResults();

    ShowWindow(g_memorySearcherWindow, SW_SHOWNOACTIVATE);
    UpdateWindow(g_memorySearcherWindow);
}

#endif

#include "cheat_finder.h"

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

struct CheatFinderCandidate
{
    uint32_t address;
    uint32_t previous;
};

struct CheatFinderLockedAddress
{
    CheatFinderCandidate candidate;
    int width;
};

struct CheatFinderButtonSpec
{
    const wchar_t* text;
    int id;
};

enum CheatFinderFilter
{
    CHEAT_FINDER_FILTER_EQUAL,
    CHEAT_FINDER_FILTER_INCREASED,
    CHEAT_FINDER_FILTER_DECREASED,
    CHEAT_FINDER_FILTER_UNCHANGED
};

enum CheatFinderSelectionSource
{
    CHEAT_FINDER_SELECTION_NONE,
    CHEAT_FINDER_SELECTION_CANDIDATE,
    CHEAT_FINDER_SELECTION_LOCKED
};

static HWND g_cheatFinderWindow = NULL;
static HWND g_cheatFinderList = NULL;
static HWND g_cheatFinderLockedList = NULL;
static HBRUSH g_cheatFinderBackgroundBrush = NULL;
static HFONT g_cheatFinderFont = NULL;
static bool g_cheatFinderOwnFont = false;
static UiLanguage g_cheatFinderLanguage = UI_LANGUAGE_ENGLISH;
static std::vector<CheatFinderCandidate> g_cheatFinderCandidates;
static std::vector<CheatFinderLockedAddress> g_cheatFinderLockedAddresses;
static bool g_cheatFinderHasScan = false;
static bool g_cheatFinderCapped = false;
static int g_cheatFinderScanWidth = 1;
static CheatFinderSelectionSource g_cheatFinderSelectionSource = CHEAT_FINDER_SELECTION_NONE;

static const size_t kCheatFinderMaxCandidates = 250000;
static const size_t kCheatFinderDisplayLimit = 3000;
static const uint32_t kCheatFinderScanBegin = 0x80000000u;
static const uint32_t kCheatFinderScanEnd = 0xb0000000u;

static const int kCheatFinderWindowWidth = 980;
static const int kCheatFinderWindowHeight = 760;
static const int kCheatFinderMarginX = 18;
static const int kCheatFinderRightX = 944;
static const int kCheatFinderListY = 278;
static const int kCheatFinderListWidth = kCheatFinderRightX - kCheatFinderMarginX;
static const int kCheatFinderListHeight = 306;
static const int kCheatFinderLockedListY = 592;
static const int kCheatFinderLockedListHeight = 118;
static const int kCheatFinderListColumnPadding = 4;
static const int kCheatFinderSectionX = 18;
static const int kCheatFinderControlX = 146;
static const int kCheatFinderButtonGap = 10;
static const int kCheatFinderFilterButtonWidth = 92;
static const int kCheatFinderActionButtonWidth = 126;
static const int kCheatFinderCloseButtonWidth = 74;
static const int kCheatFinderTopTextWidth =
    kCheatFinderListWidth - kCheatFinderCloseButtonWidth - kCheatFinderButtonGap;

static const int kCheatFinderIdWidth = 43001;
static const int kCheatFinderIdValue = 43002;
static const int kCheatFinderIdNewScan = 43003;
static const int kCheatFinderIdEqual = 43004;
static const int kCheatFinderIdIncreased = 43005;
static const int kCheatFinderIdDecreased = 43006;
static const int kCheatFinderIdUnchanged = 43007;
static const int kCheatFinderIdReset = 43008;
static const int kCheatFinderIdClose = 43009;
static const int kCheatFinderIdStatus = 43010;
static const int kCheatFinderIdList = 43011;
static const int kCheatFinderIdPoke = 43012;
static const int kCheatFinderIdCopyCheat = 43013;
static const int kCheatFinderIdLockAddress = 43014;
static const int kCheatFinderIdRefreshLocked = 43015;
static const int kCheatFinderIdClearLocked = 43016;
static const int kCheatFinderIdLockedList = 43017;

static bool cheatFinderChinese(void)
{
    return g_cheatFinderLanguage == UI_LANGUAGE_CHINESE;
}

static const wchar_t* cheatFinderTitle(void)
{
    return cheatFinderChinese() ? L"\u5185\u5b58\u641c\u7d22\u5668" : L"Cheat Finder";
}

static HICON loadCheatFinderIcon(int size)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED);
}

static void applyCheatFinderIcon(HWND window)
{
    if (!window)
    {
        return;
    }

    HICON largeIcon = loadCheatFinderIcon(32);
    HICON smallIcon = loadCheatFinderIcon(16);
    if (largeIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    }
    if (smallIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    }
}

static HFONT cheatFinderFont(void)
{
    if (g_cheatFinderFont)
    {
        return g_cheatFinderFont;
    }

    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0))
    {
        g_cheatFinderFont = CreateFontIndirectW(&metrics.lfMessageFont);
        g_cheatFinderOwnFont = g_cheatFinderFont != NULL;
    }
    if (!g_cheatFinderFont)
    {
        g_cheatFinderFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_cheatFinderOwnFont = false;
    }
    return g_cheatFinderFont;
}

static HWND cheatFinderItem(int id)
{
    return g_cheatFinderWindow ? GetDlgItem(g_cheatFinderWindow, id) : NULL;
}

static HWND createCheatFinderChild(HWND parent, const wchar_t* className, const wchar_t* text,
    DWORD exStyle, DWORD style, int x, int y, int w, int h, int id)
{
    HWND child = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (child)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)cheatFinderFont(), TRUE);
        SetWindowTheme(child, L"Explorer", NULL);
    }
    return child;
}

static HWND createCheatFinderLabel(const wchar_t* text, int x, int y, int w, int h)
{
    return createCheatFinderChild(g_cheatFinderWindow, L"STATIC", text,
        0, 0, x, y, w, h, -1);
}

static HWND createCheatFinderButton(const wchar_t* text, int x, int y, int w, int h, int id)
{
    return createCheatFinderChild(g_cheatFinderWindow, L"BUTTON", text,
        0, 0, x, y, w, h, id);
}

static void createCheatFinderButtonRow(const CheatFinderButtonSpec* buttons, int count,
    int x, int y, int width, int height)
{
    if (!buttons || count <= 0)
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        createCheatFinderButton(buttons[i].text, x, y, width, height, buttons[i].id);
        x += width + kCheatFinderButtonGap;
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

static void setCheatFinderStatus(const wchar_t* text)
{
    setWindowTextAndRedraw(cheatFinderItem(kCheatFinderIdStatus), text);
}

static int selectedCheatFinderWidth(void)
{
    HWND combo = cheatFinderItem(kCheatFinderIdWidth);
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

static int activeCheatFinderWidth(void)
{
    return g_cheatFinderHasScan ? g_cheatFinderScanWidth : selectedCheatFinderWidth();
}

static bool parseCheatFinderValue(uint32_t* out)
{
    if (!out)
    {
        return false;
    }

    wchar_t text[64] = {};
    GetWindowTextW(cheatFinderItem(kCheatFinderIdValue), text, (int)(sizeof(text) / sizeof(text[0])));
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

static const wchar_t* cheatFinderWidthName(int width);

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

static int cheatFinderListAvailableWidth(HWND list)
{
    RECT rect;
    memset(&rect, 0, sizeof(rect));
    if (!list || !GetClientRect(list, &rect))
    {
        return 0;
    }

    int width = rect.right - rect.left -
        GetSystemMetrics(SM_CXVSCROLL) - kCheatFinderListColumnPadding;
    return width > 0 ? width : 0;
}

static void addCheatFinderListColumn(HWND list, int index, const wchar_t* text, int width)
{
    LVCOLUMNW column;
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = (LPWSTR)text;
    column.cx = width;
    column.iSubItem = index;
    SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&column);
}

static void setupFittedCheatFinderColumns(HWND list,
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

    int availableWidth = cheatFinderListAvailableWidth(list);
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
        addCheatFinderListColumn(list, i, headers[i], width);
    }
}

static void setupCheatFinderReportList(HWND list,
    const wchar_t** headers, const int* weights, int count)
{
    if (!list)
    {
        return;
    }

    DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
    ListView_SetExtendedListViewStyle(list, exStyle);
    setupFittedCheatFinderColumns(list, headers, weights, count);
}

static void refreshCheatFinderLockedList(void)
{
    if (!g_cheatFinderLockedList)
    {
        return;
    }

    int selectedRow = ListView_GetNextItem(g_cheatFinderLockedList, -1, LVNI_SELECTED);
    SendMessageW(g_cheatFinderLockedList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_cheatFinderLockedList);

    for (size_t i = 0; i < g_cheatFinderLockedAddresses.size(); ++i)
    {
        const CheatFinderLockedAddress& locked = g_cheatFinderLockedAddresses[i];
        uint32_t current = 0;
        wchar_t addressText[32] = {};
        wchar_t valueText[64] = {};
        swprintf(addressText, sizeof(addressText) / sizeof(addressText[0]),
            L"0x%08X", locked.candidate.address);
        if (readRuntimeValue(locked.candidate.address, locked.width, &current))
        {
            formatValue(current, locked.width, valueText,
                sizeof(valueText) / sizeof(valueText[0]));
        }
        else
        {
            wcscpy(valueText, cheatFinderChinese() ?
                L"\u8bfb\u53d6\u5931\u8d25" : L"Read failed");
        }

        insertListViewRow(g_cheatFinderLockedList, (int)i, addressText);
        setListViewText(g_cheatFinderLockedList, (int)i, 1,
            cheatFinderWidthName(locked.width));
        setListViewText(g_cheatFinderLockedList, (int)i, 2, valueText);
    }

    if (selectedRow >= 0 && (size_t)selectedRow < g_cheatFinderLockedAddresses.size())
    {
        ListView_SetItemState(g_cheatFinderLockedList, selectedRow,
            LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        g_cheatFinderSelectionSource = CHEAT_FINDER_SELECTION_LOCKED;
    }
    SendMessageW(g_cheatFinderLockedList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_cheatFinderLockedList, NULL, TRUE);
}

static void refreshCheatFinderResults(void)
{
    if (!g_cheatFinderList)
    {
        return;
    }

    SendMessageW(g_cheatFinderList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_cheatFinderList);

    int width = activeCheatFinderWidth();
    size_t shown = g_cheatFinderCandidates.size();
    if (shown > kCheatFinderDisplayLimit)
    {
        shown = kCheatFinderDisplayLimit;
    }

    for (size_t i = 0; i < shown; ++i)
    {
        const CheatFinderCandidate& candidate = g_cheatFinderCandidates[i];
        wchar_t addressText[32] = {};
        wchar_t valueText[64] = {};
        wchar_t previousText[64] = {};
        uint32_t current = 0;
        swprintf(addressText, sizeof(addressText) / sizeof(addressText[0]), L"0x%08X", candidate.address);
        if (!readRuntimeValue(candidate.address, width, &current))
        {
            current = candidate.previous;
        }
        formatValue(current, width, valueText, sizeof(valueText) / sizeof(valueText[0]));
        formatValue(candidate.previous, width, previousText, sizeof(previousText) / sizeof(previousText[0]));

        insertListViewRow(g_cheatFinderList, (int)i, addressText);
        setListViewText(g_cheatFinderList, (int)i, 1, valueText);
        setListViewText(g_cheatFinderList, (int)i, 2, previousText);
    }

    SendMessageW(g_cheatFinderList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_cheatFinderList, NULL, TRUE);

    wchar_t status[192] = {};
    if (!g_cheatFinderHasScan)
    {
        swprintf(status, sizeof(status) / sizeof(status[0]), cheatFinderChinese() ?
            L"\u5148\u9009 u8/u16/u32\uff0c\u8f93\u5165\u5f53\u524d\u6570\u503c\uff0c\u70b9\u51fb\u300c\u9996\u6b21\u641c\u7d22\u300d\u3002" :
            L"Choose u8/u16/u32, enter the current value, then click New Scan.");
    }
    else if (g_cheatFinderCapped)
    {
        swprintf(status, sizeof(status) / sizeof(status[0]), cheatFinderChinese() ?
            L"\u5019\u9009\u8fc7\u591a\uff0c\u5df2\u9650\u5236\u4e3a %u \u6761\uff1b\u5217\u8868\u663e\u793a\u524d %u \u6761\u3002" :
            L"Too many matches; capped at %u candidates. Showing first %u.",
            (unsigned)kCheatFinderMaxCandidates, (unsigned)shown);
    }
    else
    {
        swprintf(status, sizeof(status) / sizeof(status[0]), cheatFinderChinese() ?
            L"\u5019\u9009\uff1a%u \u6761\uff1b\u5217\u8868\u663e\u793a\uff1a%u \u6761\u3002" :
            L"Candidates: %u; showing: %u.",
            (unsigned)g_cheatFinderCandidates.size(), (unsigned)shown);
    }
    setCheatFinderStatus(status);
    refreshCheatFinderLockedList();
}

struct CheatFinderScanContext
{
    int width;
    uint32_t target;
    std::vector<CheatFinderCandidate>* candidates;
    bool capped;
};

static bool scanCheatFinderRegion(uint32_t start, uint32_t size, void* userData)
{
    CheatFinderScanContext* context = (CheatFinderScanContext*)userData;
    if (!context || !context->candidates || context->capped)
    {
        return true;
    }

    // Scan only the cached alias to avoid duplicate physical addresses.
    uint64_t regionBegin = start;
    uint64_t regionEnd = regionBegin + size;
    if (regionBegin < kCheatFinderScanBegin)
    {
        regionBegin = kCheatFinderScanBegin;
    }
    if (regionEnd > kCheatFinderScanEnd)
    {
        regionEnd = kCheatFinderScanEnd;
    }
    if (regionBegin >= regionEnd || regionEnd - regionBegin < (uint32_t)context->width)
    {
        return true;
    }

    uint32_t width = (uint32_t)context->width;
    uint32_t alignedBegin = (uint32_t)((regionBegin + (width - 1u)) & ~(uint64_t)(width - 1u));
    if (alignedBegin >= regionEnd)
    {
        return true;
    }

    const uint32_t chunkSize = 64 * 1024;
    std::vector<uint8_t> buffer(chunkSize + 4);
    for (uint32_t address = alignedBegin; address < regionEnd;)
    {
        uint32_t remaining = (uint32_t)(regionEnd - address);
        uint32_t readSize = remaining > chunkSize ? chunkSize : remaining;
        if (readSize < width)
        {
            break;
        }
        if (!emulatorRuntimeReadMemory(address, &buffer[0], readSize))
        {
            address += readSize;
            continue;
        }

        for (uint32_t offset = 0; offset + width <= readSize; offset += width)
        {
            uint32_t value = readLeValue(&buffer[offset], context->width);
            if (value == context->target)
            {
                CheatFinderCandidate candidate;
                candidate.address = address + offset;
                candidate.previous = value;
                context->candidates->push_back(candidate);
                if (context->candidates->size() >= kCheatFinderMaxCandidates)
                {
                    context->capped = true;
                    return true;
                }
            }
        }
        address += readSize - (readSize % width);
    }
    return true;
}

static void cheatFinderNewScan(void)
{
    uint32_t target = 0;
    int width = selectedCheatFinderWidth();
    if (!parseCheatFinderValue(&target))
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u8bf7\u8f93\u5165\u5341\u8fdb\u5236\u6216 0x \u5341\u516d\u8fdb\u5236\u6570\u503c\u3002" :
            L"Enter a decimal value or 0x hexadecimal value.");
        return;
    }
    target &= maskForWidth(width);

    g_cheatFinderCandidates.clear();
    g_cheatFinderCapped = false;
    g_cheatFinderScanWidth = width;

    CheatFinderScanContext context;
    context.width = width;
    context.target = target;
    context.candidates = &g_cheatFinderCandidates;
    context.capped = false;

    bool ok = emulatorRuntimeForEachReadableRegion(scanCheatFinderRegion, &context);
    g_cheatFinderCapped = context.capped;
    g_cheatFinderHasScan = ok;
    if (!ok)
    {
        refreshCheatFinderResults();
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u672a\u8fd0\u884c\u6e38\u620f\uff0c\u65e0\u6cd5\u641c\u7d22\u5185\u5b58\u3002" :
            L"No game is running; cannot scan memory.");
        return;
    }

    refreshCheatFinderResults();
}

static bool candidateMatchesFilter(uint32_t previous, uint32_t current, uint32_t target,
    CheatFinderFilter filter)
{
    switch (filter)
    {
    case CHEAT_FINDER_FILTER_EQUAL:
        return current == target;
    case CHEAT_FINDER_FILTER_INCREASED:
        return current > previous;
    case CHEAT_FINDER_FILTER_DECREASED:
        return current < previous;
    case CHEAT_FINDER_FILTER_UNCHANGED:
        return current == previous;
    default:
        return false;
    }
}

static void cheatFinderFilter(CheatFinderFilter filter)
{
    if (!g_cheatFinderHasScan)
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u8bf7\u5148\u8fdb\u884c\u9996\u6b21\u641c\u7d22\u3002" :
            L"Run a new scan first.");
        return;
    }

    int width = activeCheatFinderWidth();
    uint32_t target = 0;
    if (filter == CHEAT_FINDER_FILTER_EQUAL)
    {
        if (!parseCheatFinderValue(&target))
        {
            setCheatFinderStatus(cheatFinderChinese() ?
                L"\u7b5b\u9009\u7b49\u4e8e\u65f6\u9700\u8981\u8f93\u5165\u6570\u503c\u3002" :
                L"Equal filtering needs a value.");
            return;
        }
        target &= maskForWidth(width);
    }

    std::vector<CheatFinderCandidate> next;
    next.reserve(g_cheatFinderCandidates.size());
    for (size_t i = 0; i < g_cheatFinderCandidates.size(); ++i)
    {
        CheatFinderCandidate candidate = g_cheatFinderCandidates[i];
        uint32_t current = 0;
        if (!readRuntimeValue(candidate.address, width, &current))
        {
            continue;
        }
        if (candidateMatchesFilter(candidate.previous, current, target, filter))
        {
            candidate.previous = current;
            next.push_back(candidate);
        }
    }
    g_cheatFinderCandidates.swap(next);
    g_cheatFinderCapped = false;
    refreshCheatFinderResults();
}

static void cheatFinderReset(void)
{
    g_cheatFinderCandidates.clear();
    g_cheatFinderLockedAddresses.clear();
    g_cheatFinderHasScan = false;
    g_cheatFinderCapped = false;
    g_cheatFinderScanWidth = selectedCheatFinderWidth();
    g_cheatFinderSelectionSource = CHEAT_FINDER_SELECTION_NONE;
    refreshCheatFinderResults();
}

static bool selectedCheatFinderCandidate(CheatFinderCandidate* out)
{
    if (!out || !g_cheatFinderList)
    {
        return false;
    }
    int row = ListView_GetNextItem(g_cheatFinderList, -1, LVNI_SELECTED);
    if (row < 0 || (size_t)row >= g_cheatFinderCandidates.size())
    {
        return false;
    }
    *out = g_cheatFinderCandidates[(size_t)row];
    return true;
}

static bool selectedLockedCheatFinderCandidate(CheatFinderCandidate* out, int* width, int* rowOut)
{
    if (!out || !width || !g_cheatFinderLockedList)
    {
        return false;
    }
    int row = ListView_GetNextItem(g_cheatFinderLockedList, -1, LVNI_SELECTED);
    if (row < 0 || (size_t)row >= g_cheatFinderLockedAddresses.size())
    {
        return false;
    }

    const CheatFinderLockedAddress& locked = g_cheatFinderLockedAddresses[(size_t)row];
    *out = locked.candidate;
    *width = locked.width;
    if (rowOut)
    {
        *rowOut = row;
    }
    return true;
}

static bool activeCheatFinderCandidate(CheatFinderCandidate* out, int* width)
{
    if (!out || !width)
    {
        return false;
    }
    if (g_cheatFinderSelectionSource == CHEAT_FINDER_SELECTION_LOCKED &&
        selectedLockedCheatFinderCandidate(out, width, NULL))
    {
        return true;
    }
    if (g_cheatFinderSelectionSource == CHEAT_FINDER_SELECTION_CANDIDATE &&
        selectedCheatFinderCandidate(out))
    {
        *width = activeCheatFinderWidth();
        return true;
    }
    if (selectedLockedCheatFinderCandidate(out, width, NULL))
    {
        return true;
    }
    if (selectedCheatFinderCandidate(out))
    {
        *width = activeCheatFinderWidth();
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

static const wchar_t* cheatFinderWidthName(int width)
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

static void cheatFinderPokeSelected(void)
{
    CheatFinderCandidate candidate;
    uint32_t value = 0;
    int width = 1;
    if (!activeCheatFinderCandidate(&candidate, &width))
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u8bf7\u5148\u9009\u4e2d\u6216\u9501\u5b9a\u4e00\u4e2a\u5730\u5740\u3002" :
            L"Select or lock one address first.");
        return;
    }
    if (!parseCheatFinderValue(&value))
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u8bf7\u8f93\u5165\u8981\u5199\u5165\u7684\u6570\u503c\u3002" :
            L"Enter the value to write.");
        return;
    }
    value &= maskForWidth(width);

    uint8_t bytes[4] = {};
    writeLeValue(bytes, width, value);
    if (!emulatorRuntimeWriteMemory(candidate.address, bytes, (size_t)width))
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u5199\u5165\u5931\u8d25\uff0c\u6e38\u620f\u53ef\u80fd\u5df2\u505c\u6b62\u6216\u5730\u5740\u5931\u6548\u3002" :
            L"Write failed; the game may have stopped or the address changed.");
        return;
    }

    int row = ListView_GetNextItem(g_cheatFinderList, -1, LVNI_SELECTED);
    if (row >= 0 && (size_t)row < g_cheatFinderCandidates.size())
    {
        g_cheatFinderCandidates[(size_t)row].previous = value;
    }
    refreshCheatFinderResults();
    setCheatFinderStatus(cheatFinderChinese() ?
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

static void cheatFinderCopyCheatLine(void)
{
    CheatFinderCandidate candidate;
    uint32_t value = 0;
    int width = 1;
    if (!activeCheatFinderCandidate(&candidate, &width))
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u8bf7\u5148\u9009\u4e2d\u6216\u9501\u5b9a\u4e00\u4e2a\u5730\u5740\u3002" :
            L"Select or lock one address first.");
        return;
    }
    if (!parseCheatFinderValue(&value))
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u8bf7\u8f93\u5165\u8981\u56fa\u5b9a\u7684\u6570\u503c\u3002" :
            L"Enter the value to freeze.");
        return;
    }
    value &= maskForWidth(width);

    wchar_t line[256] = {};
    swprintf(line, sizeof(line) / sizeof(line[0]),
        L"on|\u65b0\u91d1\u624b\u6307/New Cheat|%ls|0x%08X|0x%X",
        cheatFinderWidthName(width),
        candidate.address,
        value);
    if (!setClipboardText(g_cheatFinderWindow, line))
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u590d\u5236\u5230\u526a\u8d34\u677f\u5931\u8d25\u3002" :
            L"Failed to copy to the clipboard.");
        return;
    }
    setCheatFinderStatus(cheatFinderChinese() ?
        L"\u5df2\u590d\u5236 .cht \u884c\uff0c\u53ef\u7c98\u8d34\u5230\u5bf9\u5e94\u91d1\u624b\u6307\u6587\u4ef6\u3002" :
        L"Copied a .cht line; paste it into the matching cheat file.");
}

static void cheatFinderLockSelected(void)
{
    CheatFinderCandidate candidate;
    if (!selectedCheatFinderCandidate(&candidate))
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u8bf7\u5148\u5728\u5217\u8868\u4e2d\u9009\u4e2d\u8981\u9501\u5b9a\u7684\u5730\u5740\u3002" :
            L"Select the address to lock first.");
        return;
    }

    int width = activeCheatFinderWidth();
    int selectedRow = -1;
    for (size_t i = 0; i < g_cheatFinderLockedAddresses.size(); ++i)
    {
        if (g_cheatFinderLockedAddresses[i].candidate.address == candidate.address &&
            g_cheatFinderLockedAddresses[i].width == width)
        {
            g_cheatFinderLockedAddresses[i].candidate = candidate;
            selectedRow = (int)i;
            break;
        }
    }
    if (selectedRow < 0)
    {
        CheatFinderLockedAddress locked;
        locked.candidate = candidate;
        locked.width = width;
        g_cheatFinderLockedAddresses.push_back(locked);
        selectedRow = (int)g_cheatFinderLockedAddresses.size() - 1;
    }

    refreshCheatFinderLockedList();
    ListView_SetItemState(g_cheatFinderLockedList, selectedRow,
        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    g_cheatFinderSelectionSource = CHEAT_FINDER_SELECTION_LOCKED;
    setCheatFinderStatus(cheatFinderChinese() ?
        L"\u5df2\u5c06\u9009\u4e2d\u5730\u5740\u52a0\u5165\u9501\u5b9a\u5217\u8868\u3002" :
        L"Added the selected address to the locked list.");
}

static void cheatFinderRefreshLocked(void)
{
    if (g_cheatFinderLockedAddresses.empty())
    {
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u9501\u5b9a\u5217\u8868\u4e3a\u7a7a\u3002" :
            L"The locked list is empty.");
        return;
    }

    refreshCheatFinderLockedList();
    setCheatFinderStatus(cheatFinderChinese() ?
        L"\u5df2\u5237\u65b0\u9501\u5b9a\u5217\u8868\u4e2d\u7684\u5f53\u524d\u503c\u3002" :
        L"Refreshed the locked list values.");
}

static void cheatFinderClearLocked(void)
{
    int row = g_cheatFinderLockedList ?
        ListView_GetNextItem(g_cheatFinderLockedList, -1, LVNI_SELECTED) : -1;
    if (row >= 0 && (size_t)row < g_cheatFinderLockedAddresses.size())
    {
        g_cheatFinderLockedAddresses.erase(g_cheatFinderLockedAddresses.begin() + row);
        if (g_cheatFinderLockedAddresses.empty())
        {
            g_cheatFinderSelectionSource = CHEAT_FINDER_SELECTION_NONE;
        }
        refreshCheatFinderLockedList();
        setCheatFinderStatus(cheatFinderChinese() ?
            L"\u5df2\u4ece\u9501\u5b9a\u5217\u8868\u79fb\u9664\u9009\u4e2d\u5730\u5740\u3002" :
            L"Removed the selected locked address.");
        return;
    }

    g_cheatFinderLockedAddresses.clear();
    g_cheatFinderSelectionSource = CHEAT_FINDER_SELECTION_NONE;
    refreshCheatFinderLockedList();
    setCheatFinderStatus(cheatFinderChinese() ?
        L"\u5df2\u6e05\u7a7a\u9501\u5b9a\u5217\u8868\u3002" :
        L"Cleared the locked list.");
}

static void setupCheatFinderList(HWND list)
{
    const wchar_t* address = cheatFinderChinese() ? L"\u5730\u5740" : L"Address";
    const wchar_t* current = cheatFinderChinese() ? L"\u5f53\u524d\u503c" : L"Current Value";
    const wchar_t* previous = cheatFinderChinese() ? L"\u4e0a\u6b21\u503c" : L"Previous Value";
    const wchar_t* headers[] = { address, current, previous };
    int weights[] = { 180, 260, 260 };
    setupCheatFinderReportList(list, headers, weights, 3);
}

static void setupCheatFinderLockedList(HWND list)
{
    const wchar_t* address = cheatFinderChinese() ? L"\u5730\u5740" : L"Address";
    const wchar_t* type = cheatFinderChinese() ? L"\u7c7b\u578b" : L"Type";
    const wchar_t* current = cheatFinderChinese() ? L"\u5f53\u524d\u503c" : L"Current Value";
    const wchar_t* headers[] = { address, type, current };
    int weights[] = { 220, 80, 400 };
    setupCheatFinderReportList(list, headers, weights, 3);
}

static void createCheatFinderIntro(bool zh)
{
    createCheatFinderButton(zh ? L"\u5173\u95ed" : L"Close",
        kCheatFinderRightX - kCheatFinderCloseButtonWidth, 10,
        kCheatFinderCloseButtonWidth, 28, kCheatFinderIdClose);

    createCheatFinderLabel(zh ?
        L"\u6b65\u9aa4\uff1a\u9996\u6b21\u641c\u7d22 -> \u6e38\u620f\u4e2d\u6539\u53d8\u6570\u503c -> \u7528\u53d8\u5927/\u53d8\u5c0f/\u4e0d\u53d8\u7f29\u5c0f\u7ed3\u679c" :
        L"Steps: New Scan -> change the in-game value -> narrow with Bigger/Smaller/Unchanged",
        kCheatFinderMarginX, 14, kCheatFinderTopTextWidth, 22);
    createCheatFinderLabel(zh ?
        L"\u627e\u5230\u53ef\u80fd\u5730\u5740\u540e\uff1a\u52a0\u5165\u9501\u5b9a\u5217\u8868\uff0c\u518d\u5237\u65b0\u5f53\u524d\u503c\u6216\u590d\u5236 .cht \u884c\u3002" :
        L"Lock likely rows below, refresh values, or copy a .cht line.",
        kCheatFinderMarginX, 38, kCheatFinderListWidth, 22);
    createCheatFinderLabel(zh ?
        L"\u7c7b\u578b\u8303\u56f4\uff1au8=0~255\uff0cu16=0~65535\uff0cu32=0~4294967295\u3002\u4e0d\u786e\u5b9a\u65f6\u5148\u8bd5 u8\u3002" :
        L"Type ranges: u8=0-255, u16=0-65535, u32=0-4294967295. If unsure, try u8 first.",
        kCheatFinderMarginX, 62, kCheatFinderListWidth, 22);
}

static void createCheatFinderSearchControls(bool zh)
{
    createCheatFinderLabel(zh ? L"1. \u641c\u7d22" : L"1. Search",
        kCheatFinderSectionX, 96, 108, 22);
    createCheatFinderLabel(zh ? L"\u7c7b\u578b" : L"Type",
        kCheatFinderControlX, 96, 48, 22);
    HWND widthCombo = createCheatFinderChild(g_cheatFinderWindow, L"COMBOBOX", L"",
        0, CBS_DROPDOWNLIST,
        202, 92, 86, 120, kCheatFinderIdWidth);
    SendMessageW(widthCombo, CB_ADDSTRING, 0, (LPARAM)L"u8");
    SendMessageW(widthCombo, CB_ADDSTRING, 0, (LPARAM)L"u16");
    SendMessageW(widthCombo, CB_ADDSTRING, 0, (LPARAM)L"u32");
    SendMessageW(widthCombo, CB_SETCURSEL, 0, 0);

    createCheatFinderLabel(zh ? L"\u6570\u503c" : L"Value",
        306, 96, 46, 22);
    createCheatFinderChild(g_cheatFinderWindow, L"EDIT", L"",
        WS_EX_CLIENTEDGE, ES_AUTOHSCROLL,
        360, 92, 130, 24, kCheatFinderIdValue);
    createCheatFinderButton(zh ? L"\u9996\u6b21\u641c\u7d22" : L"New Scan",
        508, 90, 108, 28, kCheatFinderIdNewScan);
}

static void createCheatFinderFilterControls(bool zh)
{
    createCheatFinderLabel(zh ? L"2. \u7f29\u5c0f\u7ed3\u679c" : L"2. Narrow Results",
        kCheatFinderSectionX, 136, 120, 22);
    const CheatFinderButtonSpec buttons[] = {
        { zh ? L"\u7b49\u4e8e" : L"Equal", kCheatFinderIdEqual },
        { zh ? L"\u53d8\u5927" : L"Bigger", kCheatFinderIdIncreased },
        { zh ? L"\u53d8\u5c0f" : L"Smaller", kCheatFinderIdDecreased },
        { zh ? L"\u4e0d\u53d8" : L"Unchanged", kCheatFinderIdUnchanged },
    };
    createCheatFinderButtonRow(buttons, 4, kCheatFinderControlX, 132,
        kCheatFinderFilterButtonWidth, 28);
}

static void createCheatFinderActionControls(bool zh)
{
    createCheatFinderLabel(zh ? L"3. \u9a8c\u8bc1/\u751f\u6210" : L"3. Test / Create",
        kCheatFinderSectionX, 176, 120, 22);
    const CheatFinderButtonSpec actionButtons[] = {
        { zh ? L"\u5199\u5165\u9009\u4e2d\u5730\u5740" : L"Write Selected", kCheatFinderIdPoke },
        { zh ? L"\u590d\u5236 .cht \u884c" : L"Copy .cht Line", kCheatFinderIdCopyCheat },
        { zh ? L"\u91cd\u7f6e" : L"Reset", kCheatFinderIdReset },
    };
    createCheatFinderButtonRow(actionButtons, 3, kCheatFinderControlX, 172,
        kCheatFinderActionButtonWidth, 28);

    createCheatFinderLabel(zh ? L"\u9501\u5b9a\u5730\u5740" : L"Locked Address",
        kCheatFinderSectionX, 216, 120, 22);
    const CheatFinderButtonSpec lockButtons[] = {
        { zh ? L"\u9501\u5b9a\u9009\u4e2d" : L"Lock Selected", kCheatFinderIdLockAddress },
        { zh ? L"\u5237\u65b0\u9501\u5b9a" : L"Refresh Lock", kCheatFinderIdRefreshLocked },
        { zh ? L"\u6e05\u9664\u9501\u5b9a" : L"Clear Lock", kCheatFinderIdClearLocked },
    };
    createCheatFinderButtonRow(lockButtons, 3, kCheatFinderControlX, 212,
        kCheatFinderActionButtonWidth, 28);
}

static void createCheatFinderResultControls(void)
{
    createCheatFinderChild(g_cheatFinderWindow, L"STATIC", L"",
        0, 0, kCheatFinderMarginX, 248, kCheatFinderListWidth, 22, kCheatFinderIdStatus);

    g_cheatFinderList = createCheatFinderChild(g_cheatFinderWindow, WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kCheatFinderMarginX, kCheatFinderListY, kCheatFinderListWidth, kCheatFinderListHeight,
        kCheatFinderIdList);
    setupCheatFinderList(g_cheatFinderList);

    g_cheatFinderLockedList = createCheatFinderChild(g_cheatFinderWindow, WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kCheatFinderMarginX, kCheatFinderLockedListY, kCheatFinderListWidth, kCheatFinderLockedListHeight,
        kCheatFinderIdLockedList);
    setupCheatFinderLockedList(g_cheatFinderLockedList);
    refreshCheatFinderLockedList();
}

static void createCheatFinderContents(bool zh)
{
    createCheatFinderIntro(zh);
    createCheatFinderSearchControls(zh);
    createCheatFinderFilterControls(zh);
    createCheatFinderActionControls(zh);
    createCheatFinderResultControls();
}

static LRESULT CALLBACK cheatFinderWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        switch (id)
        {
        case kCheatFinderIdNewScan:
            cheatFinderNewScan();
            return 0;
        case kCheatFinderIdEqual:
            cheatFinderFilter(CHEAT_FINDER_FILTER_EQUAL);
            return 0;
        case kCheatFinderIdIncreased:
            cheatFinderFilter(CHEAT_FINDER_FILTER_INCREASED);
            return 0;
        case kCheatFinderIdDecreased:
            cheatFinderFilter(CHEAT_FINDER_FILTER_DECREASED);
            return 0;
        case kCheatFinderIdUnchanged:
            cheatFinderFilter(CHEAT_FINDER_FILTER_UNCHANGED);
            return 0;
        case kCheatFinderIdPoke:
            cheatFinderPokeSelected();
            return 0;
        case kCheatFinderIdCopyCheat:
            cheatFinderCopyCheatLine();
            return 0;
        case kCheatFinderIdReset:
            cheatFinderReset();
            return 0;
        case kCheatFinderIdLockAddress:
            cheatFinderLockSelected();
            return 0;
        case kCheatFinderIdRefreshLocked:
            cheatFinderRefreshLocked();
            return 0;
        case kCheatFinderIdClearLocked:
            cheatFinderClearLocked();
            return 0;
        case kCheatFinderIdClose:
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
                if (header->idFrom == kCheatFinderIdList)
                {
                    g_cheatFinderSelectionSource = CHEAT_FINDER_SELECTION_CANDIDATE;
                }
                else if (header->idFrom == kCheatFinderIdLockedList)
                {
                    g_cheatFinderSelectionSource = CHEAT_FINDER_SELECTION_LOCKED;
                }
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        if (id == kCheatFinderIdStatus)
        {
            SetBkMode((HDC)wParam, OPAQUE);
            SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
            SetTextColor((HDC)wParam, GetSysColor(COLOR_BTNTEXT));
            return (LRESULT)(g_cheatFinderBackgroundBrush ?
                g_cheatFinderBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
        }
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)(g_cheatFinderBackgroundBrush ?
            g_cheatFinderBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_cheatFinderWindow == hwnd)
        {
            g_cheatFinderWindow = NULL;
            g_cheatFinderList = NULL;
            g_cheatFinderLockedList = NULL;
        }
        if (g_cheatFinderFont && g_cheatFinderOwnFont)
        {
            DeleteObject(g_cheatFinderFont);
        }
        g_cheatFinderFont = NULL;
        g_cheatFinderOwnFont = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensureCheatFinderWindowClass(void)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = cheatFinderWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DingooPieCheatFinderWindow";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = g_cheatFinderBackgroundBrush;
    RegisterClassW(&wc);
    registered = true;
}

void cheatFinderOpenWindow(HWND owner, UiLanguage language)
{
    (void)owner;
    g_cheatFinderLanguage = language;
    if (g_cheatFinderWindow)
    {
        ShowWindow(g_cheatFinderWindow, SW_SHOWNOACTIVATE);
        return;
    }

    INITCOMMONCONTROLSEX controls;
    memset(&controls, 0, sizeof(controls));
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    bool zh = cheatFinderChinese();
    g_cheatFinderBackgroundBrush = GetSysColorBrush(COLOR_BTNFACE);
    ensureCheatFinderWindowClass();

    g_cheatFinderWindow = CreateWindowExW(WS_EX_CONTROLPARENT,
        L"DingooPieCheatFinderWindow",
        cheatFinderTitle(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, kCheatFinderWindowWidth, kCheatFinderWindowHeight,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_cheatFinderWindow)
    {
        return;
    }

    applyCheatFinderIcon(g_cheatFinderWindow);
    createCheatFinderContents(zh);
    refreshCheatFinderResults();

    ShowWindow(g_cheatFinderWindow, SW_SHOWNOACTIVATE);
    UpdateWindow(g_cheatFinderWindow);
}

#endif

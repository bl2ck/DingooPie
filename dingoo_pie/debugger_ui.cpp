#include "debugger_ui.h"

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
#include <string>
#include <vector>
#include <wchar.h>
#include <windows.h>
#include <uxtheme.h>

static HWND g_debuggerWindow = NULL;
static HWND g_disasmList = NULL;
static HWND g_registerText = NULL;
static HWND g_memoryList = NULL;
static HWND g_breakpointList = NULL;
static HWND g_watchList = NULL;
static HBRUSH g_debuggerBackgroundBrush = NULL;
static HFONT g_debuggerFont = NULL;
static bool g_debuggerOwnFont = false;
static UiLanguage g_debuggerLanguage = UI_LANGUAGE_ENGLISH;

static const int kDebuggerWindowWidth = 980;
static const int kDebuggerWindowHeight = 760;
static const int kDebuggerMargin = 18;
static const int kDebuggerRightX = 944;
static const int kDebuggerButtonWidth = 64;
static const int kDebuggerButtonHeight = 28;
static const int kDebuggerButtonGap = 8;
static const int kDebuggerBottomButtonCount = 3;
static const int kDebuggerBottomSectionGap = 20;
static const int kDebuggerBottomButtonRowWidth =
    (kDebuggerButtonWidth * kDebuggerBottomButtonCount) +
    (kDebuggerButtonGap * (kDebuggerBottomButtonCount - 1));
static const int kDebuggerListColumnPadding = 4;
static const int kDebuggerBottomControlY = 526;
static const int kDebuggerBottomInputY = 528;
static const int kDebuggerBottomLabelY = 532;
static const int kDebuggerBottomListY = 560;
static const int kDebuggerBottomListHeight = 118;
static const int kDebuggerBreakpointX = kDebuggerMargin;
static const int kDebuggerBreakpointListWidth = 428;
static const int kDebuggerBreakpointRight = kDebuggerBreakpointX + kDebuggerBreakpointListWidth;
static const int kDebuggerWatchX = kDebuggerBreakpointRight + kDebuggerBottomSectionGap;
static const int kDebuggerWatchListWidth = kDebuggerRightX - kDebuggerWatchX;
static const int kDebuggerBreakpointButtonX =
    kDebuggerBreakpointRight - kDebuggerBottomButtonRowWidth;
static const int kDebuggerWatchButtonX = kDebuggerRightX - kDebuggerBottomButtonRowWidth;

static const int kDebuggerIdPcAddress = 44001;
static const int kDebuggerIdMemAddress = 44002;
static const int kDebuggerIdMemSize = 44003;
static const int kDebuggerIdWatchAddress = 44004;
static const int kDebuggerIdWatchSize = 44005;
static const int kDebuggerIdBreakpointAddress = 44006;
static const int kDebuggerIdRefresh = 44007;
static const int kDebuggerIdAddBreakpoint = 44008;
static const int kDebuggerIdRemoveBreakpoint = 44009;
static const int kDebuggerIdAddWatch = 44010;
static const int kDebuggerIdRemoveWatch = 44011;
static const int kDebuggerIdClearBreakpoints = 44012;
static const int kDebuggerIdClearWatches = 44013;
static const int kDebuggerIdClose = 44014;
static const int kDebuggerIdStatus = 44015;
static const int kDebuggerIdDisasm = 44016;
static const int kDebuggerIdRegisters = 44017;
static const int kDebuggerIdMemory = 44018;
static const int kDebuggerIdBreakpoints = 44019;
static const int kDebuggerIdWatches = 44020;

static bool debuggerChinese(void)
{
    return g_debuggerLanguage == UI_LANGUAGE_CHINESE;
}

static const wchar_t* debuggerTitle(void)
{
    return debuggerChinese() ? L"\u8c03\u8bd5\u5668" : L"Debugger";
}

static HICON loadDebuggerIcon(int size)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED);
}

static void applyDebuggerIcon(HWND window)
{
    if (!window)
    {
        return;
    }

    HICON largeIcon = loadDebuggerIcon(32);
    HICON smallIcon = loadDebuggerIcon(16);
    if (largeIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    }
    if (smallIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    }
}

static HFONT debuggerFont(void)
{
    if (g_debuggerFont)
    {
        return g_debuggerFont;
    }

    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0))
    {
        g_debuggerFont = CreateFontIndirectW(&metrics.lfMessageFont);
        g_debuggerOwnFont = g_debuggerFont != NULL;
    }
    if (!g_debuggerFont)
    {
        g_debuggerFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_debuggerOwnFont = false;
    }
    return g_debuggerFont;
}

static HWND debuggerItem(int id)
{
    return g_debuggerWindow ? GetDlgItem(g_debuggerWindow, id) : NULL;
}

static HWND createDebuggerChild(HWND parent, const wchar_t* className, const wchar_t* text,
    DWORD exStyle, DWORD style, int x, int y, int w, int h, int id)
{
    HWND child = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (child)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)debuggerFont(), TRUE);
        SetWindowTheme(child, L"Explorer", NULL);
    }
    return child;
}

static HWND createDebuggerLabel(const wchar_t* text, int x, int y, int w, int h)
{
    return createDebuggerChild(g_debuggerWindow, L"STATIC", text, 0, 0, x, y, w, h, -1);
}

static HWND createDebuggerButton(const wchar_t* text, int x, int y, int w, int h, int id)
{
    return createDebuggerChild(g_debuggerWindow, L"BUTTON", text, 0, 0, x, y, w, h, id);
}

struct DebuggerButtonSpec
{
    const wchar_t* text;
    int id;
};

struct DebuggerListColumnSpec
{
    const wchar_t* text;
    int weight;
};

static void createDebuggerButtonRow(int x, int y,
    const DebuggerButtonSpec* buttons, size_t buttonCount)
{
    for (size_t i = 0; i < buttonCount; ++i)
    {
        createDebuggerButton(buttons[i].text, x, y,
            kDebuggerButtonWidth, kDebuggerButtonHeight, buttons[i].id);
        x += kDebuggerButtonWidth + kDebuggerButtonGap;
    }
}

static bool debuggerWindowTextControl(int id)
{
    return id == kDebuggerIdRegisters;
}

// Dynamic read-only text controls need an explicit repaint after replacement.
static void setDebuggerText(HWND control, const wchar_t* text)
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

static void clearDebuggerList(HWND list)
{
    if (!list)
    {
        return;
    }

    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
}

static void finishDebuggerListRefresh(HWND list)
{
    if (!list)
    {
        return;
    }

    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, NULL, TRUE);
}

static void setDebuggerStatus(const wchar_t* text)
{
    setDebuggerText(debuggerItem(kDebuggerIdStatus), text);
}

static bool parseHexEdit(int id, uint32_t* out)
{
    if (!out)
    {
        return false;
    }
    wchar_t text[64] = {};
    GetWindowTextW(debuggerItem(id), text, (int)(sizeof(text) / sizeof(text[0])));
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
    unsigned long value = wcstoul(p, &end, 0);
    while (end && (*end == L' ' || *end == L'\t'))
    {
        end++;
    }
    if (!end || *end)
    {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static void setHexEdit(int id, uint32_t value)
{
    wchar_t text[32] = {};
    swprintf(text, sizeof(text) / sizeof(text[0]), L"0x%08X", value);
    SetWindowTextW(debuggerItem(id), text);
}

static std::wstring utf8ToWide(const std::string& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (needed <= 0)
    {
        std::wstring fallback;
        fallback.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            fallback.push_back((wchar_t)(unsigned char)text[i]);
        }
        return fallback;
    }
    std::wstring out;
    out.resize((size_t)needed - 1);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], needed);
    return out;
}

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

static void addListColumn(HWND list, int index, const wchar_t* text, int width)
{
    LVCOLUMNW column;
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = (LPWSTR)text;
    column.cx = width;
    column.iSubItem = index;
    SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&column);
}

static void setupList(HWND list)
{
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
}

static int debuggerListAvailableWidth(HWND list)
{
    RECT rect;
    memset(&rect, 0, sizeof(rect));
    if (!list || !GetClientRect(list, &rect))
    {
        return 0;
    }

    int width = rect.right - rect.left -
        GetSystemMetrics(SM_CXVSCROLL) - kDebuggerListColumnPadding;
    return width > 0 ? width : 0;
}

static void setupFittedListColumns(HWND list,
    const DebuggerListColumnSpec* columns, size_t columnCount)
{
    setupList(list);
    if (!list || !columns || columnCount == 0)
    {
        return;
    }

    int totalWeight = 0;
    for (size_t i = 0; i < columnCount; ++i)
    {
        totalWeight += columns[i].weight > 0 ? columns[i].weight : 1;
    }

    int availableWidth = debuggerListAvailableWidth(list);
    if (availableWidth <= 0)
    {
        availableWidth = totalWeight;
    }

    int usedWidth = 0;
    for (size_t i = 0; i < columnCount; ++i)
    {
        int width = 0;
        if (i + 1 == columnCount)
        {
            width = availableWidth - usedWidth;
        }
        else
        {
            int weight = columns[i].weight > 0 ? columns[i].weight : 1;
            width = (availableWidth * weight) / totalWeight;
        }
        if (width < 1)
        {
            width = 1;
        }
        if (i + 1 != columnCount)
        {
            usedWidth += width;
        }
        addListColumn(list, (int)i, columns[i].text, width);
    }
}

static void setupDisasmList(HWND list)
{
    DebuggerListColumnSpec columns[] = {
        { debuggerChinese() ? L"\u5730\u5740" : L"Address", 112 },
        { debuggerChinese() ? L"\u673a\u5668\u7801" : L"Encoding", 112 },
        { debuggerChinese() ? L"\u6307\u4ee4" : L"Instruction", 302 }
    };
    setupFittedListColumns(list, columns, sizeof(columns) / sizeof(columns[0]));
}

static void setupMemoryList(HWND list)
{
    DebuggerListColumnSpec columns[] = {
        { debuggerChinese() ? L"\u5730\u5740" : L"Address", 112 },
        { L"+0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F", 640 },
        { L"ASCII", 170 }
    };
    setupFittedListColumns(list, columns, sizeof(columns) / sizeof(columns[0]));
}

static void setupBreakpointList(HWND list)
{
    DebuggerListColumnSpec columns[] = {
        { debuggerChinese() ? L"\u5730\u5740" : L"Address", 146 },
        { debuggerChinese() ? L"\u547d\u4e2d" : L"Hits", 80 },
        { L"Last PC", 198 }
    };
    setupFittedListColumns(list, columns, sizeof(columns) / sizeof(columns[0]));
}

static void setupWatchList(HWND list)
{
    DebuggerListColumnSpec columns[] = {
        { debuggerChinese() ? L"\u8303\u56f4" : L"Range", 156 },
        { debuggerChinese() ? L"\u547d\u4e2d" : L"Hits", 62 },
        { L"Last PC", 104 },
        { debuggerChinese() ? L"\u5199\u5165" : L"Write", 152 }
    };
    setupFittedListColumns(list, columns, sizeof(columns) / sizeof(columns[0]));
}

static int selectedListAddress(HWND list)
{
    if (!list)
    {
        return -1;
    }
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    return row;
}

static void refreshRegisters(const EmulatorRuntimeRegisterSnapshot& snapshot)
{
    static const wchar_t* kNames[32] = {
        L"zero", L"at", L"v0", L"v1", L"a0", L"a1", L"a2", L"a3",
        L"t0", L"t1", L"t2", L"t3", L"t4", L"t5", L"t6", L"t7",
        L"s0", L"s1", L"s2", L"s3", L"s4", L"s5", L"s6", L"s7",
        L"t8", L"t9", L"k0", L"k1", L"gp", L"sp", L"fp", L"ra"
    };

    std::wstring text;
    wchar_t line[96] = {};
    swprintf(line, sizeof(line) / sizeof(line[0]), L"pc  = %08X\r\nhi  = %08X    lo  = %08X\r\n\r\n",
        snapshot.pc, snapshot.hi, snapshot.lo);
    text += line;
    for (int i = 0; i < 32; i += 2)
    {
        swprintf(line, sizeof(line) / sizeof(line[0]), L"%-4ls= %08X    %-4ls= %08X\r\n",
            kNames[i], snapshot.gpr[i], kNames[i + 1], snapshot.gpr[i + 1]);
        text += line;
    }
    setDebuggerText(g_registerText, text.c_str());
}

static void refreshDisassembly(uint32_t address)
{
    if (!g_disasmList)
    {
        return;
    }
    std::vector<EmulatorRuntimeDisassemblyLine> lines;
    clearDebuggerList(g_disasmList);
    if (!emulatorRuntimeDisassemble(address & ~3u, 36, &lines))
    {
        finishDebuggerListRefresh(g_disasmList);
        return;
    }

    for (size_t i = 0; i < lines.size(); ++i)
    {
        wchar_t addressText[32] = {};
        wchar_t encodingText[32] = {};
        swprintf(addressText, sizeof(addressText) / sizeof(addressText[0]), L"%08X", lines[i].address);
        swprintf(encodingText, sizeof(encodingText) / sizeof(encodingText[0]),
            lines[i].valid ? L"%08X" : L"--------", lines[i].encoding);

        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        item.iSubItem = 0;
        item.pszText = addressText;
        SendMessageW(g_disasmList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        setListViewText(g_disasmList, (int)i, 1, encodingText);
        std::wstring text = utf8ToWide(lines[i].text);
        setListViewText(g_disasmList, (int)i, 2, text.c_str());
    }
    finishDebuggerListRefresh(g_disasmList);
}

static void refreshMemory(uint32_t address, uint32_t bytes)
{
    if (!g_memoryList)
    {
        return;
    }
    if (bytes == 0)
    {
        bytes = 0x100;
    }
    if (bytes > 0x1000)
    {
        bytes = 0x1000;
    }
    address &= ~0xfu;
    clearDebuggerList(g_memoryList);

    uint8_t buffer[16];
    uint32_t rows = (bytes + 15u) / 16u;
    for (uint32_t row = 0; row < rows; ++row)
    {
        uint32_t rowAddress = address + row * 16u;
        memset(buffer, 0, sizeof(buffer));
        bool ok = emulatorRuntimeReadMemory(rowAddress, buffer, sizeof(buffer));

        wchar_t addressText[32] = {};
        wchar_t hexText[96] = {};
        wchar_t asciiText[32] = {};
        swprintf(addressText, sizeof(addressText) / sizeof(addressText[0]), L"%08X", rowAddress);
        if (ok)
        {
            wchar_t* hexOut = hexText;
            for (int i = 0; i < 16; ++i)
            {
                swprintf(hexOut, 4, L"%02X ", buffer[i]);
                hexOut += 3;
                asciiText[i] = (buffer[i] >= 32 && buffer[i] < 127) ? (wchar_t)buffer[i] : L'.';
            }
            asciiText[16] = 0;
        }
        else
        {
            wcscpy(hexText, L"-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --");
            wcscpy(asciiText, L"unmapped");
        }

        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = (int)row;
        item.iSubItem = 0;
        item.pszText = addressText;
        SendMessageW(g_memoryList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        setListViewText(g_memoryList, (int)row, 1, hexText);
        setListViewText(g_memoryList, (int)row, 2, asciiText);
    }
    finishDebuggerListRefresh(g_memoryList);
}

static void refreshDebugEntryLists(void)
{
    if (g_breakpointList)
    {
        clearDebuggerList(g_breakpointList);
        std::vector<EmulatorRuntimeDebugEntry> breakpoints = emulatorRuntimeBreakpoints();
        for (size_t i = 0; i < breakpoints.size(); ++i)
        {
            wchar_t addressText[32] = {};
            wchar_t hitsText[32] = {};
            wchar_t pcText[32] = {};
            swprintf(addressText, sizeof(addressText) / sizeof(addressText[0]), L"%08X", breakpoints[i].address);
            swprintf(hitsText, sizeof(hitsText) / sizeof(hitsText[0]), L"%llu", (unsigned long long)breakpoints[i].hits);
            swprintf(pcText, sizeof(pcText) / sizeof(pcText[0]), L"%08X", breakpoints[i].lastPc);

            LVITEMW item;
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT;
            item.iItem = (int)i;
            item.iSubItem = 0;
            item.pszText = addressText;
            SendMessageW(g_breakpointList, LVM_INSERTITEMW, 0, (LPARAM)&item);
            setListViewText(g_breakpointList, (int)i, 1, hitsText);
            setListViewText(g_breakpointList, (int)i, 2, pcText);
        }
        finishDebuggerListRefresh(g_breakpointList);
    }

    if (g_watchList)
    {
        clearDebuggerList(g_watchList);
        std::vector<EmulatorRuntimeDebugEntry> watches = emulatorRuntimeWriteWatches();
        for (size_t i = 0; i < watches.size(); ++i)
        {
            wchar_t rangeText[64] = {};
            wchar_t hitsText[32] = {};
            wchar_t pcText[32] = {};
            wchar_t writeText[96] = {};
            swprintf(rangeText, sizeof(rangeText) / sizeof(rangeText[0]), L"%08X +%u",
                watches[i].address, watches[i].size);
            swprintf(hitsText, sizeof(hitsText) / sizeof(hitsText[0]), L"%llu", (unsigned long long)watches[i].hits);
            swprintf(pcText, sizeof(pcText) / sizeof(pcText[0]), L"%08X", watches[i].lastPc);
            swprintf(writeText, sizeof(writeText) / sizeof(writeText[0]), L"%08X/%u = %llX",
                watches[i].lastAddress, watches[i].lastSize, (unsigned long long)watches[i].lastValue);

            LVITEMW item;
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT;
            item.iItem = (int)i;
            item.iSubItem = 0;
            item.pszText = rangeText;
            SendMessageW(g_watchList, LVM_INSERTITEMW, 0, (LPARAM)&item);
            setListViewText(g_watchList, (int)i, 1, hitsText);
            setListViewText(g_watchList, (int)i, 2, pcText);
            setListViewText(g_watchList, (int)i, 3, writeText);
        }
        finishDebuggerListRefresh(g_watchList);
    }
}

static void refreshDebugger(void)
{
    EmulatorRuntimeRegisterSnapshot snapshot;
    if (!emulatorRuntimeGetRegisterSnapshot(&snapshot))
    {
        setDebuggerText(g_registerText, debuggerChinese() ?
            L"\u6ca1\u6709\u8fd0\u884c\u4e2d\u7684\u6e38\u620f\u3002" :
            L"No running game.");
        clearDebuggerList(g_disasmList);
        finishDebuggerListRefresh(g_disasmList);
        clearDebuggerList(g_memoryList);
        finishDebuggerListRefresh(g_memoryList);
        refreshDebugEntryLists();
        setDebuggerStatus(debuggerChinese() ?
            L"\u8bf7\u5148\u542f\u52a8\u6e38\u620f\u3002" :
            L"Start a game first.");
        return;
    }

    refreshRegisters(snapshot);

    uint32_t pcAddress = 0;
    if (!parseHexEdit(kDebuggerIdPcAddress, &pcAddress))
    {
        pcAddress = snapshot.pc;
        setHexEdit(kDebuggerIdPcAddress, pcAddress);
    }
    uint32_t memAddress = 0;
    if (!parseHexEdit(kDebuggerIdMemAddress, &memAddress))
    {
        memAddress = snapshot.gpr[29];
        setHexEdit(kDebuggerIdMemAddress, memAddress);
    }
    uint32_t memSize = 0;
    if (!parseHexEdit(kDebuggerIdMemSize, &memSize))
    {
        memSize = 0x100;
        setHexEdit(kDebuggerIdMemSize, memSize);
    }

    refreshDisassembly(pcAddress);
    refreshMemory(memAddress, memSize);
    refreshDebugEntryLists();
    setDebuggerStatus(debuggerChinese() ?
        L"\u5df2\u5237\u65b0\u3002\u65ad\u70b9\u548c\u5199\u5165\u76d1\u89c6\u4f1a\u8bb0\u5f55\u547d\u4e2d\uff0c\u4e0d\u4f1a\u6682\u505c CPU\u3002" :
        L"Refreshed. Breakpoints and write watches record hits without pausing the CPU.");
}

static void addBreakpointFromInput(void)
{
    uint32_t address = 0;
    if (!parseHexEdit(kDebuggerIdBreakpointAddress, &address))
    {
        setDebuggerStatus(debuggerChinese() ? L"\u65ad\u70b9\u5730\u5740\u65e0\u6548\u3002" : L"Invalid breakpoint address.");
        return;
    }
    emulatorRuntimeAddBreakpoint(address);
    refreshDebugEntryLists();
}

static void removeSelectedBreakpoint(void)
{
    std::vector<EmulatorRuntimeDebugEntry> breakpoints = emulatorRuntimeBreakpoints();
    int row = selectedListAddress(g_breakpointList);
    if (row < 0 || (size_t)row >= breakpoints.size())
    {
        return;
    }
    emulatorRuntimeRemoveBreakpoint(breakpoints[(size_t)row].address);
    refreshDebugEntryLists();
}

static void addWatchFromInput(void)
{
    uint32_t address = 0;
    uint32_t size = 0;
    if (!parseHexEdit(kDebuggerIdWatchAddress, &address) ||
        !parseHexEdit(kDebuggerIdWatchSize, &size) ||
        size == 0)
    {
        setDebuggerStatus(debuggerChinese() ? L"\u5199\u5165\u76d1\u89c6\u8303\u56f4\u65e0\u6548\u3002" : L"Invalid write watch range.");
        return;
    }
    if (size > 0x10000)
    {
        size = 0x10000;
        setHexEdit(kDebuggerIdWatchSize, size);
    }
    if (!emulatorRuntimeAddWriteWatch(address, size))
    {
        setDebuggerStatus(debuggerChinese() ? L"\u5199\u5165\u76d1\u89c6\u8303\u56f4\u8d85\u51fa\u5730\u5740\u7a7a\u95f4\u3002" : L"Write watch range is outside address space.");
        return;
    }
    refreshDebugEntryLists();
}

static void removeSelectedWatch(void)
{
    std::vector<EmulatorRuntimeDebugEntry> watches = emulatorRuntimeWriteWatches();
    int row = selectedListAddress(g_watchList);
    if (row < 0 || (size_t)row >= watches.size())
    {
        return;
    }
    emulatorRuntimeRemoveWriteWatch(watches[(size_t)row].address, watches[(size_t)row].size);
    refreshDebugEntryLists();
}

static void createDebuggerToolbar(bool zh)
{
    createDebuggerLabel(zh ? L"PC \u9644\u8fd1" : L"PC view",
        kDebuggerMargin, 16, 70, 22);
    createDebuggerChild(g_debuggerWindow, L"EDIT", L"",
        WS_EX_CLIENTEDGE, ES_AUTOHSCROLL,
        92, 12, 118, 24, kDebuggerIdPcAddress);
    createDebuggerLabel(zh ? L"\u5185\u5b58" : L"Memory",
        228, 16, 60, 22);
    createDebuggerChild(g_debuggerWindow, L"EDIT", L"",
        WS_EX_CLIENTEDGE, ES_AUTOHSCROLL,
        288, 12, 118, 24, kDebuggerIdMemAddress);
    createDebuggerLabel(zh ? L"\u957f\u5ea6" : L"Bytes",
        424, 16, 48, 22);
    createDebuggerChild(g_debuggerWindow, L"EDIT", L"0x100",
        WS_EX_CLIENTEDGE, ES_AUTOHSCROLL,
        472, 12, 82, 24, kDebuggerIdMemSize);
    createDebuggerButton(zh ? L"\u5237\u65b0" : L"Refresh",
        572, 10, 82, kDebuggerButtonHeight, kDebuggerIdRefresh);
    createDebuggerButton(zh ? L"\u5173\u95ed" : L"Close",
        870, 10, 74, kDebuggerButtonHeight, kDebuggerIdClose);
}

static void createDebuggerMainViews(bool zh)
{
    createDebuggerLabel(zh ? L"\u53cd\u6c47\u7f16" : L"Disassembly",
        kDebuggerMargin, 52, 120, 22);
    g_disasmList = createDebuggerChild(g_debuggerWindow, WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kDebuggerMargin, 76, 526, 244, kDebuggerIdDisasm);
    setupDisasmList(g_disasmList);

    createDebuggerLabel(zh ? L"\u5bc4\u5b58\u5668" : L"Registers",
        562, 52, 120, 22);
    g_registerText = createDebuggerChild(g_debuggerWindow, L"EDIT", L"",
        WS_EX_CLIENTEDGE, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        562, 76, 382, 244, kDebuggerIdRegisters);

    createDebuggerLabel(zh ? L"\u5185\u5b58\u67e5\u770b" : L"Memory Viewer",
        kDebuggerMargin, 334, 130, 22);
    g_memoryList = createDebuggerChild(g_debuggerWindow, WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kDebuggerMargin, 358, 926, 160, kDebuggerIdMemory);
    setupMemoryList(g_memoryList);
}

static void createDebuggerBreakpoints(bool zh)
{
    createDebuggerLabel(zh ? L"\u65ad\u70b9" : L"Breakpoints",
        kDebuggerMargin, kDebuggerBottomLabelY, 90, 22);
    createDebuggerChild(g_debuggerWindow, L"EDIT", L"",
        WS_EX_CLIENTEDGE, ES_AUTOHSCROLL,
        112, kDebuggerBottomInputY, 118, 24, kDebuggerIdBreakpointAddress);
    DebuggerButtonSpec buttons[] = {
        { zh ? L"\u6dfb\u52a0" : L"Add", kDebuggerIdAddBreakpoint },
        { zh ? L"\u5220\u9664" : L"Remove", kDebuggerIdRemoveBreakpoint },
        { zh ? L"\u6e05\u7a7a" : L"Clear", kDebuggerIdClearBreakpoints }
    };
    createDebuggerButtonRow(kDebuggerBreakpointButtonX, kDebuggerBottomControlY,
        buttons, sizeof(buttons) / sizeof(buttons[0]));
    g_breakpointList = createDebuggerChild(g_debuggerWindow, WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kDebuggerBreakpointX, kDebuggerBottomListY,
        kDebuggerBreakpointListWidth, kDebuggerBottomListHeight, kDebuggerIdBreakpoints);
    setupBreakpointList(g_breakpointList);
}

static void createDebuggerWatches(bool zh)
{
    createDebuggerLabel(zh ? L"\u5199\u5165\u76d1\u89c6" : L"Write Watch",
        kDebuggerWatchX, kDebuggerBottomLabelY, 92, 22);
    createDebuggerChild(g_debuggerWindow, L"EDIT", L"",
        WS_EX_CLIENTEDGE, ES_AUTOHSCROLL,
        560, kDebuggerBottomInputY, 94, 24, kDebuggerIdWatchAddress);
    createDebuggerChild(g_debuggerWindow, L"EDIT", L"4",
        WS_EX_CLIENTEDGE, ES_AUTOHSCROLL,
        660, kDebuggerBottomInputY, 36, 24, kDebuggerIdWatchSize);
    DebuggerButtonSpec buttons[] = {
        { zh ? L"\u6dfb\u52a0" : L"Add", kDebuggerIdAddWatch },
        { zh ? L"\u5220\u9664" : L"Remove", kDebuggerIdRemoveWatch },
        { zh ? L"\u6e05\u7a7a" : L"Clear", kDebuggerIdClearWatches }
    };
    createDebuggerButtonRow(kDebuggerWatchButtonX, kDebuggerBottomControlY,
        buttons, sizeof(buttons) / sizeof(buttons[0]));
    g_watchList = createDebuggerChild(g_debuggerWindow, WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kDebuggerWatchX, kDebuggerBottomListY,
        kDebuggerWatchListWidth, kDebuggerBottomListHeight, kDebuggerIdWatches);
    setupWatchList(g_watchList);
}

static void createDebuggerContents(bool zh)
{
    createDebuggerToolbar(zh);
    createDebuggerMainViews(zh);
    createDebuggerBreakpoints(zh);
    createDebuggerWatches(zh);
    createDebuggerChild(g_debuggerWindow, L"STATIC", L"",
        0, 0, kDebuggerMargin, 690, kDebuggerRightX - kDebuggerMargin, 22, kDebuggerIdStatus);
}

static LRESULT CALLBACK debuggerWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        switch (id)
        {
        case kDebuggerIdRefresh:
            refreshDebugger();
            return 0;
        case kDebuggerIdAddBreakpoint:
            addBreakpointFromInput();
            return 0;
        case kDebuggerIdRemoveBreakpoint:
            removeSelectedBreakpoint();
            return 0;
        case kDebuggerIdClearBreakpoints:
            emulatorRuntimeClearBreakpoints();
            refreshDebugEntryLists();
            return 0;
        case kDebuggerIdAddWatch:
            addWatchFromInput();
            return 0;
        case kDebuggerIdRemoveWatch:
            removeSelectedWatch();
            return 0;
        case kDebuggerIdClearWatches:
            emulatorRuntimeClearWriteWatches();
            refreshDebugEntryLists();
            return 0;
        case kDebuggerIdClose:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_TIMER:
        if (wParam == 1)
        {
            refreshDebugEntryLists();
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        if (debuggerWindowTextControl(id))
        {
            SetBkMode((HDC)wParam, OPAQUE);
            SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
            SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        if (id == kDebuggerIdStatus)
        {
            SetBkMode((HDC)wParam, TRANSPARENT);
            SetTextColor((HDC)wParam, GetSysColor(COLOR_BTNTEXT));
            return (LRESULT)(g_debuggerBackgroundBrush ?
                g_debuggerBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
        }
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)(g_debuggerBackgroundBrush ?
            g_debuggerBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_debuggerWindow == hwnd)
        {
            g_debuggerWindow = NULL;
            g_disasmList = NULL;
            g_registerText = NULL;
            g_memoryList = NULL;
            g_breakpointList = NULL;
            g_watchList = NULL;
        }
        if (g_debuggerFont && g_debuggerOwnFont)
        {
            DeleteObject(g_debuggerFont);
        }
        g_debuggerFont = NULL;
        g_debuggerOwnFont = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensureDebuggerWindowClass(void)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = debuggerWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DingooPieDebuggerWindow";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = loadDebuggerIcon(32);
    wc.hbrBackground = g_debuggerBackgroundBrush;
    RegisterClassW(&wc);
    registered = true;
}

void debuggerUiOpenWindow(HWND owner, UiLanguage language)
{
    (void)owner;
    g_debuggerLanguage = language;
    if (g_debuggerWindow)
    {
        ShowWindow(g_debuggerWindow, SW_SHOWNOACTIVATE);
        refreshDebugger();
        return;
    }

    INITCOMMONCONTROLSEX controls;
    memset(&controls, 0, sizeof(controls));
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    bool zh = debuggerChinese();
    g_debuggerBackgroundBrush = GetSysColorBrush(COLOR_BTNFACE);
    ensureDebuggerWindowClass();

    g_debuggerWindow = CreateWindowExW(WS_EX_CONTROLPARENT,
        L"DingooPieDebuggerWindow",
        debuggerTitle(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, kDebuggerWindowWidth, kDebuggerWindowHeight,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_debuggerWindow)
    {
        return;
    }

    applyDebuggerIcon(g_debuggerWindow);
    createDebuggerContents(zh);
    refreshDebugger();
    SetTimer(g_debuggerWindow, 1, 1000, NULL);

    ShowWindow(g_debuggerWindow, SW_SHOWNOACTIVATE);
    UpdateWindow(g_debuggerWindow);
}

#endif

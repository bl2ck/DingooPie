#include "cheat_manager_ui.h"

#ifdef _WIN32

#include "cheat_runtime.h"
#include "emulator_settings.h"
#include "frontend_menu.h"
#include "platform_win32.h"
#include "resource_ids.h"
#include "ui_strings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <wchar.h>
#include <windows.h>

static HWND g_cheatManagerWindow = NULL;
static HWND g_cheatManagerList = NULL;
static HWND g_cheatManagerStatus = NULL;
static HWND g_cheatManagerFile = NULL;
static HWND g_cheatManagerEnable = NULL;
static HBRUSH g_cheatManagerBackgroundBrush = NULL;
static HFONT g_cheatManagerFont = NULL;
static bool g_cheatManagerOwnFont = false;
static bool g_cheatManagerRefreshing = false;
static UiLanguage g_cheatManagerLanguage = UI_LANGUAGE_ENGLISH;
static EmulatorSettings* g_cheatManagerSettings = NULL;
static std::string g_cheatManagerAppPath;

static const int kCheatManagerWindowWidth = 660;
static const int kCheatManagerWindowHeight = 464;
static const int kCheatManagerMargin = 18;
static const int kCheatManagerRightX = 624;
static const int kCheatManagerTopY = 10;
static const int kCheatManagerFileHeight = 22;
static const int kCheatManagerFileToEnableGap = 2;
static const int kCheatManagerEnableX = 18;
static const int kCheatManagerEnableHeight = 24;
static const int kCheatManagerEnableY =
    kCheatManagerTopY + kCheatManagerFileHeight + kCheatManagerFileToEnableGap;
static const int kCheatManagerEnableToListGap = 8;
static const int kCheatManagerListY =
    kCheatManagerEnableY + kCheatManagerEnableHeight + kCheatManagerEnableToListGap;
static const int kCheatManagerListWidth = kCheatManagerRightX - kCheatManagerMargin;
static const int kCheatManagerListHeight = 286;
static const int kCheatManagerListToStatusGap = 6;
static const int kCheatManagerStatusHeight = 24;
static const int kCheatManagerStatusToButtonGap = 6;
static const int kCheatManagerStatusY =
    kCheatManagerListY + kCheatManagerListHeight + kCheatManagerListToStatusGap;
static const int kCheatManagerButtonY =
    kCheatManagerStatusY + kCheatManagerStatusHeight + kCheatManagerStatusToButtonGap;
static const int kCheatManagerBottomButtonCount = 6;
static const int kCheatManagerBottomButtonWidth = 96;
static const int kCheatManagerBottomButtonGap =
    (kCheatManagerListWidth - kCheatManagerBottomButtonWidth * kCheatManagerBottomButtonCount) /
    (kCheatManagerBottomButtonCount - 1);
static const int kCheatManagerCloseButtonWidth = 74;
static const int kCheatManagerButtonHeight = 28;
static const int kCheatManagerHeaderButtonGap = 10;
static const int kCheatManagerFileTextWidth =
    kCheatManagerListWidth - kCheatManagerCloseButtonWidth - kCheatManagerHeaderButtonGap;

static const int kCheatManagerIdList = 45001;
static const int kCheatManagerIdStatus = 45002;
static const int kCheatManagerIdFile = 45003;
static const int kCheatManagerIdEnable = 45004;
static const int kCheatManagerIdEnableSelected = 45005;
static const int kCheatManagerIdDisableSelected = 45006;
static const int kCheatManagerIdEnableAll = 45007;
static const int kCheatManagerIdDisableAll = 45008;
static const int kCheatManagerIdApply = 45009;
static const int kCheatManagerIdRefresh = 45010;
static const int kCheatManagerIdClose = 45011;

static bool cheatManagerChinese(void)
{
    return g_cheatManagerLanguage == UI_LANGUAGE_CHINESE;
}

static const wchar_t* cheatManagerTitle(void)
{
    return cheatManagerChinese() ? L"\u91d1\u624b\u6307\u7ba1\u7406\u5668" : L"Cheat Manager";
}

static HICON loadCheatManagerIcon(int size)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED);
}

static void applyCheatManagerIcon(HWND window)
{
    if (!window)
    {
        return;
    }
    HICON largeIcon = loadCheatManagerIcon(32);
    HICON smallIcon = loadCheatManagerIcon(16);
    if (largeIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    }
    if (smallIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    }
}

static HFONT cheatManagerFont(void)
{
    if (g_cheatManagerFont)
    {
        return g_cheatManagerFont;
    }

    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        g_cheatManagerFont = CreateFontIndirectW(&metrics.lfMessageFont);
        g_cheatManagerOwnFont = g_cheatManagerFont != NULL;
    }
    if (!g_cheatManagerFont)
    {
        g_cheatManagerFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_cheatManagerOwnFont = false;
    }
    return g_cheatManagerFont;
}

static HWND cheatManagerItem(int id)
{
    return g_cheatManagerWindow ? GetDlgItem(g_cheatManagerWindow, id) : NULL;
}

static HWND createCheatManagerChild(const wchar_t* className, const wchar_t* text,
    DWORD exStyle, DWORD style, int x, int y, int w, int h, int id)
{
    DWORD childStyle = (DWORD)platformWin32NormalizeChildStyle(className, style);
    HWND child = CreateWindowExW(exStyle, className, text,
        WS_CHILD | WS_VISIBLE | childStyle,
        x, y, w, h, g_cheatManagerWindow, (HMENU)(INT_PTR)id,
        GetModuleHandleW(NULL), NULL);
    if (child)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)cheatManagerFont(), TRUE);
    }
    return child;
}

static HWND createCheatManagerButton(const wchar_t* text, int x, int y, int w, int id)
{
    return createCheatManagerChild(L"BUTTON", text, 0, 0,
        x, y, w, kCheatManagerButtonHeight, id);
}

static void setCheatManagerText(HWND item, const wchar_t* text)
{
    if (!item)
    {
        return;
    }
    SetWindowTextW(item, text ? text : L"");
    InvalidateRect(item, NULL, TRUE);
}

static std::wstring cheatManagerDisplayName(const CheatRuntimeEntryView& entry)
{
    std::string text;
    if (!entry.nameChinese.empty() && !entry.nameEnglish.empty() &&
        entry.nameChinese != entry.nameEnglish)
    {
        text = entry.nameChinese + " / " + entry.nameEnglish;
    }
    else if (!entry.name.empty())
    {
        text = entry.name;
    }
    else if (!entry.nameChinese.empty())
    {
        text = entry.nameChinese;
    }
    else
    {
        text = entry.nameEnglish;
    }
    std::wstring wide = platformUtf8ToWide(text);
    return wide.empty() ? std::wstring(L"(unnamed)") : wide;
}

static std::vector<std::string> cheatManagerEnabledFeatureKeys(const CheatRuntimeStatus& status)
{
    std::vector<std::string> keys;
    for (size_t i = 0; i < status.entries.size(); ++i)
    {
        if (status.entries[i].enabled && !status.entries[i].name.empty())
        {
            keys.push_back(status.entries[i].name);
        }
    }
    return keys;
}

static void saveCheatManagerSelection(const CheatRuntimeStatus& status)
{
    if (!g_cheatManagerSettings)
    {
        return;
    }
    bool changed = emulatorSetCheatFeatureKeysForApp(g_cheatManagerSettings,
        g_cheatManagerAppPath, cheatManagerEnabledFeatureKeys(status));
    if (changed)
    {
        emulatorSaveSettings(*g_cheatManagerSettings);
    }
}

static void setCheatManagerGlobalEnabled(bool enabled)
{
    if (g_cheatManagerSettings)
    {
        if (g_cheatManagerSettings->cheatsEnabled != enabled)
        {
            g_cheatManagerSettings->cheatsEnabled = enabled;
            emulatorApplySettingsToEnvironment(*g_cheatManagerSettings);
            emulatorSaveSettings(*g_cheatManagerSettings);
        }
    }
    cheatRuntimeSetEnabled(enabled);
    if (enabled)
    {
        cheatRuntimeApplyNow();
    }
    frontendMenuRefresh();
}

static void refreshCheatManagerWindow(void)
{
    if (!g_cheatManagerWindow)
    {
        return;
    }

    CheatRuntimeStatus status = cheatRuntimeGetStatus();
    g_cheatManagerRefreshing = true;
    SendMessageW(g_cheatManagerEnable, BM_SETCHECK,
        status.enabled ? BST_CHECKED : BST_UNCHECKED, 0);

    ListView_DeleteAllItems(g_cheatManagerList);
    for (size_t i = 0; i < status.entries.size(); ++i)
    {
        std::wstring name = cheatManagerDisplayName(status.entries[i]);
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        item.iSubItem = 0;
        item.pszText = (LPWSTR)name.c_str();
        SendMessageW(g_cheatManagerList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        ListView_SetCheckState(g_cheatManagerList, (int)i, status.entries[i].enabled ? TRUE : FALSE);
    }

    std::wstring fileText = cheatManagerChinese() ? L"\u6587\u4ef6: " : L"File: ";
    fileText += status.sourcePath.empty() ?
        (cheatManagerChinese() ? L"\u672a\u52a0\u8f7d" : L"(not loaded)") :
        platformUtf8ToWide(status.sourcePath);
    setCheatManagerText(g_cheatManagerFile, fileText.c_str());

    wchar_t statusText[256] = {};
    if (!status.loaded)
    {
        swprintf(statusText, sizeof(statusText) / sizeof(statusText[0]),
            cheatManagerChinese() ? L"\u65e0\u6e38\u620f\u540c\u540d .cht \u6587\u4ef6\u3002" :
            L"No same-name .cht file for the current game.");
    }
    else if (status.shaMismatch)
    {
        swprintf(statusText, sizeof(statusText) / sizeof(statusText[0]),
            cheatManagerChinese() ? L"\u91d1\u624b\u6307\u6587\u4ef6\u4e0d\u5c5e\u4e8e\u5f53\u524d\u6e38\u620f\u3002" :
            L"Cheat file does not match the current game.");
    }
    else
    {
        unsigned enabledCount = 0;
        for (size_t i = 0; i < status.entries.size(); ++i)
        {
            if (status.entries[i].enabled)
            {
                enabledCount++;
            }
        }
        swprintf(statusText, sizeof(statusText) / sizeof(statusText[0]),
            cheatManagerChinese() ? L"\u529f\u80fd: %u, \u5df2\u542f\u7528: %u, \u603b\u5f00\u5173: %ls" :
            L"Features: %u, enabled: %u, global switch: %ls",
            (unsigned)status.entries.size(), enabledCount,
            status.enabled ? (cheatManagerChinese() ? L"\u5f00" : L"on") :
                (cheatManagerChinese() ? L"\u5173" : L"off"));
    }
    setCheatManagerText(g_cheatManagerStatus, statusText);

    bool canEdit = status.available && !status.entries.empty();
    EnableWindow(cheatManagerItem(kCheatManagerIdEnableSelected), canEdit);
    EnableWindow(cheatManagerItem(kCheatManagerIdDisableSelected), canEdit);
    EnableWindow(cheatManagerItem(kCheatManagerIdEnableAll), canEdit);
    EnableWindow(cheatManagerItem(kCheatManagerIdDisableAll), canEdit);
    EnableWindow(cheatManagerItem(kCheatManagerIdApply), canEdit && status.enabled);
    g_cheatManagerRefreshing = false;
}

static void setCheatManagerEntryEnabled(size_t index, bool enabled)
{
    CheatRuntimeStatus status = cheatRuntimeGetStatus();
    if (!status.available || index >= status.entries.size())
    {
        return;
    }
    if (!cheatRuntimeSetEntryEnabled(index, enabled))
    {
        return;
    }
    if (enabled && !status.enabled)
    {
        setCheatManagerGlobalEnabled(true);
    }
    if (enabled)
    {
        cheatRuntimeApplyNow();
    }
    CheatRuntimeStatus updated = cheatRuntimeGetStatus();
    saveCheatManagerSelection(updated);
    frontendMenuRefresh();
    refreshCheatManagerWindow();
}

static void setAllCheatManagerEntries(bool enabled)
{
    CheatRuntimeStatus status = cheatRuntimeGetStatus();
    if (!status.available || status.entries.empty())
    {
        return;
    }
    for (size_t i = 0; i < status.entries.size(); ++i)
    {
        cheatRuntimeSetEntryEnabled(i, enabled);
    }
    if (enabled && !status.enabled)
    {
        setCheatManagerGlobalEnabled(true);
    }
    if (enabled)
    {
        cheatRuntimeApplyNow();
    }
    CheatRuntimeStatus updated = cheatRuntimeGetStatus();
    saveCheatManagerSelection(updated);
    frontendMenuRefresh();
    refreshCheatManagerWindow();
}

static void setSelectedCheatManagerEntry(bool enabled)
{
    int row = ListView_GetNextItem(g_cheatManagerList, -1, LVNI_SELECTED);
    if (row < 0)
    {
        return;
    }
    setCheatManagerEntryEnabled((size_t)row, enabled);
}

static void setupCheatManagerList(HWND list)
{
    DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
        LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES;
    ListView_SetExtendedListViewStyle(list, exStyle);

    LVCOLUMNW column;
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = (LPWSTR)(cheatManagerChinese() ? L"\u529f\u80fd" : L"Feature");
    column.cx = 570;
    column.iSubItem = 0;
    SendMessageW(list, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
}

static void createCheatManagerContents(void)
{
    g_cheatManagerFile = createCheatManagerChild(L"STATIC", L"",
        0, SS_PATHELLIPSIS | SS_NOPREFIX, kCheatManagerMargin, kCheatManagerTopY,
        kCheatManagerFileTextWidth, kCheatManagerFileHeight, kCheatManagerIdFile);
    g_cheatManagerEnable = createCheatManagerChild(L"BUTTON",
        cheatManagerChinese() ? L"\u542f\u7528\u91d1\u624b\u6307" : L"Enable Cheats",
        0, BS_AUTOCHECKBOX, kCheatManagerEnableX, kCheatManagerEnableY,
        160, kCheatManagerEnableHeight, kCheatManagerIdEnable);

    g_cheatManagerList = createCheatManagerChild(WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kCheatManagerMargin, kCheatManagerListY,
        kCheatManagerListWidth, kCheatManagerListHeight, kCheatManagerIdList);
    setupCheatManagerList(g_cheatManagerList);

    g_cheatManagerStatus = createCheatManagerChild(L"STATIC", L"",
        0, SS_ENDELLIPSIS | SS_NOPREFIX, kCheatManagerMargin, kCheatManagerStatusY,
        kCheatManagerListWidth, kCheatManagerStatusHeight, kCheatManagerIdStatus);

    int x = kCheatManagerMargin;
    createCheatManagerButton(cheatManagerChinese() ? L"\u542f\u7528\u9009\u4e2d" : L"Enable Row",
        x, kCheatManagerButtonY, kCheatManagerBottomButtonWidth, kCheatManagerIdEnableSelected);
    x += kCheatManagerBottomButtonWidth + kCheatManagerBottomButtonGap;
    createCheatManagerButton(cheatManagerChinese() ? L"\u7981\u7528\u9009\u4e2d" : L"Disable Row",
        x, kCheatManagerButtonY, kCheatManagerBottomButtonWidth, kCheatManagerIdDisableSelected);
    x += kCheatManagerBottomButtonWidth + kCheatManagerBottomButtonGap;
    createCheatManagerButton(cheatManagerChinese() ? L"\u5168\u90e8\u542f\u7528" : L"Enable All",
        x, kCheatManagerButtonY, kCheatManagerBottomButtonWidth, kCheatManagerIdEnableAll);
    x += kCheatManagerBottomButtonWidth + kCheatManagerBottomButtonGap;
    createCheatManagerButton(cheatManagerChinese() ? L"\u5168\u90e8\u7981\u7528" : L"Disable All",
        x, kCheatManagerButtonY, kCheatManagerBottomButtonWidth, kCheatManagerIdDisableAll);
    x += kCheatManagerBottomButtonWidth + kCheatManagerBottomButtonGap;
    createCheatManagerButton(cheatManagerChinese() ? L"\u7acb\u5373\u5e94\u7528" : L"Apply Now",
        x, kCheatManagerButtonY, kCheatManagerBottomButtonWidth, kCheatManagerIdApply);
    x += kCheatManagerBottomButtonWidth + kCheatManagerBottomButtonGap;
    createCheatManagerButton(cheatManagerChinese() ? L"\u5237\u65b0" : L"Refresh",
        x, kCheatManagerButtonY, kCheatManagerBottomButtonWidth, kCheatManagerIdRefresh);
    createCheatManagerButton(cheatManagerChinese() ? L"\u5173\u95ed" : L"Close",
        kCheatManagerRightX - kCheatManagerCloseButtonWidth,
        10, kCheatManagerCloseButtonWidth, kCheatManagerIdClose);

    refreshCheatManagerWindow();
}

static LRESULT CALLBACK cheatManagerWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        switch (id)
        {
        case kCheatManagerIdEnable:
            setCheatManagerGlobalEnabled(
                SendMessageW(g_cheatManagerEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
            refreshCheatManagerWindow();
            return 0;
        case kCheatManagerIdEnableSelected:
            setSelectedCheatManagerEntry(true);
            return 0;
        case kCheatManagerIdDisableSelected:
            setSelectedCheatManagerEntry(false);
            return 0;
        case kCheatManagerIdEnableAll:
            setAllCheatManagerEntries(true);
            return 0;
        case kCheatManagerIdDisableAll:
            setAllCheatManagerEntries(false);
            return 0;
        case kCheatManagerIdApply:
            cheatRuntimeApplyNow();
            refreshCheatManagerWindow();
            return 0;
        case kCheatManagerIdRefresh:
            refreshCheatManagerWindow();
            return 0;
        case kCheatManagerIdClose:
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
        if (!g_cheatManagerRefreshing && header && header->idFrom == kCheatManagerIdList &&
            header->code == LVN_ITEMCHANGED)
        {
            NMLISTVIEW* changed = (NMLISTVIEW*)lParam;
            if ((changed->uChanged & LVIF_STATE) &&
                ((changed->uOldState ^ changed->uNewState) & LVIS_STATEIMAGEMASK) &&
                changed->iItem >= 0)
            {
                setCheatManagerEntryEnabled((size_t)changed->iItem,
                    ListView_GetCheckState(g_cheatManagerList, changed->iItem) != FALSE);
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)(g_cheatManagerBackgroundBrush ?
            g_cheatManagerBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_cheatManagerWindow == hwnd)
        {
            g_cheatManagerWindow = NULL;
            g_cheatManagerList = NULL;
            g_cheatManagerStatus = NULL;
            g_cheatManagerFile = NULL;
            g_cheatManagerEnable = NULL;
        }
        if (g_cheatManagerFont && g_cheatManagerOwnFont)
        {
            DeleteObject(g_cheatManagerFont);
        }
        g_cheatManagerFont = NULL;
        g_cheatManagerOwnFont = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensureCheatManagerWindowClass(void)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = cheatManagerWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DingooPieCheatManagerWindow";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = g_cheatManagerBackgroundBrush;
    RegisterClassW(&wc);
    registered = true;
}

void cheatManagerOpenWindow(
    HWND owner,
    UiLanguage language,
    EmulatorSettings* settings,
    const std::string& currentAppPath)
{
    (void)owner;
    g_cheatManagerLanguage = language;
    g_cheatManagerSettings = settings;
    g_cheatManagerAppPath = currentAppPath;

    if (g_cheatManagerWindow)
    {
        ShowWindow(g_cheatManagerWindow, SW_SHOWNOACTIVATE);
        refreshCheatManagerWindow();
        return;
    }

    INITCOMMONCONTROLSEX controls;
    memset(&controls, 0, sizeof(controls));
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    g_cheatManagerBackgroundBrush = GetSysColorBrush(COLOR_BTNFACE);
    ensureCheatManagerWindowClass();

    g_cheatManagerWindow = CreateWindowExW(WS_EX_CONTROLPARENT,
        L"DingooPieCheatManagerWindow",
        cheatManagerTitle(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, kCheatManagerWindowWidth, kCheatManagerWindowHeight,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_cheatManagerWindow)
    {
        return;
    }

    applyCheatManagerIcon(g_cheatManagerWindow);
    createCheatManagerContents();

    ShowWindow(g_cheatManagerWindow, SW_SHOWNOACTIVATE);
    UpdateWindow(g_cheatManagerWindow);
}

#endif

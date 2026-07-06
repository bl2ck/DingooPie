#include "save_state_manager_ui.h"

#ifdef _WIN32

#include "platform_win32.h"
#include "resource_ids.h"
#include "save_state.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <commctrl.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <time.h>
#include <windows.h>

static HWND g_saveStateManagerWindow = NULL;
static HWND g_saveStateManagerList = NULL;
static HWND g_saveStateManagerStatus = NULL;
static HWND g_saveStateManagerAppText = NULL;
static HWND g_saveStateManagerThumbnailLabel = NULL;
static HWND g_saveStateManagerThumbnailImage = NULL;
static HWND g_saveStateManagerThumbnailText = NULL;
static HBITMAP g_saveStateManagerThumbnailBitmap = NULL;
static HBRUSH g_saveStateManagerBackgroundBrush = NULL;
static HFONT g_saveStateManagerFont = NULL;
static bool g_saveStateManagerOwnFont = false;
static UiLanguage g_saveStateManagerLanguage = UI_LANGUAGE_ENGLISH;
static std::string g_saveStateManagerAppPath;
static bool g_saveStateManagerGameRunning = false;
static SaveStateManagerCallbacks g_saveStateManagerCallbacks = {};

static const int kSaveStateManagerWindowWidth = 660;
static const int kSaveStateManagerWindowHeight = 464;
static const int kSaveStateManagerMargin = 18;
static const int kSaveStateManagerRightX = 624;
static const int kSaveStateManagerButtonWidth = 96;
static const int kSaveStateManagerCloseButtonWidth = 74;
static const int kSaveStateManagerButtonHeight = 28;
static const int kSaveStateManagerHeaderButtonGap = 10;
static const int kSaveStateManagerTopY = 10;
static const int kSaveStateManagerAppTextHeight = 22;
static const int kSaveStateManagerAppToListGap = 8;
static const int kSaveStateManagerContentWidth = kSaveStateManagerRightX - kSaveStateManagerMargin;
static const int kSaveStateManagerThumbnailGap = 12;
static const int kSaveStateManagerThumbnailWidth = 160;
static const int kSaveStateManagerThumbnailHeight = 120;
static const int kSaveStateManagerThumbnailLabelHeight = 20;
static const int kSaveStateManagerListY =
    kSaveStateManagerTopY + kSaveStateManagerAppTextHeight + kSaveStateManagerAppToListGap;
static const int kSaveStateManagerThumbnailImageY =
    kSaveStateManagerListY + kSaveStateManagerThumbnailLabelHeight + 4;
static const int kSaveStateManagerListHeight = 312;
static const int kSaveStateManagerListToStatusGap = 6;
static const int kSaveStateManagerStatusHeight = 24;
static const int kSaveStateManagerStatusToButtonGap = 6;
static const int kSaveStateManagerStatusY =
    kSaveStateManagerListY + kSaveStateManagerListHeight + kSaveStateManagerListToStatusGap;
static const int kSaveStateManagerButtonY =
    kSaveStateManagerStatusY + kSaveStateManagerStatusHeight + kSaveStateManagerStatusToButtonGap;
static const int kSaveStateManagerBottomButtonGap = 6;
static const int kSaveStateManagerAppTextWidth =
    kSaveStateManagerContentWidth - kSaveStateManagerCloseButtonWidth - kSaveStateManagerHeaderButtonGap;

static const int kSaveStateManagerIdList = 46001;
static const int kSaveStateManagerIdStatus = 46002;
static const int kSaveStateManagerIdAppText = 46003;
static const int kSaveStateManagerIdSave = 46004;
static const int kSaveStateManagerIdLoad = 46005;
static const int kSaveStateManagerIdDelete = 46006;
static const int kSaveStateManagerIdOpenFolder = 46007;
static const int kSaveStateManagerIdRefresh = 46008;
static const int kSaveStateManagerIdClose = 46009;

static bool saveStateManagerChinese(void)
{
    return g_saveStateManagerLanguage == UI_LANGUAGE_CHINESE;
}

static const wchar_t* saveStateManagerTitle(void)
{
    return saveStateManagerChinese() ?
        L"\u5b58\u6863\u7ba1\u7406\u5668" :
        L"Save Manager";
}

static HWND saveStateManagerItem(int id)
{
    return g_saveStateManagerWindow ? GetDlgItem(g_saveStateManagerWindow, id) : NULL;
}

static HICON loadSaveStateManagerIcon(int size)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED);
}

static void applySaveStateManagerIcon(HWND window)
{
    if (!window)
    {
        return;
    }
    HICON largeIcon = loadSaveStateManagerIcon(32);
    HICON smallIcon = loadSaveStateManagerIcon(16);
    if (largeIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    }
    if (smallIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    }
}

static HFONT saveStateManagerFont(void)
{
    if (g_saveStateManagerFont)
    {
        return g_saveStateManagerFont;
    }

    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0))
    {
        g_saveStateManagerFont = CreateFontIndirectW(&metrics.lfMessageFont);
        g_saveStateManagerOwnFont = g_saveStateManagerFont != NULL;
    }
    if (!g_saveStateManagerFont)
    {
        g_saveStateManagerFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_saveStateManagerOwnFont = false;
    }
    return g_saveStateManagerFont;
}

static HWND createSaveStateManagerChild(const wchar_t* className, const wchar_t* text,
    DWORD exStyle, DWORD style, int x, int y, int w, int h, int id)
{
    DWORD childStyle = (DWORD)platformWin32NormalizeChildStyle(className, style);
    HWND child = CreateWindowExW(exStyle, className, text,
        WS_CHILD | WS_VISIBLE | childStyle,
        x, y, w, h, g_saveStateManagerWindow, (HMENU)(INT_PTR)id,
        GetModuleHandleW(NULL), NULL);
    if (child)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)saveStateManagerFont(), TRUE);
    }
    return child;
}

static HWND createSaveStateManagerButton(const wchar_t* text, int x, int y, int w, int id)
{
    return createSaveStateManagerChild(L"BUTTON", text, 0, 0,
        x, y, w, kSaveStateManagerButtonHeight, id);
}

static void setSaveStateManagerText(HWND item, const wchar_t* text)
{
    if (!item)
    {
        return;
    }
    SetWindowTextW(item, text ? text : L"");
    InvalidateRect(item, NULL, TRUE);
}

static void setSaveStateManagerStatus(const wchar_t* text)
{
    setSaveStateManagerText(g_saveStateManagerStatus, text);
}

static std::wstring saveStateManagerTimeText(uint64_t timestamp)
{
    if (!timestamp)
    {
        return L"-";
    }

    time_t value = (time_t)timestamp;
    struct tm localTime;
    if (localtime_s(&localTime, &value) != 0)
    {
        return L"-";
    }

    wchar_t text[32] = {};
    swprintf(text, sizeof(text) / sizeof(text[0]), L"%04d-%02d-%02d %02d:%02d:%02d",
        localTime.tm_year + 1900,
        localTime.tm_mon + 1,
        localTime.tm_mday,
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec);
    return std::wstring(text);
}

static uint64_t saveStateManagerFileSize(const std::string& path)
{
    if (path.empty())
    {
        return 0;
    }
    WIN32_FILE_ATTRIBUTE_DATA data;
    memset(&data, 0, sizeof(data));
    std::wstring widePath = platformUtf8ToWide(path);
    if (!GetFileAttributesExW(widePath.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        return 0;
    }
    return ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

static std::wstring saveStateManagerSizeText(uint64_t bytes)
{
    wchar_t text[32] = {};
    if (bytes >= 1024ull * 1024ull * 1024ull)
    {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"%.2f GB",
            (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
    else if (bytes >= 1024ull * 1024ull)
    {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"%.2f MB",
            (double)bytes / (1024.0 * 1024.0));
    }
    else if (bytes >= 1024ull)
    {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"%.2f KB",
            (double)bytes / 1024.0);
    }
    else
    {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"%llu B",
            (unsigned long long)bytes);
    }
    return std::wstring(text);
}

static void setSaveStateManagerListText(HWND list, int row, int column, const wchar_t* text)
{
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = column;
    item.pszText = (LPWSTR)text;
    SendMessageW(list, LVM_SETITEMW, 0, (LPARAM)&item);
}

static int selectedSaveStateManagerSlot(void)
{
    if (!g_saveStateManagerList)
    {
        return 0;
    }
    int row = ListView_GetNextItem(g_saveStateManagerList, -1, LVNI_SELECTED);
    return row >= 0 && row < kSaveStateSlotCount ? row + 1 : 0;
}

static bool saveStateManagerFileExists(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }
    std::wstring widePath = platformUtf8ToWide(path);
    DWORD attributes = GetFileAttributesW(widePath.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void clearSaveStateManagerThumbnail(void)
{
    if (g_saveStateManagerThumbnailImage)
    {
        SendMessageW(g_saveStateManagerThumbnailImage, STM_SETIMAGE, IMAGE_BITMAP, 0);
    }
    if (g_saveStateManagerThumbnailBitmap)
    {
        DeleteObject(g_saveStateManagerThumbnailBitmap);
        g_saveStateManagerThumbnailBitmap = NULL;
    }
}

static void setSaveStateManagerThumbnailText(const wchar_t* text)
{
    setSaveStateManagerText(g_saveStateManagerThumbnailText, text);
    if (g_saveStateManagerThumbnailImage)
    {
        ShowWindow(g_saveStateManagerThumbnailImage, SW_HIDE);
    }
    if (g_saveStateManagerThumbnailText)
    {
        ShowWindow(g_saveStateManagerThumbnailText, SW_SHOW);
    }
}

static void updateSaveStateManagerThumbnail(void)
{
    clearSaveStateManagerThumbnail();

    int slot = selectedSaveStateManagerSlot();
    SaveStateSlotInfo info = {};
    if (slot > 0 && !g_saveStateManagerAppPath.empty())
    {
        info = saveStateSlotInfo(g_saveStateManagerAppPath, slot);
    }
    if (slot <= 0 || !info.exists)
    {
        setSaveStateManagerThumbnailText(saveStateManagerChinese() ?
            L"\u65e0\u7f29\u7565\u56fe" : L"No thumbnail");
        return;
    }

    std::string thumbnailPath = saveStateThumbnailPathForSlot(g_saveStateManagerAppPath, slot);
    if (!saveStateManagerFileExists(thumbnailPath))
    {
        setSaveStateManagerThumbnailText(saveStateManagerChinese() ?
            L"\u65e0\u7f29\u7565\u56fe" : L"No thumbnail");
        return;
    }

    std::wstring widePath = platformUtf8ToWide(thumbnailPath);
    g_saveStateManagerThumbnailBitmap = (HBITMAP)LoadImageW(NULL, widePath.c_str(),
        IMAGE_BITMAP, kSaveStateManagerThumbnailWidth, kSaveStateManagerThumbnailHeight,
        LR_LOADFROMFILE);
    if (!g_saveStateManagerThumbnailBitmap)
    {
        setSaveStateManagerThumbnailText(saveStateManagerChinese() ?
            L"\u7f29\u7565\u56fe\u52a0\u8f7d\u5931\u8d25" : L"Thumbnail failed");
        return;
    }

    SendMessageW(g_saveStateManagerThumbnailImage, STM_SETIMAGE, IMAGE_BITMAP,
        (LPARAM)g_saveStateManagerThumbnailBitmap);
    ShowWindow(g_saveStateManagerThumbnailText, SW_HIDE);
    ShowWindow(g_saveStateManagerThumbnailImage, SW_SHOW);
}

static SaveStateSlotInfo saveStateManagerSlotInfo(int slot)
{
    SaveStateSlotInfo info = {};
    if (slot > 0 && slot <= kSaveStateSlotCount && !g_saveStateManagerAppPath.empty())
    {
        info = saveStateSlotInfo(g_saveStateManagerAppPath, slot);
    }
    return info;
}

static void updateSaveStateManagerButtons(void)
{
    int slot = selectedSaveStateManagerSlot();
    SaveStateSlotInfo info = saveStateManagerSlotInfo(slot);
    EnableWindow(saveStateManagerItem(kSaveStateManagerIdSave),
        g_saveStateManagerGameRunning && slot > 0);
    EnableWindow(saveStateManagerItem(kSaveStateManagerIdLoad),
        g_saveStateManagerGameRunning && slot > 0 && info.exists);
    EnableWindow(saveStateManagerItem(kSaveStateManagerIdDelete),
        slot > 0 && info.exists);
    EnableWindow(saveStateManagerItem(kSaveStateManagerIdOpenFolder),
        !g_saveStateManagerAppPath.empty());
    updateSaveStateManagerThumbnail();
}

static void refreshSaveStateManagerList(void)
{
    if (!g_saveStateManagerList)
    {
        return;
    }

    int selectedRow = ListView_GetNextItem(g_saveStateManagerList, -1, LVNI_SELECTED);
    SendMessageW(g_saveStateManagerList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_saveStateManagerList);

    for (int slot = 1; slot <= kSaveStateSlotCount; ++slot)
    {
        SaveStateSlotInfo info = saveStateManagerSlotInfo(slot);
        wchar_t slotText[32] = {};
        swprintf(slotText, sizeof(slotText) / sizeof(slotText[0]),
            saveStateManagerChinese() ? L"\u6863\u4f4d %d" : L"Slot %d", slot);
        const wchar_t* stateText = info.exists ?
            (saveStateManagerChinese() ? L"\u5df2\u4fdd\u5b58" : L"Saved") :
            (saveStateManagerChinese() ? L"\u7a7a" : L"Empty");
        std::wstring timeText = info.exists ? saveStateManagerTimeText(info.modifiedTime) : L"-";
        std::wstring sizeText = info.exists ? saveStateManagerSizeText(saveStateManagerFileSize(info.path)) : L"-";

        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = slot - 1;
        item.iSubItem = 0;
        item.pszText = slotText;
        SendMessageW(g_saveStateManagerList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        setSaveStateManagerListText(g_saveStateManagerList, slot - 1, 1, stateText);
        setSaveStateManagerListText(g_saveStateManagerList, slot - 1, 2, sizeText.c_str());
        setSaveStateManagerListText(g_saveStateManagerList, slot - 1, 3, timeText.c_str());
    }

    if (selectedRow < 0 || selectedRow >= kSaveStateSlotCount)
    {
        selectedRow = 0;
    }
    ListView_SetItemState(g_saveStateManagerList, selectedRow,
        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

    SendMessageW(g_saveStateManagerList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_saveStateManagerList, NULL, TRUE);
    updateSaveStateManagerButtons();
}

static void refreshSaveStateManagerWindow(void)
{
    std::wstring appText = saveStateManagerChinese() ? L"\u6e38\u620f: " : L"Game: ";
    appText += g_saveStateManagerAppPath.empty() ?
        (saveStateManagerChinese() ? L"\u672a\u52a0\u8f7d" : L"(not loaded)") :
        platformUtf8ToWide(g_saveStateManagerAppPath);
    setSaveStateManagerText(g_saveStateManagerAppText, appText.c_str());
    refreshSaveStateManagerList();
    setSaveStateManagerStatus(saveStateManagerChinese() ?
        L"\u9009\u62e9\u6863\u4f4d\u540e\u53ef\u4fdd\u5b58\u3001\u8bfb\u53d6\u6216\u5220\u9664\u3002" :
        L"Select a slot to save, load, or delete.");
}

static std::wstring saveStateManagerSlotLabel(int slot, bool exists, uint64_t modifiedTime)
{
    wchar_t text[96] = {};
    std::wstring timeText = saveStateManagerTimeText(modifiedTime);
    if (saveStateManagerChinese())
    {
        if (exists && !timeText.empty())
        {
            swprintf(text, sizeof(text) / sizeof(text[0]),
                L"\u6863\u4f4d %d  %ls", slot, timeText.c_str());
        }
        else
        {
            swprintf(text, sizeof(text) / sizeof(text[0]),
                L"\u6863\u4f4d %d%s", slot, exists ? L"" : L" \u7a7a");
        }
    }
    else
    {
        if (exists && !timeText.empty())
        {
            swprintf(text, sizeof(text) / sizeof(text[0]),
                L"Slot %d  %ls", slot, timeText.c_str());
        }
        else
        {
            swprintf(text, sizeof(text) / sizeof(text[0]),
                L"Slot %d%s", slot, exists ? L"" : L" (empty)");
        }
    }
    return std::wstring(text);
}

static void notifySaveStateManagerChanged(void)
{
    if (g_saveStateManagerCallbacks.changed)
    {
        g_saveStateManagerCallbacks.changed(g_saveStateManagerCallbacks.userData);
    }
}

static void saveSelectedSaveStateSlot(void)
{
    int slot = selectedSaveStateManagerSlot();
    if (slot <= 0 || !g_saveStateManagerCallbacks.saveSlot)
    {
        return;
    }
    std::string error;
    bool ok = g_saveStateManagerCallbacks.saveSlot(
        slot, &error, g_saveStateManagerCallbacks.userData);
    refreshSaveStateManagerList();
    if (ok)
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u5df2\u4fdd\u5b58\u9009\u4e2d\u6863\u4f4d\u3002" :
            L"Selected slot saved.");
    }
    else if (error.empty())
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u64cd\u4f5c\u5df2\u53d6\u6d88\u3002" :
            L"Operation canceled.");
    }
    else
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u4fdd\u5b58\u5931\u8d25\u3002" :
            L"Failed to save selected slot.");
    }
}

static void loadSelectedSaveStateSlot(void)
{
    int slot = selectedSaveStateManagerSlot();
    if (slot <= 0 || !g_saveStateManagerCallbacks.loadSlot)
    {
        return;
    }
    SaveStateSlotInfo info = saveStateManagerSlotInfo(slot);
    if (!info.exists)
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u9009\u4e2d\u6863\u4f4d\u4e3a\u7a7a\u3002" :
            L"Selected slot is empty.");
        return;
    }
    std::string error;
    bool ok = g_saveStateManagerCallbacks.loadSlot(
        slot, &error, g_saveStateManagerCallbacks.userData);
    refreshSaveStateManagerList();
    if (ok)
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u5df2\u8bfb\u53d6\u9009\u4e2d\u6863\u4f4d\u3002" :
            L"Selected slot loaded.");
    }
    else if (error.empty())
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u64cd\u4f5c\u5df2\u53d6\u6d88\u3002" :
            L"Operation canceled.");
    }
    else
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u8bfb\u53d6\u5931\u8d25\u3002" :
            L"Failed to load selected slot.");
    }
}

static void deleteSelectedSaveStateSlot(void)
{
    int slot = selectedSaveStateManagerSlot();
    if (slot <= 0)
    {
        return;
    }
    SaveStateSlotInfo info = saveStateManagerSlotInfo(slot);
    if (!info.exists)
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u9009\u4e2d\u6863\u4f4d\u4e3a\u7a7a\u3002" :
            L"Selected slot is empty.");
        return;
    }
    std::wstring message = saveStateManagerChinese() ?
        L"\u5220\u9664\u9009\u4e2d\u5373\u65f6\u5b58\u6863\uff1f" :
        L"Delete the selected save state?";
    message += L"\n";
    message += saveStateManagerSlotLabel(slot, info.exists, info.modifiedTime);
    if (MessageBoxW(g_saveStateManagerWindow, message.c_str(),
        saveStateManagerTitle(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }
    std::wstring path = platformUtf8ToWide(info.path);
    bool ok = DeleteFileW(path.c_str()) != FALSE;
    if (ok)
    {
        std::string thumbnailPath = saveStateThumbnailPathForSlot(g_saveStateManagerAppPath, slot);
        if (saveStateManagerFileExists(thumbnailPath))
        {
            DeleteFileW(platformUtf8ToWide(thumbnailPath).c_str());
        }
    }
    refreshSaveStateManagerList();
    notifySaveStateManagerChanged();
    setSaveStateManagerStatus(ok ?
        (saveStateManagerChinese() ? L"\u5df2\u5220\u9664\u9009\u4e2d\u6863\u4f4d\u3002" : L"Selected slot deleted.") :
        (saveStateManagerChinese() ? L"\u5220\u9664\u5931\u8d25\u3002" : L"Failed to delete selected slot."));
}

static std::wstring saveStateManagerFolder(void)
{
    std::string path = saveStatePathForSlot(g_saveStateManagerAppPath, 1);
    std::wstring folder = platformUtf8ToWide(path);
    size_t slash = folder.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        folder.resize(slash);
    }
    return folder;
}

static void openSaveStateManagerFolder(void)
{
    std::wstring folder = saveStateManagerFolder();
    if (folder.empty())
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u5c1a\u672a\u52a0\u8f7d\u6e38\u620f\u3002" :
            L"No game is loaded.");
        return;
    }
    CreateDirectoryW(folder.c_str(), NULL);
    HINSTANCE result = ShellExecuteW(g_saveStateManagerWindow,
        L"open", folder.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32)
    {
        setSaveStateManagerStatus(saveStateManagerChinese() ?
            L"\u6253\u5f00\u5b58\u6863\u76ee\u5f55\u5931\u8d25\u3002" :
            L"Failed to open save state folder.");
    }
}

static void setupSaveStateManagerList(HWND list)
{
    DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
    ListView_SetExtendedListViewStyle(list, exStyle);

    const wchar_t* headers[] = {
        saveStateManagerChinese() ? L"\u6863\u4f4d" : L"Slot",
        saveStateManagerChinese() ? L"\u72b6\u6001" : L"State",
        saveStateManagerChinese() ? L"\u5927\u5c0f" : L"Size",
        saveStateManagerChinese() ? L"\u4fee\u6539\u65f6\u95f4" : L"Modified"
    };
    const int widths[] = { 64, 72, 92, 166 };
    for (int i = 0; i < 4; ++i)
    {
        LVCOLUMNW column;
        memset(&column, 0, sizeof(column));
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = (LPWSTR)headers[i];
        column.cx = widths[i];
        column.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&column);
    }
}

static void createSaveStateManagerContents(void)
{
    const int thumbnailFrameWidth =
        kSaveStateManagerThumbnailWidth + GetSystemMetrics(SM_CXEDGE) * 2;
    const int thumbnailFrameHeight =
        kSaveStateManagerThumbnailHeight + GetSystemMetrics(SM_CYEDGE) * 2;
    const int thumbnailX = kSaveStateManagerRightX - thumbnailFrameWidth;
    const int listWidth =
        thumbnailX - kSaveStateManagerMargin - kSaveStateManagerThumbnailGap;

    g_saveStateManagerAppText = createSaveStateManagerChild(L"STATIC", L"",
        0, SS_PATHELLIPSIS | SS_NOPREFIX,
        kSaveStateManagerMargin, kSaveStateManagerTopY,
        kSaveStateManagerAppTextWidth,
        kSaveStateManagerAppTextHeight, kSaveStateManagerIdAppText);
    createSaveStateManagerButton(saveStateManagerChinese() ? L"\u5173\u95ed" : L"Close",
        kSaveStateManagerRightX - kSaveStateManagerCloseButtonWidth,
        kSaveStateManagerTopY, kSaveStateManagerCloseButtonWidth, kSaveStateManagerIdClose);

    g_saveStateManagerList = createSaveStateManagerChild(WC_LISTVIEWW, L"",
        WS_EX_CLIENTEDGE, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kSaveStateManagerMargin, kSaveStateManagerListY,
        listWidth, kSaveStateManagerListHeight, kSaveStateManagerIdList);
    setupSaveStateManagerList(g_saveStateManagerList);

    g_saveStateManagerThumbnailLabel = createSaveStateManagerChild(L"STATIC",
        saveStateManagerChinese() ? L"\u7f29\u7565\u56fe" : L"Thumbnail",
        0, SS_ENDELLIPSIS | SS_NOPREFIX,
        thumbnailX, kSaveStateManagerListY,
        thumbnailFrameWidth, kSaveStateManagerThumbnailLabelHeight, -1);
    g_saveStateManagerThumbnailImage = createSaveStateManagerChild(L"STATIC", L"",
        WS_EX_CLIENTEDGE, SS_BITMAP | SS_CENTERIMAGE,
        thumbnailX, kSaveStateManagerThumbnailImageY,
        thumbnailFrameWidth, thumbnailFrameHeight, -1);
    g_saveStateManagerThumbnailText = createSaveStateManagerChild(L"STATIC", L"",
        WS_EX_CLIENTEDGE, SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX,
        thumbnailX, kSaveStateManagerThumbnailImageY,
        thumbnailFrameWidth, thumbnailFrameHeight, -1);

    g_saveStateManagerStatus = createSaveStateManagerChild(L"STATIC", L"",
        0, SS_ENDELLIPSIS | SS_NOPREFIX,
        kSaveStateManagerMargin, kSaveStateManagerStatusY,
        kSaveStateManagerContentWidth, kSaveStateManagerStatusHeight, kSaveStateManagerIdStatus);

    int x = kSaveStateManagerMargin;
    createSaveStateManagerButton(saveStateManagerChinese() ? L"\u4fdd\u5b58" : L"Save",
        x, kSaveStateManagerButtonY, kSaveStateManagerButtonWidth, kSaveStateManagerIdSave);
    x += kSaveStateManagerButtonWidth + kSaveStateManagerBottomButtonGap;
    createSaveStateManagerButton(saveStateManagerChinese() ? L"\u8bfb\u53d6" : L"Load",
        x, kSaveStateManagerButtonY, kSaveStateManagerButtonWidth, kSaveStateManagerIdLoad);
    x += kSaveStateManagerButtonWidth + kSaveStateManagerBottomButtonGap;
    createSaveStateManagerButton(saveStateManagerChinese() ? L"\u5220\u9664" : L"Delete",
        x, kSaveStateManagerButtonY, kSaveStateManagerButtonWidth, kSaveStateManagerIdDelete);
    x += kSaveStateManagerButtonWidth + kSaveStateManagerBottomButtonGap;
    createSaveStateManagerButton(saveStateManagerChinese() ? L"\u6253\u5f00\u76ee\u5f55" : L"Open Folder",
        x, kSaveStateManagerButtonY, kSaveStateManagerButtonWidth, kSaveStateManagerIdOpenFolder);
    x += kSaveStateManagerButtonWidth + kSaveStateManagerBottomButtonGap;
    createSaveStateManagerButton(saveStateManagerChinese() ? L"\u5237\u65b0" : L"Refresh",
        x, kSaveStateManagerButtonY, kSaveStateManagerButtonWidth, kSaveStateManagerIdRefresh);

    refreshSaveStateManagerWindow();
}

static LRESULT CALLBACK saveStateManagerWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        switch (id)
        {
        case kSaveStateManagerIdSave:
            saveSelectedSaveStateSlot();
            return 0;
        case kSaveStateManagerIdLoad:
            loadSelectedSaveStateSlot();
            return 0;
        case kSaveStateManagerIdDelete:
            deleteSelectedSaveStateSlot();
            return 0;
        case kSaveStateManagerIdOpenFolder:
            openSaveStateManagerFolder();
            return 0;
        case kSaveStateManagerIdRefresh:
            refreshSaveStateManagerWindow();
            return 0;
        case kSaveStateManagerIdClose:
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
        if (header && header->idFrom == kSaveStateManagerIdList)
        {
            if (header->code == LVN_ITEMCHANGED)
            {
                updateSaveStateManagerButtons();
            }
            else if (header->code == NM_DBLCLK)
            {
                loadSelectedSaveStateSlot();
                return 0;
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)(g_saveStateManagerBackgroundBrush ?
            g_saveStateManagerBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_saveStateManagerWindow == hwnd)
        {
            clearSaveStateManagerThumbnail();
            g_saveStateManagerWindow = NULL;
            g_saveStateManagerList = NULL;
            g_saveStateManagerStatus = NULL;
            g_saveStateManagerAppText = NULL;
            g_saveStateManagerThumbnailLabel = NULL;
            g_saveStateManagerThumbnailImage = NULL;
            g_saveStateManagerThumbnailText = NULL;
            g_saveStateManagerAppPath.clear();
        }
        if (g_saveStateManagerFont && g_saveStateManagerOwnFont)
        {
            DeleteObject(g_saveStateManagerFont);
        }
        g_saveStateManagerFont = NULL;
        g_saveStateManagerOwnFont = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensureSaveStateManagerWindowClass(void)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = saveStateManagerWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DingooPieSaveStateManagerWindow";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = g_saveStateManagerBackgroundBrush;
    RegisterClassW(&wc);
    registered = true;
}

void saveStateManagerOpenWindow(
    HWND owner,
    UiLanguage language,
    const std::string& appPath,
    bool gameRunning,
    const SaveStateManagerCallbacks& callbacks)
{
    (void)owner;
    g_saveStateManagerLanguage = language;
    g_saveStateManagerAppPath = appPath;
    g_saveStateManagerGameRunning = gameRunning;
    g_saveStateManagerCallbacks = callbacks;

    if (g_saveStateManagerWindow)
    {
        SetWindowTextW(g_saveStateManagerWindow, saveStateManagerTitle());
        ShowWindow(g_saveStateManagerWindow, SW_SHOWNOACTIVATE);
        refreshSaveStateManagerWindow();
        return;
    }

    INITCOMMONCONTROLSEX controls;
    memset(&controls, 0, sizeof(controls));
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    g_saveStateManagerBackgroundBrush = GetSysColorBrush(COLOR_BTNFACE);
    ensureSaveStateManagerWindowClass();
    g_saveStateManagerWindow = CreateWindowExW(WS_EX_CONTROLPARENT,
        L"DingooPieSaveStateManagerWindow",
        saveStateManagerTitle(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        kSaveStateManagerWindowWidth, kSaveStateManagerWindowHeight,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_saveStateManagerWindow)
    {
        return;
    }
    applySaveStateManagerIcon(g_saveStateManagerWindow);
    createSaveStateManagerContents();
    ShowWindow(g_saveStateManagerWindow, SW_SHOWNOACTIVATE);
    UpdateWindow(g_saveStateManagerWindow);
}

#endif

#include "frontend_menu.h"

#include "app_paths.h"
#include "debug_console.h"
#include "emulator_config.h"
#include "input_controls.h"
#include "sdl_frontend.h"
#include "platform_win32.h"
#include "ui_strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

enum FrontendMenuCommand
{
    MENU_FILE_OPEN = 1001,
    MENU_FILE_RECENT_CLEAR,
    MENU_FILE_RESTART,
    MENU_FILE_PAUSE_RESUME,
    MENU_FILE_SAVE_SCREENSHOT,
    MENU_FILE_EXIT,
    MENU_VIDEO_SCALE_1X,
    MENU_VIDEO_SCALE_2X,
    MENU_VIDEO_SCALE_3X,
    MENU_VIDEO_FULLSCREEN,
    MENU_VIDEO_AA_OFF,
    MENU_VIDEO_AA_LOW,
    MENU_VIDEO_AA_CLEAR,
    MENU_VIDEO_EFFECT_NORMAL,
    MENU_VIDEO_EFFECT_GRAYSCALE,
    MENU_VIDEO_EFFECT_INVERT,
    MENU_VIDEO_EFFECT_SOFT_BLUR,
    MENU_VIDEO_EFFECT_SHARPEN,
    MENU_VIDEO_EFFECT_VIVID,
    MENU_VIDEO_EFFECT_SEPIA,
    MENU_VIDEO_EFFECT_PIXEL_GRID,
    MENU_VIDEO_EFFECT_LCD_SCANLINE,
    MENU_VIDEO_EFFECT_LIGHT_CRT,
    MENU_VIDEO_BRIGHTNESS_75,
    MENU_VIDEO_BRIGHTNESS_90,
    MENU_VIDEO_BRIGHTNESS_100,
    MENU_VIDEO_BRIGHTNESS_110,
    MENU_VIDEO_BRIGHTNESS_125,
    MENU_VIDEO_BRIGHTNESS_150,
    MENU_VIDEO_CONTRAST_75,
    MENU_VIDEO_CONTRAST_90,
    MENU_VIDEO_CONTRAST_100,
    MENU_VIDEO_CONTRAST_110,
    MENU_VIDEO_CONTRAST_125,
    MENU_VIDEO_CONTRAST_150,
    MENU_VIDEO_SATURATION_75,
    MENU_VIDEO_SATURATION_90,
    MENU_VIDEO_SATURATION_100,
    MENU_VIDEO_SATURATION_110,
    MENU_VIDEO_SATURATION_125,
    MENU_VIDEO_SATURATION_150,
    MENU_VIDEO_MINIMIZED_NORMAL,
    MENU_VIDEO_MINIMIZED_THROTTLE,
    MENU_VIDEO_MINIMIZED_PAUSE,
    MENU_VIDEO_PORTRAIT,
    MENU_VIDEO_SHOW_FPS,
    MENU_SETTINGS_AUDIO_VOLUME_0,
    MENU_SETTINGS_AUDIO_VOLUME_25,
    MENU_SETTINGS_AUDIO_VOLUME_50,
    MENU_SETTINGS_AUDIO_VOLUME_75,
    MENU_SETTINGS_AUDIO_VOLUME_100,
    MENU_SETTINGS_AUDIO_VOLUME_125,
    MENU_SETTINGS_AUDIO_VOLUME_150,
    MENU_SETTINGS_AUDIO_BUFFER_512,
    MENU_SETTINGS_AUDIO_BUFFER_1024,
    MENU_SETTINGS_AUDIO_BUFFER_2048,
    MENU_SETTINGS_AUDIO_BUFFER_4096,
    MENU_SETTINGS_AUDIO_BUFFER_8192,
    MENU_SETTINGS_DROP_AUDIO,
    MENU_INPUT_DISABLE_IME,
    MENU_INPUT_SHOW_VIRTUAL_CONTROLS,
    MENU_INPUT_MAPPING_WINDOW,
    MENU_SETTINGS_BACKEND_AUTO,
    MENU_SETTINGS_BACKEND_IRJIT,
    MENU_SETTINGS_BACKEND_INTERPRETER,
    MENU_SETTINGS_CPU_CLOCK_AUTO,
    MENU_SETTINGS_CPU_CLOCK_200,
    MENU_SETTINGS_CPU_CLOCK_336,
    MENU_SETTINGS_CPU_CLOCK_370,
    MENU_SETTINGS_CPU_CLOCK_400,
    MENU_SETTINGS_CPU_CLOCK_430,
    MENU_SETTINGS_SPEED_AUTO,
    MENU_SETTINGS_SPEED_100,
    MENU_SETTINGS_SPEED_095,
    MENU_SETTINGS_SPEED_090,
    MENU_SETTINGS_SPEED_085,
    MENU_SETTINGS_SPEED_080,
    MENU_SETTINGS_SPEED_075,
    MENU_SETTINGS_SPEED_070,
    MENU_SETTINGS_SPEED_065,
    MENU_SETTINGS_SPEED_060,
    MENU_SETTINGS_SPEED_055,
    MENU_SETTINGS_SPEED_050,
    MENU_SETTINGS_SPEED_045,
    MENU_SETTINGS_SPEED_040,
    MENU_SETTINGS_SPEED_035,
    MENU_SETTINGS_SPEED_030,
    MENU_SETTINGS_SPEED_025,
    MENU_SETTINGS_SPEED_020,
    MENU_SETTINGS_DELAY_AUTO,
    MENU_SETTINGS_DELAY_100,
    MENU_SETTINGS_DELAY_095,
    MENU_SETTINGS_DELAY_090,
    MENU_SETTINGS_DELAY_085,
    MENU_SETTINGS_DELAY_080,
    MENU_SETTINGS_DELAY_075,
    MENU_SETTINGS_DELAY_070,
    MENU_SETTINGS_DELAY_065,
    MENU_SETTINGS_DELAY_060,
    MENU_SETTINGS_DELAY_055,
    MENU_SETTINGS_DELAY_050,
    MENU_SETTINGS_DELAY_045,
    MENU_SETTINGS_DELAY_040,
    MENU_SETTINGS_DELAY_035,
    MENU_SETTINGS_DELAY_030,
    MENU_SETTINGS_DELAY_025,
    MENU_SETTINGS_DELAY_020,
    MENU_SETTINGS_LANGUAGE_ENGLISH,
    MENU_SETTINGS_LANGUAGE_CHINESE,
    MENU_SETTINGS_RESET,
    MENU_DEBUG_SHOW_CONSOLE,
    MENU_DEBUG_PROFILE,
    MENU_DEBUG_OPEN_LOG,
    MENU_HELP_ABOUT
};

static const unsigned int MENU_FILE_RECENT_BASE = 3000;
static const unsigned int MENU_FILE_RECENT_MAX =
    MENU_FILE_RECENT_BASE + EMULATOR_RECENT_APP_LIMIT - 1;

#ifdef _WIN32
static HWND g_menuWindow = NULL;
#endif
static EmulatorSettings* g_menuSettings = NULL;
static std::string g_currentAppPath;
static bool g_gameRunning = false;
static bool g_pendingRelaunch = false;
static std::string g_pendingRelaunchPath;

static void rebuildMenu(void);

static const wchar_t* uiText(UiTextId id)
{
    return uiText(g_menuSettings ? g_menuSettings->uiLanguage : UI_LANGUAGE_ENGLISH, id);
}

static std::wstring audioBufferLabel(int samples)
{
    wchar_t text[32] = {};
    if (g_menuSettings && g_menuSettings->uiLanguage == UI_LANGUAGE_CHINESE)
    {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"%d \u91c7\u6837", samples);
    }
    else
    {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"%d samples", samples);
    }
    return std::wstring(text);
}

static int percentForVideoCommand(unsigned int commandId, unsigned int firstCommand)
{
    static const int kValues[] = { 75, 90, 100, 110, 125, 150 };
    unsigned int index = commandId - firstCommand;
    if (index >= sizeof(kValues) / sizeof(kValues[0]))
    {
        return 100;
    }
    return kValues[index];
}

static int percentForAudioVolumeCommand(unsigned int commandId)
{
    static const int kValues[] = { 0, 25, 50, 75, 100, 125, 150 };
    unsigned int index = commandId - MENU_SETTINGS_AUDIO_VOLUME_0;
    if (index >= sizeof(kValues) / sizeof(kValues[0]))
    {
        return 100;
    }
    return kValues[index];
}

struct ScalePreset
{
    unsigned int commandId;
    int percent;
    const char* iniValue;
};

struct MinimizedBehaviorPreset
{
    unsigned int commandId;
    MinimizedBehavior behavior;
    UiTextId labelId;
};

static const MinimizedBehaviorPreset kMinimizedBehaviorPresets[] =
{
    { MENU_VIDEO_MINIMIZED_NORMAL, MINIMIZED_BEHAVIOR_NORMAL, TXT_VIDEO_MINIMIZED_NORMAL },
    { MENU_VIDEO_MINIMIZED_THROTTLE, MINIMIZED_BEHAVIOR_THROTTLE, TXT_VIDEO_MINIMIZED_THROTTLE },
    { MENU_VIDEO_MINIMIZED_PAUSE, MINIMIZED_BEHAVIOR_PAUSE, TXT_VIDEO_MINIMIZED_PAUSE },
};

static const ScalePreset kRuntimeSpeedPresets[] =
{
    { MENU_SETTINGS_SPEED_100, 100, "1.0" },
    { MENU_SETTINGS_SPEED_095, 95, "0.95" },
    { MENU_SETTINGS_SPEED_090, 90, "0.90" },
    { MENU_SETTINGS_SPEED_085, 85, "0.85" },
    { MENU_SETTINGS_SPEED_080, 80, "0.80" },
    { MENU_SETTINGS_SPEED_075, 75, "0.75" },
    { MENU_SETTINGS_SPEED_070, 70, "0.70" },
    { MENU_SETTINGS_SPEED_065, 65, "0.65" },
    { MENU_SETTINGS_SPEED_060, 60, "0.60" },
    { MENU_SETTINGS_SPEED_055, 55, "0.55" },
    { MENU_SETTINGS_SPEED_050, 50, "0.50" },
    { MENU_SETTINGS_SPEED_045, 45, "0.45" },
    { MENU_SETTINGS_SPEED_040, 40, "0.40" },
    { MENU_SETTINGS_SPEED_035, 35, "0.35" },
    { MENU_SETTINGS_SPEED_030, 30, "0.30" },
    { MENU_SETTINGS_SPEED_025, 25, "0.25" },
    { MENU_SETTINGS_SPEED_020, 20, "0.20" },
};

static const ScalePreset kDelayScalePresets[] =
{
    { MENU_SETTINGS_DELAY_100, 100, "1.0" },
    { MENU_SETTINGS_DELAY_095, 95, "0.95" },
    { MENU_SETTINGS_DELAY_090, 90, "0.90" },
    { MENU_SETTINGS_DELAY_085, 85, "0.85" },
    { MENU_SETTINGS_DELAY_080, 80, "0.80" },
    { MENU_SETTINGS_DELAY_075, 75, "0.75" },
    { MENU_SETTINGS_DELAY_070, 70, "0.70" },
    { MENU_SETTINGS_DELAY_065, 65, "0.65" },
    { MENU_SETTINGS_DELAY_060, 60, "0.60" },
    { MENU_SETTINGS_DELAY_055, 55, "0.55" },
    { MENU_SETTINGS_DELAY_050, 50, "0.50" },
    { MENU_SETTINGS_DELAY_045, 45, "0.45" },
    { MENU_SETTINGS_DELAY_040, 40, "0.40" },
    { MENU_SETTINGS_DELAY_035, 35, "0.35" },
    { MENU_SETTINGS_DELAY_030, 30, "0.30" },
    { MENU_SETTINGS_DELAY_025, 25, "0.25" },
    { MENU_SETTINGS_DELAY_020, 20, "0.20" },
};

static const ScalePreset* scalePresetForCommand(
    unsigned int commandId,
    const ScalePreset* presets,
    size_t presetCount)
{
    for (size_t i = 0; i < presetCount; ++i)
    {
        if (presets[i].commandId == commandId)
        {
            return &presets[i];
        }
    }
    return NULL;
}

static const ScalePreset* scalePresetForValue(
    const std::string& value,
    const ScalePreset* presets,
    size_t presetCount)
{
    if (value.empty())
    {
        return NULL;
    }

    char* end = NULL;
    double parsed = strtod(value.c_str(), &end);
    if (end == value.c_str())
    {
        return NULL;
    }

    for (size_t i = 0; i < presetCount; ++i)
    {
        double target = (double)presets[i].percent / 100.0;
        double diff = parsed - target;
        if (diff < 0.0)
        {
            diff = -diff;
        }
        if (diff < 0.0005)
        {
            return &presets[i];
        }
    }
    return NULL;
}

static const ScalePreset* runtimeSpeedPresetForCommand(unsigned int commandId)
{
    return scalePresetForCommand(commandId, kRuntimeSpeedPresets,
        sizeof(kRuntimeSpeedPresets) / sizeof(kRuntimeSpeedPresets[0]));
}

static const ScalePreset* runtimeSpeedPresetForValue(const std::string& value)
{
    return scalePresetForValue(value, kRuntimeSpeedPresets,
        sizeof(kRuntimeSpeedPresets) / sizeof(kRuntimeSpeedPresets[0]));
}

static const ScalePreset* delayScalePresetForCommand(unsigned int commandId)
{
    return scalePresetForCommand(commandId, kDelayScalePresets,
        sizeof(kDelayScalePresets) / sizeof(kDelayScalePresets[0]));
}

static const ScalePreset* delayScalePresetForValue(const std::string& value)
{
    return scalePresetForValue(value, kDelayScalePresets,
        sizeof(kDelayScalePresets) / sizeof(kDelayScalePresets[0]));
}

static const MinimizedBehaviorPreset* minimizedBehaviorPresetForCommand(unsigned int commandId)
{
    for (size_t i = 0; i < sizeof(kMinimizedBehaviorPresets) / sizeof(kMinimizedBehaviorPresets[0]); ++i)
    {
        if (kMinimizedBehaviorPresets[i].commandId == commandId)
        {
            return &kMinimizedBehaviorPresets[i];
        }
    }
    return NULL;
}

static void applyAndSaveRuntimeSettings(void)
{
    emulatorApplyRuntimeSettings(*g_menuSettings);
    emulatorSaveSettings(*g_menuSettings);
}

static void applyAndSaveVideoSettings(void)
{
    frontendApplyVideoSettings(*g_menuSettings);
    emulatorSaveSettings(*g_menuSettings);
}

#ifdef _WIN32
static void appendMenuItem(HMENU menu, UINT id, const wchar_t* text)
{
    AppendMenuW(menu, MF_STRING, id, text);
}

static void appendDisabledMenuItem(HMENU menu, const wchar_t* text)
{
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, text);
}

static void appendScalePreset(HMENU menu, const ScalePreset& preset)
{
    wchar_t label[32];
    swprintf(label, sizeof(label) / sizeof(label[0]), L"%d%%", preset.percent);
    appendMenuItem(menu, preset.commandId, label);
}

static void setMenuCheck(UINT id, bool checked)
{
    if (!g_menuWindow)
    {
        return;
    }
    HMENU menu = GetMenu(g_menuWindow);
    CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

static void setMenuEnabled(UINT id, bool enabled)
{
    if (!g_menuWindow)
    {
        return;
    }
    HMENU menu = GetMenu(g_menuWindow);
    EnableMenuItem(menu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
}

static void setMenuText(UINT id, const wchar_t* text)
{
    if (!g_menuWindow)
    {
        return;
    }
    HMENU menu = GetMenu(g_menuWindow);
    MENUITEMINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = (LPWSTR)text;
    SetMenuItemInfoW(menu, id, FALSE, &info);
}

static void requestRelaunchAfterExit(const std::string& appPath)
{
    g_pendingRelaunch = true;
    g_pendingRelaunchPath = appPath;
    printf("frontend: queued emulator relaunch app=%s\n",
        appPath.empty() ? "(none)" : appPath.c_str());
    frontendRequestQuit();
}

static bool validateAppPathForOpen(const std::string& appPath, const char* source)
{
    if (appPath.empty())
    {
        return false;
    }
    if (!appPathHasAppExtension(appPath))
    {
        printf("frontend: rejected %s app without .app extension: %s\n",
            source ? source : "selected", appPath.c_str());
        return false;
    }
    if (!platformFileExists(appPath))
    {
        printf("frontend: rejected missing %s app: %s\n",
            source ? source : "selected", appPath.c_str());
        return false;
    }
    if (!platformProbeAppHeader(appPath))
    {
        printf("frontend: rejected invalid %s app header: %s\n",
            source ? source : "selected", appPath.c_str());
        return false;
    }
    return true;
}

bool frontendMenuRequestOpenApp(const std::string& appPath)
{
    if (!validateAppPathForOpen(appPath, "open-request"))
    {
        return false;
    }

    EmulatorSettings settings = g_menuSettings ? *g_menuSettings : emulatorLoadSettings();
    bool changed = emulatorRememberRecentApp(&settings, appPath);
    if (emulatorSaveSettings(settings))
    {
        if (g_menuSettings)
        {
            *g_menuSettings = settings;
            if (changed)
            {
                rebuildMenu();
            }
        }
    }
    else
    {
        printf("frontend: failed to save recent app before relaunch: %s\n", appPath.c_str());
    }

    requestRelaunchAfterExit(appPath);
    return true;
}

bool frontendMenuConsumeRelaunchPath(std::string* outPath)
{
    bool pending = g_pendingRelaunch;
    if (outPath)
    {
        *outPath = g_pendingRelaunchPath;
    }
    g_pendingRelaunch = false;
    g_pendingRelaunchPath.clear();
    return pending;
}

static void relaunchEmulatorAfterExit(void)
{
    requestRelaunchAfterExit(g_currentAppPath);
}

static void openTextFileNearExe(const wchar_t* fileName)
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        path.resize(slash + 1);
    }
    path += fileName;

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        MessageBoxW(g_menuWindow,
            uiText(TXT_DEBUG_LOG_MISSING_BODY),
            uiText(TXT_DEBUG_LOG_MISSING_TITLE),
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

static bool hasExtensionIgnoreCase(const std::wstring& path, const wchar_t* extension)
{
    size_t extLen = wcslen(extension);
    if (path.size() < extLen)
    {
        return false;
    }
    return _wcsicmp(path.c_str() + path.size() - extLen, extension) == 0;
}

static std::wstring screenshotExtensionForFilter(DWORD filterIndex)
{
    if (filterIndex == 2)
    {
        return L".jpg";
    }
    if (filterIndex == 3)
    {
        return L".bmp";
    }
    return L".png";
}

static void ensureScreenshotExtension(std::wstring* path, DWORD filterIndex)
{
    if (!path)
    {
        return;
    }
    if (hasExtensionIgnoreCase(*path, L".png") ||
        hasExtensionIgnoreCase(*path, L".jpg") ||
        hasExtensionIgnoreCase(*path, L".jpeg") ||
        hasExtensionIgnoreCase(*path, L".bmp"))
    {
        return;
    }
    *path += screenshotExtensionForFilter(filterIndex);
}

static void buildTimestampedScreenshotName(wchar_t* buffer, size_t bufferCount)
{
    if (!buffer || bufferCount == 0)
    {
        return;
    }

    SYSTEMTIME now;
    GetLocalTime(&now);
    swprintf(buffer, bufferCount, L"dingoo-screenshot-%04u%02u%02u-%02u%02u%02u",
        (unsigned int)now.wYear,
        (unsigned int)now.wMonth,
        (unsigned int)now.wDay,
        (unsigned int)now.wHour,
        (unsigned int)now.wMinute,
        (unsigned int)now.wSecond);
}

static void saveScreenshotWithDialog(void)
{
    wchar_t fileName[MAX_PATH] = {};
    buildTimestampedScreenshotName(fileName, MAX_PATH);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_menuWindow;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = uiText(TXT_DIALOG_SAVE_FILTER);
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = uiText(TXT_DIALOG_SAVE_TITLE);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn))
    {
        return;
    }

    std::wstring path(fileName);
    ensureScreenshotExtension(&path, ofn.nFilterIndex);
    bool ok = frontendSaveScreenshot(platformWideToUtf8(path).c_str());
    MessageBoxW(g_menuWindow,
        ok ? uiText(TXT_DIALOG_SCREENSHOT_SAVED) : uiText(TXT_DIALOG_SCREENSHOT_FAILED),
        L"DingooPie",
        ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
}

static std::wstring recentAppMenuLabel(size_t index, const std::string& appPath)
{
    std::string fileName = appFileNameFromPath(appPath);
    std::wstring label = std::to_wstring((unsigned int)(index + 1));
    label += L". ";
    label += platformUtf8ToWide(fileName.empty() ? appPath : fileName);
    return label;
}

static void populateRecentMenu(HMENU recentMenu)
{
    if (g_menuSettings && !g_menuSettings->recentAppPaths.empty())
    {
        size_t recentCount = g_menuSettings->recentAppPaths.size();
        if (recentCount > EMULATOR_RECENT_APP_LIMIT)
        {
            recentCount = EMULATOR_RECENT_APP_LIMIT;
        }
        for (size_t i = 0; i < recentCount; ++i)
        {
            std::wstring label = recentAppMenuLabel(i, g_menuSettings->recentAppPaths[i]);
            appendMenuItem(recentMenu, MENU_FILE_RECENT_BASE + (UINT)i, label.c_str());
        }
        AppendMenuW(recentMenu, MF_SEPARATOR, 0, NULL);
        appendMenuItem(recentMenu, MENU_FILE_RECENT_CLEAR, uiText(TXT_FILE_RECENT_CLEAR));
        return;
    }

    appendDisabledMenuItem(recentMenu, uiText(TXT_FILE_RECENT_EMPTY));
}
#endif

void frontendMenuAttach(void* nativeWindow, EmulatorSettings* settings, const std::string& currentAppPath)
{
    g_menuSettings = settings;
    g_currentAppPath = currentAppPath;
#ifdef _WIN32
    g_menuWindow = (HWND)nativeWindow;
    if (!g_menuWindow)
    {
        return;
    }

    HMENU menuBar = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    HMENU recentMenu = CreatePopupMenu();
    HMENU optionsMenu = CreatePopupMenu();
    HMENU videoMenu = CreatePopupMenu();
    HMENU scaleMenu = CreatePopupMenu();
    HMENU antiAliasingMenu = CreatePopupMenu();
    HMENU effectMenu = CreatePopupMenu();
    HMENU brightnessMenu = CreatePopupMenu();
    HMENU contrastMenu = CreatePopupMenu();
    HMENU saturationMenu = CreatePopupMenu();
    HMENU minimizedMenu = CreatePopupMenu();
    HMENU settingsMenu = CreatePopupMenu();
    HMENU backendMenu = CreatePopupMenu();
    HMENU cpuClockMenu = CreatePopupMenu();
    HMENU speedMenu = CreatePopupMenu();
    HMENU delayMenu = CreatePopupMenu();
    HMENU audioMenu = CreatePopupMenu();
    HMENU audioVolumeMenu = CreatePopupMenu();
    HMENU audioBufferMenu = CreatePopupMenu();
    HMENU languageMenu = CreatePopupMenu();
    HMENU inputMenu = CreatePopupMenu();
    HMENU debugMenu = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();

    appendMenuItem(fileMenu, MENU_FILE_OPEN, uiText(TXT_FILE_OPEN));
    populateRecentMenu(recentMenu);
    AppendMenuW(fileMenu, MF_POPUP, (UINT_PTR)recentMenu, uiText(TXT_FILE_RECENT));
    appendMenuItem(fileMenu, MENU_FILE_RESTART, uiText(TXT_FILE_RESTART));
    appendMenuItem(fileMenu, MENU_FILE_PAUSE_RESUME, uiText(TXT_FILE_PAUSE));
    appendMenuItem(fileMenu, MENU_FILE_SAVE_SCREENSHOT, uiText(TXT_FILE_SAVE_SCREENSHOT));
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
    appendMenuItem(fileMenu, MENU_FILE_EXIT, uiText(TXT_FILE_EXIT));

    appendMenuItem(scaleMenu, MENU_VIDEO_SCALE_1X, L"1x");
    appendMenuItem(scaleMenu, MENU_VIDEO_SCALE_2X, L"2x");
    appendMenuItem(scaleMenu, MENU_VIDEO_SCALE_3X, L"3x");
    AppendMenuW(scaleMenu, MF_SEPARATOR, 0, NULL);
    appendMenuItem(scaleMenu, MENU_VIDEO_FULLSCREEN, uiText(TXT_VIDEO_FULLSCREEN));
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)scaleMenu, uiText(TXT_VIDEO_SCALE));

    appendMenuItem(antiAliasingMenu, MENU_VIDEO_AA_OFF, uiText(TXT_VIDEO_AA_OFF));
    appendMenuItem(antiAliasingMenu, MENU_VIDEO_AA_LOW, uiText(TXT_VIDEO_AA_LOW));
    appendMenuItem(antiAliasingMenu, MENU_VIDEO_AA_CLEAR, uiText(TXT_VIDEO_AA_CLEAR));
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)antiAliasingMenu, uiText(TXT_VIDEO_ANTI_ALIASING));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_NORMAL, uiText(TXT_VIDEO_EFFECT_NORMAL));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_GRAYSCALE, uiText(TXT_VIDEO_EFFECT_GRAYSCALE));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_INVERT, uiText(TXT_VIDEO_EFFECT_INVERT));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_SOFT_BLUR, uiText(TXT_VIDEO_EFFECT_SOFT_BLUR));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_SHARPEN, uiText(TXT_VIDEO_EFFECT_SHARPEN));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_VIVID, uiText(TXT_VIDEO_EFFECT_VIVID));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_SEPIA, uiText(TXT_VIDEO_EFFECT_SEPIA));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_PIXEL_GRID, uiText(TXT_VIDEO_EFFECT_PIXEL_GRID));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_LCD_SCANLINE, uiText(TXT_VIDEO_EFFECT_LCD_SCANLINE));
    appendMenuItem(effectMenu, MENU_VIDEO_EFFECT_LIGHT_CRT, uiText(TXT_VIDEO_EFFECT_LIGHT_CRT));
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)effectMenu, uiText(TXT_VIDEO_EFFECT));
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_75, L"75%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_90, L"90%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_100, L"100%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_110, L"110%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_125, L"125%");
    appendMenuItem(brightnessMenu, MENU_VIDEO_BRIGHTNESS_150, L"150%");
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)brightnessMenu, uiText(TXT_VIDEO_BRIGHTNESS));
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_75, L"75%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_90, L"90%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_100, L"100%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_110, L"110%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_125, L"125%");
    appendMenuItem(contrastMenu, MENU_VIDEO_CONTRAST_150, L"150%");
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)contrastMenu, uiText(TXT_VIDEO_CONTRAST));
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_75, L"75%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_90, L"90%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_100, L"100%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_110, L"110%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_125, L"125%");
    appendMenuItem(saturationMenu, MENU_VIDEO_SATURATION_150, L"150%");
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)saturationMenu, uiText(TXT_VIDEO_SATURATION));
    for (size_t i = 0; i < sizeof(kMinimizedBehaviorPresets) / sizeof(kMinimizedBehaviorPresets[0]); ++i)
    {
        appendMenuItem(minimizedMenu, kMinimizedBehaviorPresets[i].commandId,
            uiText(kMinimizedBehaviorPresets[i].labelId));
    }
    AppendMenuW(videoMenu, MF_POPUP, (UINT_PTR)minimizedMenu, uiText(TXT_VIDEO_MINIMIZED_BEHAVIOR));
    AppendMenuW(videoMenu, MF_SEPARATOR, 0, NULL);
    appendMenuItem(videoMenu, MENU_VIDEO_PORTRAIT, uiText(TXT_VIDEO_PORTRAIT));
    appendMenuItem(videoMenu, MENU_VIDEO_SHOW_FPS, uiText(TXT_VIDEO_SHOW_FPS));

    appendMenuItem(audioVolumeMenu, MENU_SETTINGS_AUDIO_VOLUME_0, L"0%");
    appendMenuItem(audioVolumeMenu, MENU_SETTINGS_AUDIO_VOLUME_25, L"25%");
    appendMenuItem(audioVolumeMenu, MENU_SETTINGS_AUDIO_VOLUME_50, L"50%");
    appendMenuItem(audioVolumeMenu, MENU_SETTINGS_AUDIO_VOLUME_75, L"75%");
    appendMenuItem(audioVolumeMenu, MENU_SETTINGS_AUDIO_VOLUME_100, L"100%");
    appendMenuItem(audioVolumeMenu, MENU_SETTINGS_AUDIO_VOLUME_125, L"125%");
    appendMenuItem(audioVolumeMenu, MENU_SETTINGS_AUDIO_VOLUME_150, L"150%");
    appendMenuItem(audioBufferMenu, MENU_SETTINGS_AUDIO_BUFFER_512, audioBufferLabel(512).c_str());
    appendMenuItem(audioBufferMenu, MENU_SETTINGS_AUDIO_BUFFER_1024, audioBufferLabel(1024).c_str());
    appendMenuItem(audioBufferMenu, MENU_SETTINGS_AUDIO_BUFFER_2048, audioBufferLabel(2048).c_str());
    appendMenuItem(audioBufferMenu, MENU_SETTINGS_AUDIO_BUFFER_4096, audioBufferLabel(4096).c_str());
    appendMenuItem(audioBufferMenu, MENU_SETTINGS_AUDIO_BUFFER_8192, audioBufferLabel(8192).c_str());
    appendMenuItem(inputMenu, MENU_INPUT_DISABLE_IME, uiText(TXT_INPUT_DISABLE_IME));
    appendMenuItem(inputMenu, MENU_INPUT_SHOW_VIRTUAL_CONTROLS, uiText(TXT_INPUT_VIRTUAL_CONTROLS));
    appendMenuItem(inputMenu, MENU_INPUT_MAPPING_WINDOW, uiText(TXT_INPUT_MAPPING_WINDOW));

    AppendMenuW(audioMenu, MF_POPUP, (UINT_PTR)audioVolumeMenu, uiText(TXT_SETTINGS_AUDIO_VOLUME));
    AppendMenuW(audioMenu, MF_POPUP, (UINT_PTR)audioBufferMenu, uiText(TXT_SETTINGS_AUDIO_BUFFER));
    appendMenuItem(audioMenu, MENU_SETTINGS_DROP_AUDIO, uiText(TXT_SETTINGS_DROP_AUDIO));

    AppendMenuW(optionsMenu, MF_POPUP, (UINT_PTR)videoMenu, uiText(TXT_ROOT_VIDEO));
    AppendMenuW(optionsMenu, MF_POPUP, (UINT_PTR)audioMenu, uiText(TXT_ROOT_AUDIO));
    AppendMenuW(optionsMenu, MF_POPUP, (UINT_PTR)inputMenu, uiText(TXT_ROOT_INPUT));

    // Keep this persistent settings order aligned with emulatorSaveSettings().
    // Menu selections save immediately; there is intentionally no manual Save item.
    appendMenuItem(backendMenu, MENU_SETTINGS_BACKEND_AUTO, uiText(TXT_SETTINGS_BACKEND_AUTO));
    appendMenuItem(backendMenu, MENU_SETTINGS_BACKEND_IRJIT, uiText(TXT_SETTINGS_BACKEND_IRJIT));
    appendMenuItem(backendMenu, MENU_SETTINGS_BACKEND_INTERPRETER, uiText(TXT_SETTINGS_BACKEND_INTERPRETER));
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)backendMenu, uiText(TXT_SETTINGS_CPU_BACKEND));
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_AUTO, uiText(TXT_SETTINGS_SPEED_AUTO));
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_200, L"200 MHz");
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_336, L"336 MHz");
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_370, L"370 MHz");
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_400, L"400 MHz");
    appendMenuItem(cpuClockMenu, MENU_SETTINGS_CPU_CLOCK_430, L"430 MHz");
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)cpuClockMenu, uiText(TXT_SETTINGS_CPU_CLOCK));
    appendMenuItem(speedMenu, MENU_SETTINGS_SPEED_AUTO, uiText(TXT_SETTINGS_SPEED_AUTO));
    for (size_t i = 0; i < sizeof(kRuntimeSpeedPresets) / sizeof(kRuntimeSpeedPresets[0]); ++i)
    {
        appendScalePreset(speedMenu, kRuntimeSpeedPresets[i]);
    }
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)speedMenu, uiText(TXT_SETTINGS_RUNTIME_SPEED));

    appendMenuItem(delayMenu, MENU_SETTINGS_DELAY_AUTO, uiText(TXT_SETTINGS_DELAY_AUTO));
    for (size_t i = 0; i < sizeof(kDelayScalePresets) / sizeof(kDelayScalePresets[0]); ++i)
    {
        appendScalePreset(delayMenu, kDelayScalePresets[i]);
    }
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)delayMenu, uiText(TXT_SETTINGS_DELAY));
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, NULL);

    appendMenuItem(languageMenu, MENU_SETTINGS_LANGUAGE_CHINESE, L"\u4e2d\u6587");
    appendMenuItem(languageMenu, MENU_SETTINGS_LANGUAGE_ENGLISH, L"English");
    AppendMenuW(settingsMenu, MF_POPUP, (UINT_PTR)languageMenu, uiText(TXT_SETTINGS_LANGUAGE));
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, NULL);

    appendMenuItem(settingsMenu, MENU_SETTINGS_RESET, uiText(TXT_SETTINGS_RESET));

    appendMenuItem(debugMenu, MENU_DEBUG_SHOW_CONSOLE, uiText(TXT_DEBUG_CONSOLE));
    appendMenuItem(debugMenu, MENU_DEBUG_PROFILE, uiText(TXT_DEBUG_PROFILE));
    appendMenuItem(debugMenu, MENU_DEBUG_OPEN_LOG, uiText(TXT_DEBUG_OPEN_LOG));

    appendMenuItem(helpMenu, MENU_HELP_ABOUT, uiText(TXT_HELP_ABOUT));

    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)fileMenu, uiText(TXT_ROOT_FILE));
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)optionsMenu, uiText(TXT_ROOT_OPTIONS));
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)settingsMenu, uiText(TXT_ROOT_SETTINGS));
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)debugMenu, uiText(TXT_ROOT_DEBUG));
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)helpMenu, uiText(TXT_ROOT_HELP));

    SetMenu(g_menuWindow, menuBar);
    DrawMenuBar(g_menuWindow);
#endif
    frontendMenuRefresh();
}

static void rebuildMenu(void)
{
#ifdef _WIN32
    if (!g_menuWindow)
    {
        return;
    }
    HMENU oldMenu = GetMenu(g_menuWindow);
    SetMenu(g_menuWindow, NULL);
    if (oldMenu)
    {
        DestroyMenu(oldMenu);
    }
    frontendMenuAttach(g_menuWindow, g_menuSettings, g_currentAppPath);
#endif
}

void frontendMenuRefresh(void)
{
    if (!g_menuSettings)
    {
        return;
    }
#ifdef _WIN32
    const bool hasCurrentApp = !g_currentAppPath.empty();
    const bool gamePaused = frontendGamePaused();
    setMenuEnabled(MENU_FILE_RESTART, hasCurrentApp);
    setMenuEnabled(MENU_FILE_PAUSE_RESUME, hasCurrentApp && g_gameRunning);
    setMenuText(MENU_FILE_PAUSE_RESUME, uiText(gamePaused ? TXT_FILE_RESUME : TXT_FILE_PAUSE));
    setMenuEnabled(MENU_FILE_SAVE_SCREENSHOT, hasCurrentApp && g_gameRunning);
    setMenuCheck(MENU_VIDEO_SCALE_1X, g_menuSettings->windowScale == 1);
    setMenuCheck(MENU_VIDEO_SCALE_2X, g_menuSettings->windowScale == 2);
    setMenuCheck(MENU_VIDEO_SCALE_3X, g_menuSettings->windowScale == 3);
    setMenuCheck(MENU_VIDEO_FULLSCREEN, g_menuSettings->fullscreen);
    setMenuCheck(MENU_VIDEO_AA_OFF, g_menuSettings->antiAliasing == ANTI_ALIASING_OFF);
    setMenuCheck(MENU_VIDEO_AA_LOW, g_menuSettings->antiAliasing == ANTI_ALIASING_LOW);
    setMenuCheck(MENU_VIDEO_AA_CLEAR, g_menuSettings->antiAliasing == ANTI_ALIASING_CLEAR);
    setMenuCheck(MENU_VIDEO_EFFECT_NORMAL, g_menuSettings->colorEffect == COLOR_EFFECT_NORMAL);
    setMenuCheck(MENU_VIDEO_EFFECT_GRAYSCALE, g_menuSettings->colorEffect == COLOR_EFFECT_GRAYSCALE);
    setMenuCheck(MENU_VIDEO_EFFECT_INVERT, g_menuSettings->colorEffect == COLOR_EFFECT_INVERT);
    setMenuCheck(MENU_VIDEO_EFFECT_SOFT_BLUR, g_menuSettings->colorEffect == COLOR_EFFECT_SOFT_BLUR);
    setMenuCheck(MENU_VIDEO_EFFECT_SHARPEN, g_menuSettings->colorEffect == COLOR_EFFECT_SHARPEN);
    setMenuCheck(MENU_VIDEO_EFFECT_VIVID, g_menuSettings->colorEffect == COLOR_EFFECT_VIVID);
    setMenuCheck(MENU_VIDEO_EFFECT_SEPIA, g_menuSettings->colorEffect == COLOR_EFFECT_SEPIA);
    setMenuCheck(MENU_VIDEO_EFFECT_PIXEL_GRID, g_menuSettings->colorEffect == COLOR_EFFECT_PIXEL_GRID);
    setMenuCheck(MENU_VIDEO_EFFECT_LCD_SCANLINE, g_menuSettings->colorEffect == COLOR_EFFECT_LCD_SCANLINE);
    setMenuCheck(MENU_VIDEO_EFFECT_LIGHT_CRT, g_menuSettings->colorEffect == COLOR_EFFECT_LIGHT_CRT);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_75, g_menuSettings->brightnessPercent == 75);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_90, g_menuSettings->brightnessPercent == 90);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_100, g_menuSettings->brightnessPercent == 100);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_110, g_menuSettings->brightnessPercent == 110);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_125, g_menuSettings->brightnessPercent == 125);
    setMenuCheck(MENU_VIDEO_BRIGHTNESS_150, g_menuSettings->brightnessPercent == 150);
    setMenuCheck(MENU_VIDEO_CONTRAST_75, g_menuSettings->contrastPercent == 75);
    setMenuCheck(MENU_VIDEO_CONTRAST_90, g_menuSettings->contrastPercent == 90);
    setMenuCheck(MENU_VIDEO_CONTRAST_100, g_menuSettings->contrastPercent == 100);
    setMenuCheck(MENU_VIDEO_CONTRAST_110, g_menuSettings->contrastPercent == 110);
    setMenuCheck(MENU_VIDEO_CONTRAST_125, g_menuSettings->contrastPercent == 125);
    setMenuCheck(MENU_VIDEO_CONTRAST_150, g_menuSettings->contrastPercent == 150);
    setMenuCheck(MENU_VIDEO_SATURATION_75, g_menuSettings->saturationPercent == 75);
    setMenuCheck(MENU_VIDEO_SATURATION_90, g_menuSettings->saturationPercent == 90);
    setMenuCheck(MENU_VIDEO_SATURATION_100, g_menuSettings->saturationPercent == 100);
    setMenuCheck(MENU_VIDEO_SATURATION_110, g_menuSettings->saturationPercent == 110);
    setMenuCheck(MENU_VIDEO_SATURATION_125, g_menuSettings->saturationPercent == 125);
    setMenuCheck(MENU_VIDEO_SATURATION_150, g_menuSettings->saturationPercent == 150);
    for (size_t i = 0; i < sizeof(kMinimizedBehaviorPresets) / sizeof(kMinimizedBehaviorPresets[0]); ++i)
    {
        setMenuCheck(kMinimizedBehaviorPresets[i].commandId,
            g_menuSettings->minimizedBehavior == kMinimizedBehaviorPresets[i].behavior);
    }
    setMenuCheck(MENU_VIDEO_PORTRAIT, g_menuSettings->portraitMode);
    setMenuCheck(MENU_VIDEO_SHOW_FPS, g_menuSettings->showFps);
    setMenuCheck(MENU_SETTINGS_AUDIO_VOLUME_0, g_menuSettings->audioVolumePercent == 0);
    setMenuCheck(MENU_SETTINGS_AUDIO_VOLUME_25, g_menuSettings->audioVolumePercent == 25);
    setMenuCheck(MENU_SETTINGS_AUDIO_VOLUME_50, g_menuSettings->audioVolumePercent == 50);
    setMenuCheck(MENU_SETTINGS_AUDIO_VOLUME_75, g_menuSettings->audioVolumePercent == 75);
    setMenuCheck(MENU_SETTINGS_AUDIO_VOLUME_100, g_menuSettings->audioVolumePercent == 100);
    setMenuCheck(MENU_SETTINGS_AUDIO_VOLUME_125, g_menuSettings->audioVolumePercent == 125);
    setMenuCheck(MENU_SETTINGS_AUDIO_VOLUME_150, g_menuSettings->audioVolumePercent == 150);
    setMenuCheck(MENU_SETTINGS_AUDIO_BUFFER_512, g_menuSettings->audioBufferSamples == 512);
    setMenuCheck(MENU_SETTINGS_AUDIO_BUFFER_1024, g_menuSettings->audioBufferSamples == 1024);
    setMenuCheck(MENU_SETTINGS_AUDIO_BUFFER_2048, g_menuSettings->audioBufferSamples == 2048);
    setMenuCheck(MENU_SETTINGS_AUDIO_BUFFER_4096, g_menuSettings->audioBufferSamples == 4096);
    setMenuCheck(MENU_SETTINGS_AUDIO_BUFFER_8192, g_menuSettings->audioBufferSamples == 8192);
    setMenuCheck(MENU_SETTINGS_DROP_AUDIO, g_menuSettings->dropAudio);
    setMenuCheck(MENU_INPUT_DISABLE_IME, g_menuSettings->disableIme);
    setMenuCheck(MENU_INPUT_SHOW_VIRTUAL_CONTROLS, g_menuSettings->showVirtualControls);
    setMenuCheck(MENU_SETTINGS_BACKEND_AUTO, g_menuSettings->backendName.empty());
    setMenuCheck(MENU_SETTINGS_BACKEND_IRJIT, g_menuSettings->backendName == "ppsspp_irjit");
    setMenuCheck(MENU_SETTINGS_BACKEND_INTERPRETER, g_menuSettings->backendName == "interpreter");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_AUTO, g_menuSettings->cpuClockHz.empty());
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_200, g_menuSettings->cpuClockHz == "200000000");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_336, g_menuSettings->cpuClockHz == "336000000");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_370, g_menuSettings->cpuClockHz == "370000000");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_400, g_menuSettings->cpuClockHz == "400000000");
    setMenuCheck(MENU_SETTINGS_CPU_CLOCK_430, g_menuSettings->cpuClockHz == "430000000");
    setMenuCheck(MENU_SETTINGS_SPEED_AUTO, g_menuSettings->runtimeSpeedScale.empty());
    const ScalePreset* checkedSpeed = runtimeSpeedPresetForValue(g_menuSettings->runtimeSpeedScale);
    for (size_t i = 0; i < sizeof(kRuntimeSpeedPresets) / sizeof(kRuntimeSpeedPresets[0]); ++i)
    {
        setMenuCheck(kRuntimeSpeedPresets[i].commandId,
            checkedSpeed && checkedSpeed->commandId == kRuntimeSpeedPresets[i].commandId);
    }
    setMenuCheck(MENU_SETTINGS_DELAY_AUTO, g_menuSettings->ostimeDlyScale.empty());
    const ScalePreset* checkedDelay = delayScalePresetForValue(g_menuSettings->ostimeDlyScale);
    for (size_t i = 0; i < sizeof(kDelayScalePresets) / sizeof(kDelayScalePresets[0]); ++i)
    {
        setMenuCheck(kDelayScalePresets[i].commandId,
            checkedDelay && checkedDelay->commandId == kDelayScalePresets[i].commandId);
    }
    setMenuCheck(MENU_SETTINGS_LANGUAGE_ENGLISH, g_menuSettings->uiLanguage == UI_LANGUAGE_ENGLISH);
    setMenuCheck(MENU_SETTINGS_LANGUAGE_CHINESE, g_menuSettings->uiLanguage == UI_LANGUAGE_CHINESE);
    setMenuCheck(MENU_DEBUG_SHOW_CONSOLE, g_menuSettings->showDebugConsole);
    setMenuCheck(MENU_DEBUG_PROFILE, g_menuSettings->debugProfile);
#endif
}

void frontendMenuSetGameRunning(bool running)
{
    if (g_gameRunning == running)
    {
        return;
    }
    g_gameRunning = running;
    if (!running)
    {
        frontendSetGamePaused(false);
    }
    frontendMenuRefresh();
}

static bool handleRecentAppCommand(unsigned int commandId)
{
    if (commandId < MENU_FILE_RECENT_BASE || commandId > MENU_FILE_RECENT_MAX)
    {
        return false;
    }

    size_t index = commandId - MENU_FILE_RECENT_BASE;
    if (index >= g_menuSettings->recentAppPaths.size())
    {
        return true;
    }

    std::string appPath = g_menuSettings->recentAppPaths[index];
    if (!validateAppPathForOpen(appPath, "recent"))
    {
        if (emulatorRemoveRecentApp(g_menuSettings, appPath))
        {
            emulatorSaveSettings(*g_menuSettings);
            rebuildMenu();
        }
        return true;
    }

    frontendMenuRequestOpenApp(appPath);
    return true;
}

static void clearRecentAppMenu(void)
{
    if (emulatorClearRecentApps(g_menuSettings))
    {
        emulatorSaveSettings(*g_menuSettings);
        rebuildMenu();
    }
}

bool frontendMenuHandleCommand(unsigned int commandId)
{
    if (!g_menuSettings)
    {
        return false;
    }

    if (handleRecentAppCommand(commandId))
    {
        return true;
    }

    switch (commandId)
    {
    case MENU_FILE_RECENT_CLEAR:
        clearRecentAppMenu();
        return true;
    case MENU_FILE_OPEN:
    {
        std::string appPath = platformSelectAppPathLocalized(uiText(TXT_DIALOG_APP_TITLE), uiText(TXT_DIALOG_APP_FILTER));
        if (!appPath.empty())
        {
            printf("frontend: selected app queued as recent app after normal exit: %s\n", appPath.c_str());
            frontendMenuRequestOpenApp(appPath);
        }
        return true;
    }
    case MENU_FILE_SAVE_SCREENSHOT:
#ifdef _WIN32
        if (!g_currentAppPath.empty() && g_gameRunning)
        {
            saveScreenshotWithDialog();
        }
#endif
        return true;
    case MENU_FILE_PAUSE_RESUME:
        if (!g_currentAppPath.empty() && g_gameRunning)
        {
            frontendToggleGamePaused();
        }
        return true;
    case MENU_FILE_RESTART:
#ifdef _WIN32
        if (!g_currentAppPath.empty())
        {
            printf("frontend: restarting current app: %s\n", g_currentAppPath.c_str());
            requestRelaunchAfterExit(g_currentAppPath);
        }
#endif
        return true;
    case MENU_FILE_EXIT:
        frontendRequestQuit();
        return true;
    case MENU_VIDEO_SCALE_1X:
    case MENU_VIDEO_SCALE_2X:
    case MENU_VIDEO_SCALE_3X:
        g_menuSettings->windowScale = (int)(commandId - MENU_VIDEO_SCALE_1X + 1);
        g_menuSettings->fullscreen = false;
        applyAndSaveVideoSettings();
        printf("frontend: window scale setting applied as %dx\n",
            g_menuSettings->windowScale);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_FULLSCREEN:
        g_menuSettings->fullscreen = !g_menuSettings->fullscreen;
        applyAndSaveVideoSettings();
        printf("frontend: fullscreen setting applied as %u\n",
            g_menuSettings->fullscreen ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_AA_OFF:
        g_menuSettings->antiAliasing = ANTI_ALIASING_OFF;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_AA_LOW:
        g_menuSettings->antiAliasing = ANTI_ALIASING_LOW;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_AA_CLEAR:
        g_menuSettings->antiAliasing = ANTI_ALIASING_CLEAR;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_NORMAL:
        g_menuSettings->colorEffect = COLOR_EFFECT_NORMAL;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_GRAYSCALE:
        g_menuSettings->colorEffect = COLOR_EFFECT_GRAYSCALE;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_INVERT:
        g_menuSettings->colorEffect = COLOR_EFFECT_INVERT;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_SOFT_BLUR:
        g_menuSettings->colorEffect = COLOR_EFFECT_SOFT_BLUR;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_SHARPEN:
        g_menuSettings->colorEffect = COLOR_EFFECT_SHARPEN;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_VIVID:
        g_menuSettings->colorEffect = COLOR_EFFECT_VIVID;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_SEPIA:
        g_menuSettings->colorEffect = COLOR_EFFECT_SEPIA;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_PIXEL_GRID:
        g_menuSettings->colorEffect = COLOR_EFFECT_PIXEL_GRID;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_LCD_SCANLINE:
        g_menuSettings->colorEffect = COLOR_EFFECT_LCD_SCANLINE;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_EFFECT_LIGHT_CRT:
        g_menuSettings->colorEffect = COLOR_EFFECT_LIGHT_CRT;
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_BRIGHTNESS_75:
    case MENU_VIDEO_BRIGHTNESS_90:
    case MENU_VIDEO_BRIGHTNESS_100:
    case MENU_VIDEO_BRIGHTNESS_110:
    case MENU_VIDEO_BRIGHTNESS_125:
    case MENU_VIDEO_BRIGHTNESS_150:
        g_menuSettings->brightnessPercent = percentForVideoCommand(commandId, MENU_VIDEO_BRIGHTNESS_75);
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_CONTRAST_75:
    case MENU_VIDEO_CONTRAST_90:
    case MENU_VIDEO_CONTRAST_100:
    case MENU_VIDEO_CONTRAST_110:
    case MENU_VIDEO_CONTRAST_125:
    case MENU_VIDEO_CONTRAST_150:
        g_menuSettings->contrastPercent = percentForVideoCommand(commandId, MENU_VIDEO_CONTRAST_75);
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_SATURATION_75:
    case MENU_VIDEO_SATURATION_90:
    case MENU_VIDEO_SATURATION_100:
    case MENU_VIDEO_SATURATION_110:
    case MENU_VIDEO_SATURATION_125:
    case MENU_VIDEO_SATURATION_150:
        g_menuSettings->saturationPercent = percentForVideoCommand(commandId, MENU_VIDEO_SATURATION_75);
        frontendApplyVideoSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_MINIMIZED_NORMAL:
    case MENU_VIDEO_MINIMIZED_THROTTLE:
    case MENU_VIDEO_MINIMIZED_PAUSE:
    {
        const MinimizedBehaviorPreset* preset = minimizedBehaviorPresetForCommand(commandId);
        if (preset)
        {
            g_menuSettings->minimizedBehavior = preset->behavior;
            emulatorSaveSettings(*g_menuSettings);
            frontendMenuRefresh();
        }
        return true;
    }
    case MENU_VIDEO_PORTRAIT:
        g_menuSettings->portraitMode = !g_menuSettings->portraitMode;
        applyAndSaveVideoSettings();
        printf("frontend: portrait mode setting applied as %u\n",
            g_menuSettings->portraitMode ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_VIDEO_SHOW_FPS:
        g_menuSettings->showFps = !g_menuSettings->showFps;
        applyAndSaveVideoSettings();
        printf("frontend: FPS overlay setting applied as %u\n",
            g_menuSettings->showFps ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_SETTINGS_AUDIO_VOLUME_0:
    case MENU_SETTINGS_AUDIO_VOLUME_25:
    case MENU_SETTINGS_AUDIO_VOLUME_50:
    case MENU_SETTINGS_AUDIO_VOLUME_75:
    case MENU_SETTINGS_AUDIO_VOLUME_100:
    case MENU_SETTINGS_AUDIO_VOLUME_125:
    case MENU_SETTINGS_AUDIO_VOLUME_150:
        g_menuSettings->audioVolumePercent = percentForAudioVolumeCommand(commandId);
        frontendApplyAudioSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: master volume setting saved as %d%%\n", g_menuSettings->audioVolumePercent);
        frontendMenuRefresh();
        return true;
    case MENU_SETTINGS_AUDIO_BUFFER_512:
    case MENU_SETTINGS_AUDIO_BUFFER_1024:
    case MENU_SETTINGS_AUDIO_BUFFER_2048:
    case MENU_SETTINGS_AUDIO_BUFFER_4096:
    case MENU_SETTINGS_AUDIO_BUFFER_8192:
        if (commandId == MENU_SETTINGS_AUDIO_BUFFER_512)
        {
            g_menuSettings->audioBufferSamples = 512;
        }
        else if (commandId == MENU_SETTINGS_AUDIO_BUFFER_1024)
        {
            g_menuSettings->audioBufferSamples = 1024;
        }
        else if (commandId == MENU_SETTINGS_AUDIO_BUFFER_2048)
        {
            g_menuSettings->audioBufferSamples = 2048;
        }
        else if (commandId == MENU_SETTINGS_AUDIO_BUFFER_4096)
        {
            g_menuSettings->audioBufferSamples = 4096;
        }
        else
        {
            g_menuSettings->audioBufferSamples = 8192;
        }
        frontendApplyAudioSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: audio buffer setting saved as %d samples\n", g_menuSettings->audioBufferSamples);
        frontendMenuRefresh();
        return true;
    case MENU_SETTINGS_DROP_AUDIO:
        g_menuSettings->dropAudio = !g_menuSettings->dropAudio;
        emulatorApplyRuntimeSettings(*g_menuSettings);
        frontendApplyAudioSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: disable-audio setting applied as %u\n",
            g_menuSettings->dropAudio ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_INPUT_DISABLE_IME:
        g_menuSettings->disableIme = !g_menuSettings->disableIme;
        frontendApplyInputSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: disable IME setting saved as %u\n",
            g_menuSettings->disableIme ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_INPUT_SHOW_VIRTUAL_CONTROLS:
        g_menuSettings->showVirtualControls = !g_menuSettings->showVirtualControls;
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: virtual controls overlay %s\n",
            g_menuSettings->showVirtualControls ? "enabled" : "disabled");
        frontendMenuRefresh();
        return true;
    case MENU_INPUT_MAPPING_WINDOW:
        frontendOpenInputMappingWindow();
        return true;
    case MENU_SETTINGS_BACKEND_AUTO:
        g_menuSettings->backendName = "";
        emulatorApplySettingsToEnvironment(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: backend setting saved as auto, relaunching emulator\n");
        frontendMenuRefresh();
#ifdef _WIN32
        relaunchEmulatorAfterExit();
#endif
        return true;
    case MENU_SETTINGS_BACKEND_IRJIT:
        g_menuSettings->backendName = "ppsspp_irjit";
        emulatorApplySettingsToEnvironment(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: backend setting saved as ppsspp_irjit, relaunching emulator\n");
        frontendMenuRefresh();
#ifdef _WIN32
        relaunchEmulatorAfterExit();
#endif
        return true;
    case MENU_SETTINGS_BACKEND_INTERPRETER:
        g_menuSettings->backendName = "interpreter";
        emulatorApplySettingsToEnvironment(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: backend setting saved as interpreter, relaunching emulator\n");
        frontendMenuRefresh();
#ifdef _WIN32
        relaunchEmulatorAfterExit();
#endif
        return true;
    case MENU_SETTINGS_CPU_CLOCK_AUTO:
    case MENU_SETTINGS_CPU_CLOCK_200:
    case MENU_SETTINGS_CPU_CLOCK_336:
    case MENU_SETTINGS_CPU_CLOCK_370:
    case MENU_SETTINGS_CPU_CLOCK_400:
    case MENU_SETTINGS_CPU_CLOCK_430:
        if (commandId == MENU_SETTINGS_CPU_CLOCK_AUTO)
        {
            g_menuSettings->cpuClockHz = "";
        }
        else if (commandId == MENU_SETTINGS_CPU_CLOCK_200)
        {
            g_menuSettings->cpuClockHz = "200000000";
        }
        else if (commandId == MENU_SETTINGS_CPU_CLOCK_336)
        {
            g_menuSettings->cpuClockHz = "336000000";
        }
        else if (commandId == MENU_SETTINGS_CPU_CLOCK_370)
        {
            g_menuSettings->cpuClockHz = "370000000";
        }
        else if (commandId == MENU_SETTINGS_CPU_CLOCK_400)
        {
            g_menuSettings->cpuClockHz = "400000000";
        }
        else
        {
            g_menuSettings->cpuClockHz = "430000000";
        }
        applyAndSaveRuntimeSettings();
        printf("frontend: CPU clock setting applied as '%s'\n",
            g_menuSettings->cpuClockHz.empty() ? "auto" : g_menuSettings->cpuClockHz.c_str());
        frontendMenuRefresh();
        return true;
    case MENU_SETTINGS_SPEED_AUTO:
    case MENU_SETTINGS_SPEED_100:
    case MENU_SETTINGS_SPEED_095:
    case MENU_SETTINGS_SPEED_090:
    case MENU_SETTINGS_SPEED_085:
    case MENU_SETTINGS_SPEED_080:
    case MENU_SETTINGS_SPEED_075:
    case MENU_SETTINGS_SPEED_070:
    case MENU_SETTINGS_SPEED_065:
    case MENU_SETTINGS_SPEED_060:
    case MENU_SETTINGS_SPEED_055:
    case MENU_SETTINGS_SPEED_050:
    case MENU_SETTINGS_SPEED_045:
    case MENU_SETTINGS_SPEED_040:
    case MENU_SETTINGS_SPEED_035:
    case MENU_SETTINGS_SPEED_030:
    case MENU_SETTINGS_SPEED_025:
    case MENU_SETTINGS_SPEED_020:
    {
        const ScalePreset* speedPreset = runtimeSpeedPresetForCommand(commandId);
        g_menuSettings->runtimeSpeedScale = speedPreset ? speedPreset->iniValue : "";
        applyAndSaveRuntimeSettings();
        printf("frontend: runtime speed scale setting applied as '%s'\n",
            g_menuSettings->runtimeSpeedScale.empty() ? "auto" : g_menuSettings->runtimeSpeedScale.c_str());
        frontendMenuRefresh();
        return true;
    }
    case MENU_SETTINGS_DELAY_AUTO:
    case MENU_SETTINGS_DELAY_100:
    case MENU_SETTINGS_DELAY_095:
    case MENU_SETTINGS_DELAY_090:
    case MENU_SETTINGS_DELAY_085:
    case MENU_SETTINGS_DELAY_080:
    case MENU_SETTINGS_DELAY_075:
    case MENU_SETTINGS_DELAY_070:
    case MENU_SETTINGS_DELAY_065:
    case MENU_SETTINGS_DELAY_060:
    case MENU_SETTINGS_DELAY_055:
    case MENU_SETTINGS_DELAY_050:
    case MENU_SETTINGS_DELAY_045:
    case MENU_SETTINGS_DELAY_040:
    case MENU_SETTINGS_DELAY_035:
    case MENU_SETTINGS_DELAY_030:
    case MENU_SETTINGS_DELAY_025:
    case MENU_SETTINGS_DELAY_020:
    {
        const ScalePreset* delayPreset = delayScalePresetForCommand(commandId);
        g_menuSettings->ostimeDlyScale = delayPreset ? delayPreset->iniValue : "";
        applyAndSaveRuntimeSettings();
        printf("frontend: OSTimeDly scale setting applied as '%s'\n",
            g_menuSettings->ostimeDlyScale.empty() ? "auto" : g_menuSettings->ostimeDlyScale.c_str());
        frontendMenuRefresh();
        return true;
    }
    case MENU_SETTINGS_LANGUAGE_ENGLISH:
    case MENU_SETTINGS_LANGUAGE_CHINESE:
        g_menuSettings->uiLanguage = commandId == MENU_SETTINGS_LANGUAGE_CHINESE ?
            UI_LANGUAGE_CHINESE : UI_LANGUAGE_ENGLISH;
        emulatorSaveSettings(*g_menuSettings);
        rebuildMenu();
        return true;
    case MENU_SETTINGS_RESET:
    {
        std::string oldBackendName = g_menuSettings->backendName;
        *g_menuSettings = emulatorDefaultSettings();
        emulatorSaveSettings(*g_menuSettings);
        emulatorApplyRuntimeSettings(*g_menuSettings);
        debugConsoleClose();
        frontendApplyVideoSettings(*g_menuSettings);
        frontendApplyAudioSettings(*g_menuSettings);
        frontendApplyInputSettings(*g_menuSettings);
        frontendMenuRefresh();
#ifdef _WIN32
        if (oldBackendName != g_menuSettings->backendName)
        {
            printf("frontend: backend reset from %s to %s, relaunching emulator\n",
                oldBackendName.c_str(),
                g_menuSettings->backendName.c_str());
            relaunchEmulatorAfterExit();
        }
#endif
        return true;
    }
    case MENU_DEBUG_SHOW_CONSOLE:
        g_menuSettings->showDebugConsole = !g_menuSettings->showDebugConsole;
        if (g_menuSettings->showDebugConsole)
        {
            debugConsoleOpen();
        }
        else
        {
            debugConsoleClose();
        }
        emulatorSaveSettings(*g_menuSettings);
        frontendMenuRefresh();
        return true;
    case MENU_DEBUG_PROFILE:
        g_menuSettings->debugProfile = !g_menuSettings->debugProfile;
        if (g_menuSettings->debugProfile)
        {
            debugLogOpen();
        }
        emulatorApplyRuntimeSettings(*g_menuSettings);
        emulatorSaveSettings(*g_menuSettings);
        printf("frontend: profile setting applied as %u\n",
            g_menuSettings->debugProfile ? 1u : 0u);
        frontendMenuRefresh();
        return true;
    case MENU_DEBUG_OPEN_LOG:
#ifdef _WIN32
        openTextFileNearExe(L"DingooPie-debug.log");
#endif
        return true;
    case MENU_HELP_ABOUT:
#ifdef _WIN32
        MessageBoxW(g_menuWindow,
            uiText(TXT_ABOUT_BODY),
            uiText(TXT_ABOUT_TITLE),
            MB_OK | MB_ICONINFORMATION);
#endif
        return true;
    default:
        return false;
    }
}

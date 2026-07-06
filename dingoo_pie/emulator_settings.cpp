#include "emulator_settings.h"

#include "app_paths.h"
#include "framebuffer.h"
#include "guest_filesystem.h"
#include "platform_win32.h"
#include "ppsspp_irjit_backend.h"
#include "runtime_log.h"
#include "sdk_hle.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static std::string trimIniText(const std::string& text);

static int clampInt(int value, int minValue, int maxValue)
{
    if (value < minValue)
    {
        return minValue;
    }
    if (value > maxValue)
    {
        return maxValue;
    }
    return value;
}

static bool parseBoolText(const char* text, bool fallback)
{
    if (!text || !text[0])
    {
        return fallback;
    }
    if (strcmp(text, "1") == 0)
    {
        return true;
    }
    if (strcmp(text, "0") == 0)
    {
        return false;
    }
    return fallback;
}

static bool parseIntText(const std::string& text, int* out)
{
    if (!out || text.empty())
    {
        return false;
    }

    char* end = NULL;
    errno = 0;
    long parsed = strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE)
    {
        return false;
    }
    if (parsed < INT32_MIN || parsed > INT32_MAX)
    {
        return false;
    }

    *out = (int)parsed;
    return true;
}

static bool parseDoubleText(const std::string& text, double* out)
{
    if (!out || text.empty())
    {
        return false;
    }

    char* end = NULL;
    errno = 0;
    double parsed = strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || parsed != parsed)
    {
        return false;
    }

    *out = parsed;
    return true;
}

static bool stringEqualsIgnoreCase(const std::string& value, const char* expected)
{
    if (!expected)
    {
        return value.empty();
    }
#ifdef _WIN32
    return _stricmp(value.c_str(), expected) == 0;
#else
    if (value.size() != strlen(expected))
    {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (tolower((unsigned char)value[i]) != tolower((unsigned char)expected[i]))
        {
            return false;
        }
    }
    return true;
#endif
}

static std::string normalizeBackendName(const std::string& value, const std::string& fallback)
{
    if (value.empty())
    {
        return "";
    }
    if (stringEqualsIgnoreCase(value, "ppsspp_irjit"))
    {
        return "ppsspp_irjit";
    }
    if (stringEqualsIgnoreCase(value, "interpreter"))
    {
        return "interpreter";
    }
    return fallback;
}

static std::string normalizeCpuClockHz(const std::string& value, const std::string& fallback)
{
    if (value.empty())
    {
        return "";
    }

    int parsed = 0;
    if (!parseIntText(value, &parsed))
    {
        return fallback;
    }

    switch (parsed)
    {
    case 200000000:
    case 336000000:
    case 370000000:
    case 400000000:
    case 430000000:
        return std::to_string(parsed);
    default:
        return fallback;
    }
}

static std::string normalizeScaleValue(const std::string& value, const std::string& fallback)
{
    if (value.empty())
    {
        return "";
    }

    double parsed = 0.0;
    if (!parseDoubleText(value, &parsed) || parsed < 0.20 || parsed > 1.0)
    {
        return fallback;
    }

    double scaled = parsed * 100.0;
    int percent = (int)(scaled + 0.5);
    double diff = scaled - (double)percent;
    if (diff < 0.0)
    {
        diff = -diff;
    }
    if (diff > 0.0005 || percent < 20 || percent > 100 || (percent % 5) != 0)
    {
        return fallback;
    }

    if (percent == 100)
    {
        return "1.0";
    }

    char normalized[8] = {};
    snprintf(normalized, sizeof(normalized), "0.%02d", percent);
    return normalized;
}

static AntiAliasingMode parseAntiAliasingMode(const std::string& value, AntiAliasingMode fallback)
{
    if (value.empty())
    {
        return fallback;
    }
    if (_stricmp(value.c_str(), "off") == 0)
    {
        return ANTI_ALIASING_OFF;
    }
    if (_stricmp(value.c_str(), "low") == 0)
    {
        return ANTI_ALIASING_LOW;
    }
    if (_stricmp(value.c_str(), "clear") == 0)
    {
        return ANTI_ALIASING_CLEAR;
    }
    return fallback;
}

static ColorEffectMode parseColorEffectMode(const std::string& value, ColorEffectMode fallback)
{
    if (value.empty())
    {
        return fallback;
    }
    if (_stricmp(value.c_str(), "normal") == 0)
    {
        return COLOR_EFFECT_NORMAL;
    }
    if (_stricmp(value.c_str(), "grayscale") == 0)
    {
        return COLOR_EFFECT_GRAYSCALE;
    }
    if (_stricmp(value.c_str(), "invert") == 0)
    {
        return COLOR_EFFECT_INVERT;
    }
    if (_stricmp(value.c_str(), "soft_blur") == 0)
    {
        return COLOR_EFFECT_SOFT_BLUR;
    }
    if (_stricmp(value.c_str(), "sharpen") == 0)
    {
        return COLOR_EFFECT_SHARPEN;
    }
    if (_stricmp(value.c_str(), "vivid") == 0)
    {
        return COLOR_EFFECT_VIVID;
    }
    if (_stricmp(value.c_str(), "sepia") == 0)
    {
        return COLOR_EFFECT_SEPIA;
    }
    if (_stricmp(value.c_str(), "pixel_grid") == 0)
    {
        return COLOR_EFFECT_PIXEL_GRID;
    }
    if (_stricmp(value.c_str(), "lcd_scanline") == 0)
    {
        return COLOR_EFFECT_LCD_SCANLINE;
    }
    if (_stricmp(value.c_str(), "light_crt") == 0)
    {
        return COLOR_EFFECT_LIGHT_CRT;
    }
    return fallback;
}

static bool audioEffectModeKnown(AudioEffectMode value)
{
    switch (value)
    {
    case AUDIO_EFFECT_OFF:
    case AUDIO_EFFECT_SOFT:
    case AUDIO_EFFECT_CLEAR:
    case AUDIO_EFFECT_BASS_BOOST:
    case AUDIO_EFFECT_MONO:
        return true;
    default:
        return false;
    }
}

static AudioEffectMode normalizeAudioEffectMode(AudioEffectMode value, AudioEffectMode fallback)
{
    if (audioEffectModeKnown(value))
    {
        return value;
    }
    return audioEffectModeKnown(fallback) ? fallback : AUDIO_EFFECT_OFF;
}

static AudioEffectMode parseAudioEffectMode(const std::string& value, AudioEffectMode fallback)
{
    fallback = normalizeAudioEffectMode(fallback, AUDIO_EFFECT_OFF);
    if (value.empty())
    {
        return fallback;
    }
    if (_stricmp(value.c_str(), "off") == 0)
    {
        return AUDIO_EFFECT_OFF;
    }
    if (_stricmp(value.c_str(), "soft") == 0)
    {
        return AUDIO_EFFECT_SOFT;
    }
    if (_stricmp(value.c_str(), "clear") == 0)
    {
        return AUDIO_EFFECT_CLEAR;
    }
    if (_stricmp(value.c_str(), "bass_boost") == 0)
    {
        return AUDIO_EFFECT_BASS_BOOST;
    }
    if (_stricmp(value.c_str(), "mono") == 0)
    {
        return AUDIO_EFFECT_MONO;
    }
    return fallback;
}

static MinimizedBehavior parseMinimizedBehavior(const std::string& value, MinimizedBehavior fallback)
{
    if (value.empty())
    {
        return fallback;
    }
    if (_stricmp(value.c_str(), "normal") == 0)
    {
        return MINIMIZED_BEHAVIOR_NORMAL;
    }
    if (_stricmp(value.c_str(), "pause") == 0)
    {
        return MINIMIZED_BEHAVIOR_PAUSE;
    }
    if (_stricmp(value.c_str(), "throttle") == 0)
    {
        return MINIMIZED_BEHAVIOR_THROTTLE;
    }
    return fallback;
}

static int normalizeWindowScale(int value, int fallback)
{
    switch (value)
    {
    case 1:
    case 2:
    case 3:
        return value;
    default:
        return fallback;
    }
}

static int normalizeAudioBufferSamples(int value, int fallback)
{
    switch (value)
    {
    case 512:
    case 1024:
    case 2048:
    case 4096:
    case 8192:
        return value;
    default:
        return fallback;
    }
}

static bool recentAppPathsMatch(const std::string& a, const std::string& b)
{
#ifdef _WIN32
    return _stricmp(appNormalizePath(a.c_str()).c_str(), appNormalizePath(b.c_str()).c_str()) == 0;
#else
    return appNormalizePath(a.c_str()) == appNormalizePath(b.c_str());
#endif
}

static UiLanguage parseUiLanguage(const std::string& value, UiLanguage fallback)
{
    if (value.empty())
    {
        return fallback;
    }
    if (_stricmp(value.c_str(), "english") == 0)
    {
        return UI_LANGUAGE_ENGLISH;
    }
    if (_stricmp(value.c_str(), "chinese") == 0)
    {
        return UI_LANGUAGE_CHINESE;
    }
    return fallback;
}

static bool recentListContains(const std::vector<std::string>& paths, const std::string& appPath)
{
    for (size_t i = 0; i < paths.size(); ++i)
    {
        if (recentAppPathsMatch(paths[i], appPath))
        {
            return true;
        }
    }
    return false;
}

static void appendRecentIfUnique(std::vector<std::string>* paths, const std::string& appPath)
{
    if (!paths || appPath.empty() || paths->size() >= EMULATOR_RECENT_APP_LIMIT ||
        recentListContains(*paths, appPath))
    {
        return;
    }
    paths->push_back(appPath);
}

static std::vector<std::string> buildNormalizedRecentAppList(
    const std::string& lastAppPath,
    const std::vector<std::string>& appPaths)
{
    std::vector<std::string> normalized;
    normalized.reserve(EMULATOR_RECENT_APP_LIMIT);
    appendRecentIfUnique(&normalized, lastAppPath);
    for (size_t i = 0; i < appPaths.size(); ++i)
    {
        appendRecentIfUnique(&normalized, appPaths[i]);
    }
    return normalized;
}

static std::string cheatSelectionKeyForApp(const std::string& appPath)
{
    return appCheatFileNameFromPath(appPath);
}

static bool cheatSelectionKeysMatch(const std::string& a, const std::string& b)
{
    return stringEqualsIgnoreCase(a, b.c_str());
}

static int hexDigitValue(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

static std::string encodeCheatFeatureKey(const std::string& key)
{
    // Escape only the delimiters used by this INI value; UTF-8 names stay
    // readable so users can still inspect or edit the file by hand.
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < key.size(); ++i)
    {
        unsigned char ch = (unsigned char)key[i];
        if (ch == '%' || ch == '|')
        {
            out.push_back('%');
            out.push_back(kHex[ch >> 4]);
            out.push_back(kHex[ch & 0x0f]);
        }
        else
        {
            out.push_back((char)ch);
        }
    }
    return out;
}

static std::string decodeCheatFeatureKey(const std::string& key)
{
    std::string out;
    for (size_t i = 0; i < key.size(); ++i)
    {
        if (key[i] == '%' && i + 2 < key.size())
        {
            int hi = hexDigitValue(key[i + 1]);
            int lo = hexDigitValue(key[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(key[i]);
    }
    return out;
}

static std::string encodeCheatFeatureKeys(const std::vector<std::string>& featureKeys)
{
    // The [cheats] section stores stable feature keys from the loaded .cht
    // file, not localized labels selected by the current UI language.
    std::string out;
    for (size_t i = 0; i < featureKeys.size(); ++i)
    {
        if (featureKeys[i].empty())
        {
            continue;
        }
        if (!out.empty())
        {
            out.push_back('|');
        }
        out += encodeCheatFeatureKey(featureKeys[i]);
    }
    return out;
}

static bool hasWritableCheatSelection(const std::vector<EmulatorCheatSelection>& selections)
{
    for (size_t i = 0; i < selections.size(); ++i)
    {
        if (!selections[i].cheatFileName.empty() && !selections[i].enabledFeatureKeys.empty())
        {
            return true;
        }
    }
    return false;
}

static std::vector<std::string> decodeCheatFeatureKeys(const std::string& text)
{
    std::vector<std::string> keys;
    size_t begin = 0;
    while (begin <= text.size())
    {
        size_t sep = text.find('|', begin);
        std::string part = sep == std::string::npos ?
            text.substr(begin) : text.substr(begin, sep - begin);
        part = decodeCheatFeatureKey(trimIniText(part));
        if (!part.empty())
        {
            keys.push_back(part);
        }
        if (sep == std::string::npos)
        {
            break;
        }
        begin = sep + 1;
    }
    return keys;
}

static bool recentListsEqual(
    const std::vector<std::string>& a,
    const std::vector<std::string>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32
static void appendIniSection(std::wstring* text, const wchar_t* section)
{
    if (!text)
    {
        return;
    }
    if (!text->empty())
    {
        *text += L"\r\n";
    }
    *text += L"[";
    *text += section;
    *text += L"]\r\n";
}

static void appendIniValue(std::wstring* text, const wchar_t* key, const std::wstring& value)
{
    if (!text)
    {
        return;
    }
    *text += key;
    *text += L"=";
    *text += value;
    *text += L"\r\n";
}

static void appendIniValue(std::wstring* text, const wchar_t* key, const std::string& value)
{
    appendIniValue(text, key, platformUtf8ToWide(value));
}

static void appendIniValue(std::wstring* text, const wchar_t* key, const char* value)
{
    appendIniValue(text, key, platformUtf8ToWide(value ? value : ""));
}

static void appendIniValue(std::wstring* text, const wchar_t* key, int value)
{
    appendIniValue(text, key, std::to_wstring(value));
}

static void appendIniValue(std::wstring* text, const wchar_t* key, bool value)
{
    appendIniValue(text, key, std::wstring(value ? L"1" : L"0"));
}

static bool writeUtf16IniFile(const std::string& path, const std::wstring& text)
{
    std::wstring pathW = platformUtf8ToWide(path);
    HANDLE file = CreateFileW(pathW.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    const wchar_t bom = 0xfeff;
    DWORD written = 0;
    bool ok = WriteFile(file, &bom, sizeof(bom), &written, NULL) != 0 && written == sizeof(bom);
    if (ok && !text.empty())
    {
        DWORD bytesToWrite = (DWORD)(text.size() * sizeof(wchar_t));
        ok = WriteFile(file, text.data(), bytesToWrite, &written, NULL) != 0 && written == bytesToWrite;
    }
    CloseHandle(file);

    // Keep Win32 profile API reads in sync after replacing the INI file.
    WritePrivateProfileStringW(NULL, NULL, NULL, pathW.c_str());
    return ok;
}

static bool writeOrderedSettingsFile(const EmulatorSettings& settings, const std::string& path)
{
    std::wstring text;

    // Keep the generated INI grouped in the same order as persistent menu
    // controls: File recent, Options > Video/Audio/Input, Settings, then Debug.
    appendIniSection(&text, L"recent");
    appendIniValue(&text, L"last_app", settings.lastAppPath);
    std::vector<std::string> recentApps = buildNormalizedRecentAppList(
        settings.lastAppPath, settings.recentAppPaths);
    for (size_t i = 0; i < recentApps.size(); ++i)
    {
        wchar_t key[16] = {};
        swprintf(key, sizeof(key) / sizeof(key[0]), L"app%u", (unsigned int)(i + 1));
        appendIniValue(&text, key, recentApps[i]);
    }

    appendIniSection(&text, L"video");
    appendIniValue(&text, L"scale", clampInt(settings.windowScale, 1, 3));
    appendIniValue(&text, L"fullscreen", settings.fullscreen);
    appendIniValue(&text, L"anti_aliasing", emulatorAntiAliasingName(settings.antiAliasing));
    appendIniValue(&text, L"effect", emulatorColorEffectName(settings.colorEffect));
    appendIniValue(&text, L"brightness", clampInt(settings.brightnessPercent, 50, 150));
    appendIniValue(&text, L"contrast", clampInt(settings.contrastPercent, 50, 150));
    appendIniValue(&text, L"gamma", clampInt(settings.gammaPercent, 50, 150));
    appendIniValue(&text, L"saturation", clampInt(settings.saturationPercent, 0, 200));
    appendIniValue(&text, L"minimized_behavior", emulatorMinimizedBehaviorName(settings.minimizedBehavior));
    appendIniValue(&text, L"portrait", settings.portraitMode);
    appendIniValue(&text, L"show_fps", settings.showFps);

    appendIniSection(&text, L"audio");
    const AudioEffectMode audioEffect =
        normalizeAudioEffectMode(settings.audioEffect, AUDIO_EFFECT_OFF);
    appendIniValue(&text, L"volume_percent", clampInt(settings.audioVolumePercent, 0, 150));
    appendIniValue(&text, L"buffer_samples", normalizeAudioBufferSamples(settings.audioBufferSamples, 2048));
    appendIniValue(&text, L"effect", emulatorAudioEffectName(audioEffect));
    appendIniValue(&text, L"audio_disabled", settings.audioDisabled);

    appendIniSection(&text, L"input");
    appendIniValue(&text, L"disable_ime", settings.disableIme);
    appendIniValue(&text, L"show_virtual_controls", settings.showVirtualControls);
    appendIniValue(&text, L"keyboard_mapping", settings.keyboardMapping);
    appendIniValue(&text, L"controller_mapping", settings.controllerMapping);

    appendIniSection(&text, L"runtime");
    appendIniValue(&text, L"backend", settings.backendName);
    appendIniValue(&text, L"cpu_hz", settings.cpuClockHz);
    appendIniValue(&text, L"speed_scale", settings.runtimeSpeedScale);
    appendIniValue(&text, L"ostimedly_scale", settings.ostimeDlyScale);
    appendIniValue(&text, L"cheats_enabled", settings.cheatsEnabled);

    if (hasWritableCheatSelection(settings.cheatSelections))
    {
        appendIniSection(&text, L"cheats");
        for (size_t i = 0; i < settings.cheatSelections.size(); ++i)
        {
            const EmulatorCheatSelection& selection = settings.cheatSelections[i];
            if (!selection.cheatFileName.empty() && !selection.enabledFeatureKeys.empty())
            {
                appendIniValue(&text, platformUtf8ToWide(selection.cheatFileName).c_str(),
                    encodeCheatFeatureKeys(selection.enabledFeatureKeys));
            }
        }
    }

    appendIniSection(&text, L"ui");
    appendIniValue(&text, L"language", emulatorUiLanguageName(settings.uiLanguage));

    appendIniSection(&text, L"debug");
    appendIniValue(&text, L"show_console", settings.showDebugConsole);
    appendIniValue(&text, L"profile", settings.debugProfile);
    appendIniValue(&text, L"resource_monitor_auto_open", settings.resourceMonitorAutoOpen);

    return writeUtf16IniFile(path, text);
}
#endif

static std::string trimIniText(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n'))
    {
        begin++;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n'))
    {
        end--;
    }
    return text.substr(begin, end - begin);
}

#ifdef _WIN32
static bool wideToUtf8(const wchar_t* text, int count, std::string* out)
{
    if (!out)
    {
        return false;
    }
    if (!text || count <= 0)
    {
        out->clear();
        return true;
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, text, count, NULL, 0, NULL, NULL);
    if (size <= 0)
    {
        return false;
    }

    out->assign((size_t)size, '\0');
    return WideCharToMultiByte(CP_UTF8, 0, text, count, &(*out)[0], size, NULL, NULL) == size;
}

static bool bytesToUtf8Text(const std::vector<uint8_t>& bytes, std::string* out)
{
    if (!out)
    {
        return false;
    }
    out->clear();
    if (bytes.empty())
    {
        return true;
    }

    if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
    {
        size_t wcharCount = (bytes.size() - 2) / sizeof(wchar_t);
        return wideToUtf8((const wchar_t*)&bytes[2], (int)wcharCount, out);
    }

    if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff)
    {
        std::wstring wide;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2)
        {
            wide.push_back((wchar_t)(((uint16_t)bytes[i] << 8) | bytes[i + 1]));
        }
        return wideToUtf8(wide.data(), (int)wide.size(), out);
    }

    size_t offset = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
    {
        offset = 3;
    }

    const char* raw = (const char*)bytes.data() + offset;
    int rawSize = (int)(bytes.size() - offset);
    int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw, rawSize, NULL, 0);
    if (wideSize > 0)
    {
        std::wstring wide((size_t)wideSize, L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw, rawSize, &wide[0], wideSize) == wideSize)
        {
            return wideToUtf8(wide.data(), wideSize, out);
        }
    }

    wideSize = MultiByteToWideChar(CP_ACP, 0, raw, rawSize, NULL, 0);
    if (wideSize <= 0)
    {
        return false;
    }
    std::wstring wide((size_t)wideSize, L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, raw, rawSize, &wide[0], wideSize) != wideSize)
    {
        return false;
    }
    return wideToUtf8(wide.data(), wideSize, out);
}
#endif

static bool readTextFileUtf8(const std::string& path, std::string* out)
{
    if (!out)
    {
        return false;
    }
    out->clear();

#ifdef _WIN32
    HANDLE file = CreateFileW(platformUtf8ToWide(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0)
    {
        CloseHandle(file);
        return fileSize.QuadPart == 0;
    }
    if (fileSize.QuadPart > 1024 * 1024)
    {
        CloseHandle(file);
        return false;
    }

    std::vector<uint8_t> bytes((size_t)fileSize.QuadPart);
    DWORD read = 0;
    bool ok = ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, NULL) != 0 && read == bytes.size();
    CloseHandle(file);
    return ok && bytesToUtf8Text(bytes, out);
#else
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
    {
        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0)
    {
        fclose(file);
        return size == 0;
    }
    std::vector<char> bytes((size_t)size);
    bool ok = fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    if (!ok)
    {
        return false;
    }
    out->assign(bytes.begin(), bytes.end());
    if (out->size() >= 3 && (uint8_t)(*out)[0] == 0xef && (uint8_t)(*out)[1] == 0xbb && (uint8_t)(*out)[2] == 0xbf)
    {
        out->erase(0, 3);
    }
    return true;
#endif
}

static std::string readIniString(const char* section, const char* key, const char* fallback, const std::string& path)
{
    std::string text;
    if (!readTextFileUtf8(path, &text))
    {
        return fallback ? fallback : "";
    }

    std::string currentSection;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t lineEnd = text.find('\n', pos);
        std::string line = lineEnd == std::string::npos ? text.substr(pos) : text.substr(pos, lineEnd - pos);
        pos = lineEnd == std::string::npos ? text.size() + 1 : lineEnd + 1;

        line = trimIniText(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }
        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = trimIniText(line.substr(1, line.size() - 2));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        std::string itemKey = trimIniText(line.substr(0, eq));
        std::string itemValue = trimIniText(line.substr(eq + 1));
        if (stringEqualsIgnoreCase(currentSection, section) &&
            stringEqualsIgnoreCase(itemKey, key))
        {
            return itemValue;
        }
    }

    return fallback ? fallback : "";
}

static int readIniInt(const char* section, const char* key, int fallback, const std::string& path)
{
    std::string text = readIniString(section, key, "", path);
    int parsed = 0;
    return parseIntText(text, &parsed) ? parsed : fallback;
}

static std::vector<std::pair<std::string, std::string> > readIniSectionValues(
    const char* section,
    const std::string& path)
{
    std::vector<std::pair<std::string, std::string> > values;
    std::string text;
    if (!section || !readTextFileUtf8(path, &text))
    {
        return values;
    }

    std::string currentSection;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t lineEnd = text.find('\n', pos);
        std::string line = lineEnd == std::string::npos ? text.substr(pos) : text.substr(pos, lineEnd - pos);
        pos = lineEnd == std::string::npos ? text.size() + 1 : lineEnd + 1;

        line = trimIniText(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }
        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = trimIniText(line.substr(1, line.size() - 2));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos ||
            !stringEqualsIgnoreCase(currentSection, section))
        {
            continue;
        }
        values.push_back(std::make_pair(
            trimIniText(line.substr(0, eq)),
            trimIniText(line.substr(eq + 1))));
    }

    return values;
}

static bool writeIniString(const char* section, const char* key, const std::string& value, const std::string& path)
{
#ifdef _WIN32
    std::wstring pathW = platformUtf8ToWide(path);
    std::wstring sectionW = platformUtf8ToWide(section);
    std::wstring keyW = platformUtf8ToWide(key);
    std::wstring valueW = platformUtf8ToWide(value);
    return WritePrivateProfileStringW(sectionW.c_str(), keyW.c_str(), valueW.c_str(), pathW.c_str()) != 0;
#else
    (void)section;
    (void)key;
    (void)value;
    (void)path;
    return false;
#endif
}

static void setEnvValue(const char* name, const std::string& value)
{
#ifdef _WIN32
    std::wstring nameW = platformUtf8ToWide(name);
    if (value.empty())
    {
        SetEnvironmentVariableW(nameW.c_str(), NULL);
        _putenv_s(name, "");
    }
    else
    {
        std::wstring valueW = platformUtf8ToWide(value);
        SetEnvironmentVariableW(nameW.c_str(), valueW.c_str());
        _putenv_s(name, value.c_str());
    }
#else
    if (value.empty())
    {
        unsetenv(name);
    }
    else
    {
        setenv(name, value.c_str(), 1);
    }
#endif
}

EmulatorSettings emulatorDefaultSettings(void)
{
    EmulatorSettings settings;
    settings.lastAppPath = "";
    settings.recentAppPaths.clear();

    settings.windowScale = 2;
    settings.fullscreen = false;
    settings.antiAliasing = ANTI_ALIASING_OFF;
    settings.colorEffect = COLOR_EFFECT_NORMAL;
    settings.brightnessPercent = 100;
    settings.contrastPercent = 100;
    settings.gammaPercent = 100;
    settings.saturationPercent = 100;
    settings.minimizedBehavior = MINIMIZED_BEHAVIOR_PAUSE;
    settings.portraitMode = false;
    settings.showFps = false;

    settings.audioVolumePercent = 100;
    settings.audioBufferSamples = 2048;
    settings.audioEffect = AUDIO_EFFECT_OFF;
    settings.audioDisabled = false;

    settings.disableIme = true;
    settings.showVirtualControls = false;
    settings.keyboardMapping = "";
    settings.controllerMapping = "";

    settings.backendName = "";
    settings.cpuClockHz = "";
    settings.runtimeSpeedScale = "";
    settings.ostimeDlyScale = "";
    settings.cheatsEnabled = false;
    settings.cheatSelections.clear();

    settings.uiLanguage = UI_LANGUAGE_CHINESE;

    settings.showDebugConsole = false;
    settings.debugProfile = false;
    settings.resourceMonitorAutoOpen = false;
    return settings;
}

std::string emulatorSettingsPath(void)
{
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    std::wstring path(modulePath);
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        path.resize(slash + 1);
    }
    path += L"DingooPie.ini";
    return platformWideToUtf8(path);
#else
    return "DingooPie.ini";
#endif
}

EmulatorSettings emulatorLoadSettings(void)
{
    EmulatorSettings defaults = emulatorDefaultSettings();
    std::string path = emulatorSettingsPath();
    EmulatorSettings settings = defaults;
    settings.lastAppPath = readIniString("recent", "last_app", defaults.lastAppPath.c_str(), path);
    std::vector<std::string> recentApps;
    recentApps.reserve(EMULATOR_RECENT_APP_LIMIT);
    appendRecentIfUnique(&recentApps, settings.lastAppPath);
    for (int i = 1; i <= EMULATOR_RECENT_APP_LIMIT; ++i)
    {
        char key[16] = {};
        snprintf(key, sizeof(key), "app%d", i);
        appendRecentIfUnique(&recentApps, readIniString("recent", key, "", path));
    }
    settings.recentAppPaths = recentApps;

    settings.windowScale = normalizeWindowScale(
        readIniInt("video", "scale", defaults.windowScale, path),
        defaults.windowScale);
    settings.fullscreen = parseBoolText(readIniString("video", "fullscreen", defaults.fullscreen ? "1" : "0", path).c_str(), defaults.fullscreen);
    std::string antiAliasing = readIniString("video", "anti_aliasing", emulatorAntiAliasingName(defaults.antiAliasing), path);
    settings.antiAliasing = parseAntiAliasingMode(antiAliasing, defaults.antiAliasing);
    std::string effect = readIniString("video", "effect", emulatorColorEffectName(defaults.colorEffect), path);
    settings.colorEffect = parseColorEffectMode(effect, defaults.colorEffect);
    settings.brightnessPercent = clampInt(readIniInt("video", "brightness", defaults.brightnessPercent, path), 50, 150);
    settings.contrastPercent = clampInt(readIniInt("video", "contrast", defaults.contrastPercent, path), 50, 150);
    settings.gammaPercent = clampInt(readIniInt("video", "gamma", defaults.gammaPercent, path), 50, 150);
    settings.saturationPercent = clampInt(readIniInt("video", "saturation", defaults.saturationPercent, path), 0, 200);
    std::string minimizedBehavior = readIniString("video", "minimized_behavior", emulatorMinimizedBehaviorName(defaults.minimizedBehavior), path);
    settings.minimizedBehavior = parseMinimizedBehavior(minimizedBehavior, defaults.minimizedBehavior);
    settings.portraitMode = parseBoolText(readIniString("video", "portrait", defaults.portraitMode ? "1" : "0", path).c_str(), defaults.portraitMode);
    settings.showFps = parseBoolText(readIniString("video", "show_fps", defaults.showFps ? "1" : "0", path).c_str(), defaults.showFps);

    settings.audioVolumePercent = clampInt(
        readIniInt("audio", "volume_percent", defaults.audioVolumePercent, path),
        0,
        150);
    settings.audioBufferSamples = normalizeAudioBufferSamples(
        readIniInt("audio", "buffer_samples", defaults.audioBufferSamples, path),
        defaults.audioBufferSamples);
    std::string audioEffect = readIniString(
        "audio",
        "effect",
        emulatorAudioEffectName(defaults.audioEffect),
        path);
    settings.audioEffect = parseAudioEffectMode(audioEffect, defaults.audioEffect);
    settings.audioDisabled = parseBoolText(
        readIniString(
            "audio",
            "audio_disabled",
            defaults.audioDisabled ? "1" : "0",
            path).c_str(),
        defaults.audioDisabled);

    settings.disableIme = parseBoolText(readIniString("input", "disable_ime", defaults.disableIme ? "1" : "0", path).c_str(), defaults.disableIme);
    settings.showVirtualControls = parseBoolText(readIniString("input", "show_virtual_controls", defaults.showVirtualControls ? "1" : "0", path).c_str(), defaults.showVirtualControls);
    settings.keyboardMapping = readIniString("input", "keyboard_mapping", defaults.keyboardMapping.c_str(), path);
    settings.controllerMapping = readIniString("input", "controller_mapping", defaults.controllerMapping.c_str(), path);

    settings.backendName = normalizeBackendName(
        readIniString("runtime", "backend", defaults.backendName.c_str(), path),
        defaults.backendName);
    settings.cpuClockHz = normalizeCpuClockHz(
        readIniString("runtime", "cpu_hz", defaults.cpuClockHz.c_str(), path),
        defaults.cpuClockHz);
    settings.runtimeSpeedScale = normalizeScaleValue(
        readIniString("runtime", "speed_scale", defaults.runtimeSpeedScale.c_str(), path),
        defaults.runtimeSpeedScale);
    settings.ostimeDlyScale = normalizeScaleValue(
        readIniString("runtime", "ostimedly_scale", defaults.ostimeDlyScale.c_str(), path),
        defaults.ostimeDlyScale);
    settings.cheatsEnabled = parseBoolText(readIniString("runtime", "cheats_enabled", defaults.cheatsEnabled ? "1" : "0", path).c_str(), defaults.cheatsEnabled);
    std::vector<std::pair<std::string, std::string> > cheatValues =
        readIniSectionValues("cheats", path);
    settings.cheatSelections.clear();
    settings.cheatSelections.reserve(cheatValues.size());
    for (size_t i = 0; i < cheatValues.size(); ++i)
    {
        if (cheatValues[i].first.empty())
        {
            continue;
        }
        EmulatorCheatSelection selection;
        selection.cheatFileName = cheatValues[i].first;
        selection.enabledFeatureKeys = decodeCheatFeatureKeys(cheatValues[i].second);
        settings.cheatSelections.push_back(selection);
    }

    std::string language = readIniString("ui", "language", emulatorUiLanguageName(defaults.uiLanguage), path);
    settings.uiLanguage = parseUiLanguage(language, defaults.uiLanguage);

    settings.showDebugConsole = parseBoolText(readIniString("debug", "show_console", defaults.showDebugConsole ? "1" : "0", path).c_str(), defaults.showDebugConsole);
    settings.debugProfile = parseBoolText(readIniString("debug", "profile", defaults.debugProfile ? "1" : "0", path).c_str(), defaults.debugProfile);
    settings.resourceMonitorAutoOpen = parseBoolText(readIniString("debug", "resource_monitor_auto_open", defaults.resourceMonitorAutoOpen ? "1" : "0", path).c_str(), defaults.resourceMonitorAutoOpen);
    return settings;
}

bool emulatorSaveSettings(const EmulatorSettings& settings)
{
    std::string path = emulatorSettingsPath();
#ifdef _WIN32
    bool ok = writeOrderedSettingsFile(settings, path);
#else
    bool ok = true;
    // Match the Windows full-file writer order so manually inspected INI files
    // stay aligned with the frontend menu layout.
    ok = writeIniString("recent", "last_app", settings.lastAppPath, path) && ok;
    std::vector<std::string> recentApps = buildNormalizedRecentAppList(
        settings.lastAppPath, settings.recentAppPaths);
    for (size_t i = 0; i < recentApps.size(); ++i)
    {
        char key[16] = {};
        snprintf(key, sizeof(key), "app%u", (unsigned int)(i + 1));
        ok = writeIniString("recent", key, recentApps[i], path) && ok;
    }
    ok = writeIniString("video", "scale", std::to_string(clampInt(settings.windowScale, 1, 3)), path) && ok;
    ok = writeIniString("video", "fullscreen", settings.fullscreen ? "1" : "0", path) && ok;
    ok = writeIniString("video", "anti_aliasing", emulatorAntiAliasingName(settings.antiAliasing), path) && ok;
    ok = writeIniString("video", "effect", emulatorColorEffectName(settings.colorEffect), path) && ok;
    ok = writeIniString("video", "brightness", std::to_string(clampInt(settings.brightnessPercent, 50, 150)), path) && ok;
    ok = writeIniString("video", "contrast", std::to_string(clampInt(settings.contrastPercent, 50, 150)), path) && ok;
    ok = writeIniString("video", "gamma", std::to_string(clampInt(settings.gammaPercent, 50, 150)), path) && ok;
    ok = writeIniString("video", "saturation", std::to_string(clampInt(settings.saturationPercent, 0, 200)), path) && ok;
    ok = writeIniString("video", "minimized_behavior", emulatorMinimizedBehaviorName(settings.minimizedBehavior), path) && ok;
    ok = writeIniString("video", "portrait", settings.portraitMode ? "1" : "0", path) && ok;
    ok = writeIniString("video", "show_fps", settings.showFps ? "1" : "0", path) && ok;
    const AudioEffectMode audioEffect =
        normalizeAudioEffectMode(settings.audioEffect, AUDIO_EFFECT_OFF);
    ok = writeIniString(
        "audio",
        "volume_percent",
        std::to_string(clampInt(settings.audioVolumePercent, 0, 150)),
        path) && ok;
    ok = writeIniString(
        "audio",
        "buffer_samples",
        std::to_string(normalizeAudioBufferSamples(settings.audioBufferSamples, 2048)),
        path) && ok;
    ok = writeIniString("audio", "effect", emulatorAudioEffectName(audioEffect), path) && ok;
    ok = writeIniString("audio", "audio_disabled", settings.audioDisabled ? "1" : "0", path) && ok;
    ok = writeIniString("input", "disable_ime", settings.disableIme ? "1" : "0", path) && ok;
    ok = writeIniString("input", "show_virtual_controls", settings.showVirtualControls ? "1" : "0", path) && ok;
    ok = writeIniString("input", "keyboard_mapping", settings.keyboardMapping, path) && ok;
    ok = writeIniString("input", "controller_mapping", settings.controllerMapping, path) && ok;
    ok = writeIniString("runtime", "backend", settings.backendName, path) && ok;
    ok = writeIniString("runtime", "cpu_hz", settings.cpuClockHz, path) && ok;
    ok = writeIniString("runtime", "speed_scale", settings.runtimeSpeedScale, path) && ok;
    ok = writeIniString("runtime", "ostimedly_scale", settings.ostimeDlyScale, path) && ok;
    ok = writeIniString("runtime", "cheats_enabled", settings.cheatsEnabled ? "1" : "0", path) && ok;
    for (size_t i = 0; i < settings.cheatSelections.size(); ++i)
    {
        const EmulatorCheatSelection& selection = settings.cheatSelections[i];
        if (!selection.cheatFileName.empty() && !selection.enabledFeatureKeys.empty())
        {
            ok = writeIniString("cheats", selection.cheatFileName.c_str(),
                encodeCheatFeatureKeys(selection.enabledFeatureKeys), path) && ok;
        }
    }
    ok = writeIniString("ui", "language", emulatorUiLanguageName(settings.uiLanguage), path) && ok;
    ok = writeIniString("debug", "show_console", settings.showDebugConsole ? "1" : "0", path) && ok;
    ok = writeIniString("debug", "profile", settings.debugProfile ? "1" : "0", path) && ok;
    ok = writeIniString("debug", "resource_monitor_auto_open", settings.resourceMonitorAutoOpen ? "1" : "0", path) && ok;
#endif
    if (ok && (settings.showDebugConsole || settings.debugProfile ||
        getenv("DINGOO_PIE_LOG_FILE")))
    {
        emulatorTraceSettings("saved", settings);
    }
    return ok;
}

bool emulatorRememberRecentApp(EmulatorSettings* settings, const std::string& appPath)
{
    if (!settings || appPath.empty())
    {
        return false;
    }

    std::vector<std::string> next;
    next.reserve(EMULATOR_RECENT_APP_LIMIT);
    appendRecentIfUnique(&next, appPath);
    for (size_t i = 0; i < settings->recentAppPaths.size(); ++i)
    {
        appendRecentIfUnique(&next, settings->recentAppPaths[i]);
    }
    appendRecentIfUnique(&next, settings->lastAppPath);

    bool changed = !recentAppPathsMatch(settings->lastAppPath, appPath) ||
        !recentListsEqual(settings->recentAppPaths, next);
    settings->lastAppPath = appPath;
    settings->recentAppPaths = next;
    return changed;
}

bool emulatorRemoveRecentApp(EmulatorSettings* settings, const std::string& appPath)
{
    if (!settings || appPath.empty())
    {
        return false;
    }

    const std::string targetPath = appPath;
    std::vector<std::string> next;
    next.reserve(EMULATOR_RECENT_APP_LIMIT);
    bool removed = false;
    for (size_t i = 0; i < settings->recentAppPaths.size(); ++i)
    {
        if (recentAppPathsMatch(settings->recentAppPaths[i], targetPath))
        {
            removed = true;
            continue;
        }
        appendRecentIfUnique(&next, settings->recentAppPaths[i]);
    }

    if (!settings->lastAppPath.empty() && recentAppPathsMatch(settings->lastAppPath, targetPath))
    {
        removed = true;
        settings->lastAppPath = next.empty() ? "" : next[0];
    }

    if (removed || !recentListsEqual(settings->recentAppPaths, next))
    {
        settings->recentAppPaths = next;
        return true;
    }
    return false;
}

bool emulatorClearRecentApps(EmulatorSettings* settings)
{
    if (!settings)
    {
        return false;
    }

    bool changed = !settings->lastAppPath.empty() || !settings->recentAppPaths.empty();
    settings->lastAppPath.clear();
    settings->recentAppPaths.clear();
    return changed;
}

std::vector<std::string> emulatorCheatFeatureKeysForApp(
    const EmulatorSettings& settings,
    const std::string& appPath)
{
    std::string cheatFileName = cheatSelectionKeyForApp(appPath);
    if (cheatFileName.empty())
    {
        return std::vector<std::string>();
    }

    for (size_t i = 0; i < settings.cheatSelections.size(); ++i)
    {
        if (cheatSelectionKeysMatch(settings.cheatSelections[i].cheatFileName, cheatFileName))
        {
            return settings.cheatSelections[i].enabledFeatureKeys;
        }
    }
    return std::vector<std::string>();
}

bool emulatorSetCheatFeatureKeysForApp(
    EmulatorSettings* settings,
    const std::string& appPath,
    const std::vector<std::string>& featureKeys)
{
    if (!settings)
    {
        return false;
    }

    std::string cheatFileName = cheatSelectionKeyForApp(appPath);
    if (cheatFileName.empty())
    {
        return false;
    }

    for (size_t i = 0; i < settings->cheatSelections.size(); ++i)
    {
        if (!cheatSelectionKeysMatch(settings->cheatSelections[i].cheatFileName, cheatFileName))
        {
            continue;
        }
        if (featureKeys.empty())
        {
            settings->cheatSelections.erase(settings->cheatSelections.begin() + i);
            return true;
        }
        if (settings->cheatSelections[i].enabledFeatureKeys == featureKeys)
        {
            return false;
        }
        settings->cheatSelections[i].enabledFeatureKeys = featureKeys;
        return true;
    }

    if (featureKeys.empty())
    {
        return false;
    }

    EmulatorCheatSelection selection;
    selection.cheatFileName = cheatFileName;
    selection.enabledFeatureKeys = featureKeys;
    settings->cheatSelections.push_back(selection);
    return true;
}

void emulatorTraceSettings(const char* reason, const EmulatorSettings& settings)
{
    const char* label = (reason && reason[0]) ? reason : "snapshot";
    const AudioEffectMode audioEffect =
        normalizeAudioEffectMode(settings.audioEffect, AUDIO_EFFECT_OFF);
    printf("settings-trace:%s recent.last_app=\"%s\"\n",
        label,
        settings.lastAppPath.empty() ? "(empty)" : settings.lastAppPath.c_str());
    std::vector<std::string> recentApps = buildNormalizedRecentAppList(
        settings.lastAppPath, settings.recentAppPaths);
    for (size_t i = 0; i < recentApps.size(); ++i)
    {
        printf("settings-trace:%s recent.app%u=\"%s\"\n",
            label,
            (unsigned int)(i + 1),
            recentApps[i].c_str());
    }
    printf("settings-trace:%s video.scale=%d video.fullscreen=%u video.anti_aliasing=%s video.effect=%s video.brightness=%d video.contrast=%d video.gamma=%d video.saturation=%d video.minimized_behavior=%s video.portrait=%u video.show_fps=%u\n",
        label,
        clampInt(settings.windowScale, 1, 3),
        settings.fullscreen ? 1u : 0u,
        emulatorAntiAliasingName(settings.antiAliasing),
        emulatorColorEffectName(settings.colorEffect),
        clampInt(settings.brightnessPercent, 50, 150),
        clampInt(settings.contrastPercent, 50, 150),
        clampInt(settings.gammaPercent, 50, 150),
        clampInt(settings.saturationPercent, 0, 200),
        emulatorMinimizedBehaviorName(settings.minimizedBehavior),
        settings.portraitMode ? 1u : 0u,
        settings.showFps ? 1u : 0u);
    printf("settings-trace:%s audio.volume_percent=%d audio.buffer_samples=%d audio.effect=%s audio.audio_disabled=%u\n",
        label,
        clampInt(settings.audioVolumePercent, 0, 150),
        normalizeAudioBufferSamples(settings.audioBufferSamples, 2048),
        emulatorAudioEffectName(audioEffect),
        settings.audioDisabled ? 1u : 0u);
    printf("settings-trace:%s input.disable_ime=%u input.show_virtual_controls=%u input.keyboard_mapping=\"%s\" input.controller_mapping=\"%s\"\n",
        label,
        settings.disableIme ? 1u : 0u,
        settings.showVirtualControls ? 1u : 0u,
        settings.keyboardMapping.empty() ? "(default)" : settings.keyboardMapping.c_str(),
        settings.controllerMapping.empty() ? "(default)" : settings.controllerMapping.c_str());
    printf("settings-trace:%s runtime.backend=%s runtime.cpu_hz=%s runtime.speed_scale=%s runtime.ostimedly_scale=%s runtime.cheats_enabled=%u\n",
        label,
        settings.backendName.empty() ? "auto" : settings.backendName.c_str(),
        settings.cpuClockHz.empty() ? "auto" : settings.cpuClockHz.c_str(),
        settings.runtimeSpeedScale.empty() ? "auto" : settings.runtimeSpeedScale.c_str(),
        settings.ostimeDlyScale.empty() ? "auto" : settings.ostimeDlyScale.c_str(),
        settings.cheatsEnabled ? 1u : 0u);
    for (size_t i = 0; i < settings.cheatSelections.size(); ++i)
    {
        const EmulatorCheatSelection& selection = settings.cheatSelections[i];
        if (!selection.cheatFileName.empty() && !selection.enabledFeatureKeys.empty())
        {
            printf("settings-trace:%s cheats.%s=\"%s\"\n",
                label,
                selection.cheatFileName.c_str(),
                encodeCheatFeatureKeys(selection.enabledFeatureKeys).c_str());
        }
    }
    printf("settings-trace:%s ui.language=%s\n",
        label,
        emulatorUiLanguageName(settings.uiLanguage));
    printf("settings-trace:%s debug.show_console=%u debug.profile=%u debug.resource_monitor_auto_open=%u\n",
        label,
        settings.showDebugConsole ? 1u : 0u,
        settings.debugProfile ? 1u : 0u,
        settings.resourceMonitorAutoOpen ? 1u : 0u);
}

bool emulatorResetSettings(void)
{
    EmulatorSettings defaults = emulatorDefaultSettings();
    return emulatorSaveSettings(defaults);
}

void emulatorApplySettingsToEnvironment(const EmulatorSettings& settings)
{
    setEnvValue("DINGOO_PIE_BACKEND", settings.backendName);
    setEnvValue("DINGOO_PIE_IRJIT_CLOCK_HZ", settings.cpuClockHz);

    // Empty runtimeSpeedScale is the menu/INI "Auto" preset. Runtime code maps
    // Auto to the chosen global pace while keeping the UI checked on Auto.
    setEnvValue("DINGOO_PIE_RUNTIME_SPEED_SCALE", settings.runtimeSpeedScale);
    setEnvValue("DINGOO_PIE_OSTIMEDLY_SCALE", settings.ostimeDlyScale);
    setEnvValue("DINGOO_PIE_AUDIO_DISABLED", settings.audioDisabled ? "1" : "");
    setEnvValue("DINGOO_PIE_PROFILE", settings.debugProfile || runtimeLogExternalProfileEnabled() ? "1" : "");
    runtimeLogSetProfileEnabled(settings.debugProfile);
}

void emulatorApplyRuntimeSettings(const EmulatorSettings& settings)
{
    emulatorApplySettingsToEnvironment(settings);
    bool profileEnabled = runtimeLogProfileEnabled();
    framebufferSetProfileEnabled(profileEnabled);
    fsys_set_profile_enabled(profileEnabled);
    bridge_apply_runtime_settings();
#ifdef DINGOO_PIE_ENABLE_PPSSPP_IRJIT
    ppssppShimApplyRuntimeSettings();
#endif
}

const char* emulatorAntiAliasingName(AntiAliasingMode mode)
{
    switch (mode)
    {
    case ANTI_ALIASING_LOW:
        return "low";
    case ANTI_ALIASING_CLEAR:
        return "clear";
    default:
        return "off";
    }
}

const char* emulatorColorEffectName(ColorEffectMode mode)
{
    switch (mode)
    {
    case COLOR_EFFECT_GRAYSCALE:
        return "grayscale";
    case COLOR_EFFECT_INVERT:
        return "invert";
    case COLOR_EFFECT_SOFT_BLUR:
        return "soft_blur";
    case COLOR_EFFECT_SHARPEN:
        return "sharpen";
    case COLOR_EFFECT_VIVID:
        return "vivid";
    case COLOR_EFFECT_SEPIA:
        return "sepia";
    case COLOR_EFFECT_PIXEL_GRID:
        return "pixel_grid";
    case COLOR_EFFECT_LCD_SCANLINE:
        return "lcd_scanline";
    case COLOR_EFFECT_LIGHT_CRT:
        return "light_crt";
    default:
        return "normal";
    }
}

const char* emulatorAudioEffectName(AudioEffectMode mode)
{
    switch (mode)
    {
    case AUDIO_EFFECT_SOFT:
        return "soft";
    case AUDIO_EFFECT_CLEAR:
        return "clear";
    case AUDIO_EFFECT_BASS_BOOST:
        return "bass_boost";
    case AUDIO_EFFECT_MONO:
        return "mono";
    case AUDIO_EFFECT_OFF:
    default:
        return "off";
    }
}

const char* emulatorUiLanguageName(UiLanguage language)
{
    return language == UI_LANGUAGE_CHINESE ? "chinese" : "english";
}

const char* emulatorMinimizedBehaviorName(MinimizedBehavior behavior)
{
    switch (behavior)
    {
    case MINIMIZED_BEHAVIOR_NORMAL:
        return "normal";
    case MINIMIZED_BEHAVIOR_PAUSE:
        return "pause";
    case MINIMIZED_BEHAVIOR_THROTTLE:
    default:
        return "throttle";
    }
}

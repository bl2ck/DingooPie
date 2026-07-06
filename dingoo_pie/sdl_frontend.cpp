#include "sdl_frontend.h"

#include "emulator_config.h"
#include "input_mapping_ui.h"
#include "memory_searcher_ui.h"
#include "debugger_ui.h"
#include "emulator_core.h"
#include "input_controls.h"
#include "framebuffer.h"
#include "frontend_menu.h"
#include "pause_gate.h"
#include "platform_win32.h"
#include "resource_monitor_ui.h"
#include "sdk_hle.h"
#include "sdl_audio.h"
#include "resource_ids.h"
#include "save_state.h"
#include "ui_strings.h"
#include "runtime_log.h"

#include <SDL2/SDL.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL2/SDL_syswm.h>
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <propidl.h>
#include <imm.h>
#include <gdiplus.h>
#endif
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>
#include <vector>

static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static SDL_Texture* g_frameTexture = NULL;
static SDL_Texture* g_fpsOverlayTexture = NULL;
static SDL_Texture* g_idleTitleTexture = NULL;
static SDL_Texture* g_idleSymbolTextures[4] = {};
static int g_fpsOverlayValue = -1;
static int g_fpsOverlayWidth = 0;
static int g_fpsOverlayHeight = 0;
static int g_idleTitleTextureWidth = 0;
static int g_idleTitleTextureHeight = 0;
static SDL_GameController* g_gameController = NULL;
static uint32_t g_gameControllerButtonControls = 0;
static uint32_t g_gameControllerAxisControls = 0;
static Sint16 g_gameControllerAxes[SDL_CONTROLLER_AXIS_MAX];
static uint32_t g_gameControllerButtonMap[SDL_CONTROLLER_BUTTON_MAX];
static uint32_t g_gameControllerAxisMap[SDL_CONTROLLER_AXIS_MAX][2];
static bool g_controllerMappingPending = false;
static uint32_t g_controllerMappingTarget = 0;
static bool g_controllerMappingInitialized = false;
static std::string g_appliedControllerMapping;
static SDL_atomic_t g_quitRequested;
static SDL_atomic_t g_gamePaused;
static bool g_userPauseRequested = false;
static bool g_minimizedPauseActive = false;
static unsigned int g_modalPauseDepth = 0;
static EmulatorSettings* g_frontendSettings = NULL;
static uint16_t g_lastDisplayFrame[SCREEN_WIDTH * SCREEN_HEIGHT];
static int g_lastDisplayFrameWidth = SCREEN_WIDTH;
static int g_lastDisplayFrameHeight = SCREEN_HEIGHT;
static bool g_lastDisplayFrameValid = false;
#ifdef _WIN32
static HWND g_nativeWindow = NULL;
static HIMC g_defaultImeContext = NULL;
static bool g_menuLoopPauseActive = false;
#endif

static const uint64_t kMinimizedThrottlePresentIntervalMs = 250;
static const uint32_t kMinimizedThrottleLoopDelayMs = 50;
static const uint64_t kIdlePresentIntervalUs = 16667;
static const uint64_t kIdleWakeMarginUs = 2000;
static const uint32_t kIdleMaxWaitMs = 4;
static const double kPi = 3.14159265358979323846;

static bool inputTraceEnabled(void);
static void openFirstGameController(void);
#ifdef _WIN32
static void setMenuLoopPauseActive(bool active);
#endif

static const char* sdlLogCategoryName(int category)
{
    switch (category)
    {
    case SDL_LOG_CATEGORY_APPLICATION:
        return "application";
    case SDL_LOG_CATEGORY_ERROR:
        return "error";
    case SDL_LOG_CATEGORY_ASSERT:
        return "assert";
    case SDL_LOG_CATEGORY_SYSTEM:
        return "system";
    case SDL_LOG_CATEGORY_AUDIO:
        return "audio";
    case SDL_LOG_CATEGORY_VIDEO:
        return "video";
    case SDL_LOG_CATEGORY_RENDER:
        return "render";
    case SDL_LOG_CATEGORY_INPUT:
        return "input";
    case SDL_LOG_CATEGORY_TEST:
        return "test";
    default:
        return "custom";
    }
}

static const char* sdlLogPriorityName(SDL_LogPriority priority)
{
    switch (priority)
    {
    case SDL_LOG_PRIORITY_VERBOSE:
        return "verbose";
    case SDL_LOG_PRIORITY_DEBUG:
        return "debug";
    case SDL_LOG_PRIORITY_INFO:
        return "info";
    case SDL_LOG_PRIORITY_WARN:
        return "warn";
    case SDL_LOG_PRIORITY_ERROR:
        return "error";
    case SDL_LOG_PRIORITY_CRITICAL:
        return "critical";
    default:
        return "unknown";
    }
}

static void SDLCALL frontendSdlLogOutput(void* userdata, int category,
    SDL_LogPriority priority, const char* message)
{
    (void)userdata;
    printf("sdl-log: %s %s: %s\n",
        sdlLogPriorityName(priority),
        sdlLogCategoryName(category),
        message ? message : "");
}

static uint32_t controlMask(uint32_t controlBit)
{
    return 1u << controlBit;
}

static bool confirmExitRequested(void)
{
    inputClearSyntheticControls();
    inputClearControls();
#ifdef _WIN32
    UiLanguage language = g_frontendSettings ? g_frontendSettings->uiLanguage : UI_LANGUAGE_ENGLISH;
    int result = MessageBoxW(g_nativeWindow,
        uiText(language, TXT_CONFIRM_EXIT_BODY),
        uiText(language, TXT_CONFIRM_EXIT_TITLE),
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    return result == IDYES;
#else
    return true;
#endif
}

#ifdef _WIN32
static HICON loadDingooPieIcon(int size)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED);
}

static void applyDingooPieIconToWindow(HWND window)
{
    if (!window)
    {
        return;
    }

    HICON largeIcon = loadDingooPieIcon(32);
    HICON smallIcon = loadDingooPieIcon(16);
    if (largeIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    }
    if (smallIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    }
}

static void applyWindowIcon(void)
{
    if (!g_window)
    {
        return;
    }

    applyDingooPieIconToWindow(g_nativeWindow);
}
#endif

static const uint8_t kDigitFont[10][7] =
{
    { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
    { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
    { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
    { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
    { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
    { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
    { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e },
};

static const uint8_t kLetterA[7] = { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 };
static const uint8_t kLetterB[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e };
static const uint8_t kLetterC[7] = { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e };
static const uint8_t kLetterD[7] = { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e };
static const uint8_t kLetterE[7] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f };
static const uint8_t kLetterF[7] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 };
static const uint8_t kLetterG[7] = { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f };
static const uint8_t kLetterI[7] = { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e };
static const uint8_t kLetterL[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f };
static const uint8_t kLetterN[7] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
static const uint8_t kLetterO[7] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
static const uint8_t kLetterP[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 };
static const uint8_t kLetterR[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 };
static const uint8_t kLetterS[7] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e };
static const uint8_t kLetterT[7] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
static const uint8_t kLetterU[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
static const uint8_t kLetterX[7] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 };
static const uint8_t kLetterY[7] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 };
static const uint8_t kColon[7]   = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };
static const uint8_t kLowerE[7]  = { 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e };
static const uint8_t kLowerG[7]  = { 0x00, 0x00, 0x0f, 0x11, 0x0f, 0x01, 0x0e };
static const uint8_t kLowerI[7]  = { 0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e };
static const uint8_t kLowerN[7]  = { 0x00, 0x00, 0x1e, 0x11, 0x11, 0x11, 0x11 };
static const uint8_t kLowerO[7]  = { 0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e };

static const uint8_t* glyphForChar(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return kDigitFont[ch - '0'];
    }
    switch (ch)
    {
    case 'F': return kLetterF;
    case 'A': return kLetterA;
    case 'B': return kLetterB;
    case 'C': return kLetterC;
    case 'D': return kLetterD;
    case 'E': return kLetterE;
    case 'G': return kLetterG;
    case 'I': return kLetterI;
    case 'L': return kLetterL;
    case 'N': return kLetterN;
    case 'O': return kLetterO;
    case 'P': return kLetterP;
    case 'R': return kLetterR;
    case 'S': return kLetterS;
    case 'T': return kLetterT;
    case 'U': return kLetterU;
    case 'X': return kLetterX;
    case 'Y': return kLetterY;
    case ':': return kColon;
    case 'e': return kLowerE;
    case 'g': return kLowerG;
    case 'i': return kLowerI;
    case 'n': return kLowerN;
    case 'o': return kLowerO;
    default: return NULL;
    }
}

struct VirtualControlButton
{
    const char* label;
    uint32_t controlMask;
    SDL_Rect rect;
    int dpadDx;
    int dpadDy;
    bool drawFrame;
};

static uint32_t g_virtualMouseControls = 0;
static bool g_virtualMouseButtonHeld = false;
static uint64_t g_virtualMouseReleaseTicks = 0;
static const uint64_t kVirtualMouseClickHoldMs = 180;
static uint64_t g_postRestoreInputBlockUntilTicks = 0;
static const uint64_t kPostRestoreInputBlockMs = 160;

// Virtual controls and gamepads both feed synthetic Dingoo controls; merge the
// sources before updating input state so releasing one source does not cancel another.
static uint32_t frontendSyntheticControlMask(void)
{
    return g_virtualMouseControls | g_gameControllerButtonControls | g_gameControllerAxisControls;
}

static void applyFrontendSyntheticControlMask(uint32_t oldMask, uint32_t newMask)
{
    uint32_t changed = oldMask ^ newMask;
    for (uint32_t bit = 0; bit < 32; ++bit)
    {
        uint32_t mask = 1u << bit;
        if (changed & mask)
        {
            inputSetSyntheticControl(bit, (newMask & mask) != 0);
        }
    }
}

static bool frontendPostRestoreInputBlocked(void)
{
    uint64_t until = g_postRestoreInputBlockUntilTicks;
    if (!until)
    {
        return false;
    }

    uint64_t now = SDL_GetTicks64();
    if (now < until)
    {
        return true;
    }
    g_postRestoreInputBlockUntilTicks = 0;
    return false;
}

static bool virtualControlsVisible(void)
{
    return g_frontendSettings && g_frontendSettings->showVirtualControls;
}

static bool portraitModeEnabled(void)
{
    return g_frontendSettings && g_frontendSettings->portraitMode;
}

static int displayWidthForSettings(const EmulatorSettings* settings)
{
    return (settings && settings->portraitMode) ? SCREEN_HEIGHT : SCREEN_WIDTH;
}

static int displayHeightForSettings(const EmulatorSettings* settings)
{
    return (settings && settings->portraitMode) ? SCREEN_WIDTH : SCREEN_HEIGHT;
}

static bool colorEffectNeedsPixelPostProcess(ColorEffectMode effect)
{
    return effect != COLOR_EFFECT_NORMAL && effect != COLOR_EFFECT_PIXEL_GRID;
}

static bool pixelGridEffectEnabled(void)
{
    return g_frontendSettings && g_frontendSettings->colorEffect == COLOR_EFFECT_PIXEL_GRID;
}

static uint16_t blendRgb565WithBlack(uint16_t pixel, uint32_t blackAlpha);

static bool pointInRect(int x, int y, const SDL_Rect& rect)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

static bool getVirtualControlCoordinateSize(int* outWidth, int* outHeight)
{
    if (!outWidth || !outHeight || !g_renderer)
    {
        return false;
    }

    int rendererWidth = 0;
    int rendererHeight = 0;
    SDL_GetRendererOutputSize(g_renderer, &rendererWidth, &rendererHeight);
    if (rendererWidth <= 0 || rendererHeight <= 0)
    {
        return false;
    }

    if (portraitModeEnabled())
    {
        *outWidth = rendererHeight;
        *outHeight = rendererWidth;
    }
    else
    {
        *outWidth = rendererWidth;
        *outHeight = rendererHeight;
    }
    return true;
}

static void mapRendererPointToVirtualControls(int* x, int* y)
{
    if (!x || !y || !portraitModeEnabled() || !g_renderer)
    {
        return;
    }

    int rendererWidth = 0;
    int rendererHeight = 0;
    SDL_GetRendererOutputSize(g_renderer, &rendererWidth, &rendererHeight);
    if (rendererWidth <= 0 || rendererHeight <= 0)
    {
        return;
    }

    int mappedX = rendererHeight - 1 - *y;
    int mappedY = *x;
    *x = mappedX;
    *y = mappedY;
}

static SDL_Rect rotateVirtualRectCcw(const SDL_Rect& rect)
{
    int width = 0;
    int height = 0;
    if (!getVirtualControlCoordinateSize(&width, &height))
    {
        return rect;
    }
    (void)height;

    SDL_Rect rotated = { rect.y, width - rect.x - rect.w, rect.h, rect.w };
    return rotated;
}

static void rotateVirtualPointCcw(int x, int y, int* outX, int* outY)
{
    int width = 0;
    int height = 0;
    if (!getVirtualControlCoordinateSize(&width, &height))
    {
        if (outX)
        {
            *outX = x;
        }
        if (outY)
        {
            *outY = y;
        }
        return;
    }
    (void)height;

    if (outX)
    {
        *outX = y;
    }
    if (outY)
    {
        *outY = width - 1 - x;
    }
}

static void renderVirtualFillRect(const SDL_Rect& rect)
{
    if (portraitModeEnabled())
    {
        SDL_Rect rotated = rotateVirtualRectCcw(rect);
        SDL_RenderFillRect(g_renderer, &rotated);
    }
    else
    {
        SDL_RenderFillRect(g_renderer, &rect);
    }
}

static void renderVirtualDrawRect(const SDL_Rect& rect)
{
    if (portraitModeEnabled())
    {
        SDL_Rect rotated = rotateVirtualRectCcw(rect);
        SDL_RenderDrawRect(g_renderer, &rotated);
    }
    else
    {
        SDL_RenderDrawRect(g_renderer, &rect);
    }
}

static void renderVirtualDrawLine(int x1, int y1, int x2, int y2)
{
    if (portraitModeEnabled())
    {
        int rx1 = 0;
        int ry1 = 0;
        int rx2 = 0;
        int ry2 = 0;
        rotateVirtualPointCcw(x1, y1, &rx1, &ry1);
        rotateVirtualPointCcw(x2, y2, &rx2, &ry2);
        SDL_RenderDrawLine(g_renderer, rx1, ry1, rx2, ry2);
    }
    else
    {
        SDL_RenderDrawLine(g_renderer, x1, y1, x2, y2);
    }
}

static void drawPixelGridOverlay(void)
{
    int width = 0;
    int height = 0;
    if (!getVirtualControlCoordinateSize(&width, &height) ||
        width <= SCREEN_WIDTH || height <= SCREEN_HEIGHT)
    {
        return;
    }

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 42);
    for (int x = 1; x < SCREEN_WIDTH; ++x)
    {
        int lineX = (int)(((int64_t)x * width) / SCREEN_WIDTH);
        renderVirtualDrawLine(lineX, 0, lineX, height - 1);
    }
    for (int y = 1; y < SCREEN_HEIGHT; ++y)
    {
        int lineY = (int)(((int64_t)y * height) / SCREEN_HEIGHT);
        renderVirtualDrawLine(0, lineY, width - 1, lineY);
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

static void drawVirtualGlyph(const uint8_t* glyph, int x, int y, int scale, SDL_Color color)
{
    SDL_SetRenderDrawColor(g_renderer, color.r, color.g, color.b, color.a);
    for (int row = 0; row < 7; ++row)
    {
        for (int col = 0; col < 5; ++col)
        {
            if (glyph[row] & (1 << (4 - col)))
            {
                SDL_Rect rect = { x + col * scale, y + row * scale, scale, scale };
                renderVirtualFillRect(rect);
            }
        }
    }
}

static void drawVirtualText(const char* text, int x, int y, int scale, SDL_Color color)
{
    int cursor = x;
    for (const char* p = text; *p; ++p)
    {
        const uint8_t* glyph = glyphForChar(*p);
        if (glyph)
        {
            drawVirtualGlyph(glyph, cursor, y, scale, color);
        }
        cursor += 6 * scale;
    }
}

static int rendererTextWidth(const char* text, int scale)
{
    return text && *text ? ((int)strlen(text) * 6 - 1) * scale : 0;
}

static void drawRendererGlyph(const uint8_t* glyph, int x, int y, int scale, SDL_Color color)
{
    if (!glyph)
    {
        return;
    }

    SDL_SetRenderDrawColor(g_renderer, color.r, color.g, color.b, color.a);
    for (int row = 0; row < 7; ++row)
    {
        for (int col = 0; col < 5; ++col)
        {
            if (glyph[row] & (1 << (4 - col)))
            {
                SDL_Rect rect = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(g_renderer, &rect);
            }
        }
    }
}

static void drawRendererText(const char* text, int x, int y, int scale, SDL_Color color)
{
    if (!text)
    {
        return;
    }

    int cursor = x;
    for (const char* p = text; *p; ++p)
    {
        const uint8_t* glyph = glyphForChar(*p);
        drawRendererGlyph(glyph, cursor, y, scale, color);
        cursor += 6 * scale;
    }
}

static uint64_t counterToUs(uint64_t counter, uint64_t frequency)
{
    return frequency ? counter * 1000000ull / frequency : 0;
}

static uint64_t idlePresentIntervalUs(uint64_t activePresentIntervalMs)
{
    uint64_t activeUs = activePresentIntervalMs * 1000ull;
    return activeUs > kIdlePresentIntervalUs ? activeUs : kIdlePresentIntervalUs;
}

static uint32_t idleLoopDelayMs(uint64_t nowCounter, uint64_t lastPresentCounter, uint64_t presentIntervalUs, uint64_t counterFrequency)
{
    if (!lastPresentCounter || !counterFrequency || presentIntervalUs <= kIdleWakeMarginUs)
    {
        return 1;
    }

    uint64_t elapsedUs = counterToUs(nowCounter - lastPresentCounter, counterFrequency);
    if (elapsedUs + kIdleWakeMarginUs >= presentIntervalUs)
    {
        return 1;
    }

    uint64_t delayMs = (presentIntervalUs - elapsedUs - kIdleWakeMarginUs) / 1000;
    if (delayMs > kIdleMaxWaitMs)
    {
        delayMs = kIdleMaxWaitMs;
    }
    return delayMs < 1 ? 1 : (uint32_t)delayMs;
}

static uint8_t blendChannel(uint8_t start, uint8_t end, int index, int count)
{
    if (count <= 1)
    {
        return end;
    }

    int value = (int)start + ((int)end - (int)start) * index / (count - 1);
    if (value < 0)
    {
        value = 0;
    }
    else if (value > 255)
    {
        value = 255;
    }
    return (uint8_t)value;
}

static void drawVerticalGradientRect(
    int x, int y, int w, int h,
    SDL_Color top, SDL_Color bottom)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    for (int row = 0; row < h; ++row)
    {
        SDL_SetRenderDrawColor(g_renderer,
            blendChannel(top.r, bottom.r, row, h),
            blendChannel(top.g, bottom.g, row, h),
            blendChannel(top.b, bottom.b, row, h),
            blendChannel(top.a, bottom.a, row, h));
        SDL_RenderDrawLine(g_renderer, x, y + row, x + w - 1, y + row);
    }
}

static uint32_t idleSymbolHash(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static double idleSymbolUnit(uint32_t seed)
{
    return (double)(idleSymbolHash(seed) & 0xffffu) / 65535.0;
}

static double clampDouble(double value, double minValue, double maxValue)
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

static void putPixelArgbClipped(uint32_t* pixels, int width, int height, int x, int y, uint8_t alpha)
{
    if (!pixels || x < 0 || y < 0 || x >= width || y >= height || alpha == 0)
    {
        return;
    }

    uint32_t* pixel = pixels + (size_t)y * (size_t)width + (size_t)x;
    uint8_t oldAlpha = (uint8_t)((*pixel >> 24) & 0xff);
    if (alpha > oldAlpha)
    {
        *pixel = ((uint32_t)alpha << 24) | 0x00ffffffu;
    }
}

static double distanceToSegment(double px, double py, double x1, double y1, double x2, double y2)
{
    double vx = x2 - x1;
    double vy = y2 - y1;
    double wx = px - x1;
    double wy = py - y1;
    double lengthSq = vx * vx + vy * vy;
    if (lengthSq <= 0.000001)
    {
        double dx = px - x1;
        double dy = py - y1;
        return sqrt(dx * dx + dy * dy);
    }

    double t = (wx * vx + wy * vy) / lengthSq;
    t = clampDouble(t, 0.0, 1.0);
    double cx = x1 + t * vx;
    double cy = y1 + t * vy;
    double dx = px - cx;
    double dy = py - cy;
    return sqrt(dx * dx + dy * dy);
}

static void drawTextureLine(uint32_t* pixels, int width, int height, double x1, double y1, double x2, double y2, double radius)
{
    int minX = (int)(clampDouble(floor((x1 < x2 ? x1 : x2) - radius - 2.0), 0.0, (double)(width - 1)));
    int maxX = (int)(clampDouble(ceil((x1 > x2 ? x1 : x2) + radius + 2.0), 0.0, (double)(width - 1)));
    int minY = (int)(clampDouble(floor((y1 < y2 ? y1 : y2) - radius - 2.0), 0.0, (double)(height - 1)));
    int maxY = (int)(clampDouble(ceil((y1 > y2 ? y1 : y2) + radius + 2.0), 0.0, (double)(height - 1)));

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            double dist = distanceToSegment((double)x + 0.5, (double)y + 0.5, x1, y1, x2, y2);
            double coverage = radius + 0.75 - dist;
            if (coverage <= 0.0)
            {
                continue;
            }
            if (coverage > 1.0)
            {
                coverage = 1.0;
            }
            putPixelArgbClipped(pixels, width, height, x, y, (uint8_t)(coverage * 255.0));
        }
    }
}

static void drawTextureCircle(uint32_t* pixels, int width, int height, double centerX, double centerY, double radius, double strokeRadius)
{
    int minX = (int)(clampDouble(floor(centerX - radius - strokeRadius - 2.0), 0.0, (double)(width - 1)));
    int maxX = (int)(clampDouble(ceil(centerX + radius + strokeRadius + 2.0), 0.0, (double)(width - 1)));
    int minY = (int)(clampDouble(floor(centerY - radius - strokeRadius - 2.0), 0.0, (double)(height - 1)));
    int maxY = (int)(clampDouble(ceil(centerY + radius + strokeRadius + 2.0), 0.0, (double)(height - 1)));

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            double dx = (double)x + 0.5 - centerX;
            double dy = (double)y + 0.5 - centerY;
            double dist = fabs(sqrt(dx * dx + dy * dy) - radius);
            double coverage = strokeRadius + 0.75 - dist;
            if (coverage <= 0.0)
            {
                continue;
            }
            if (coverage > 1.0)
            {
                coverage = 1.0;
            }
            putPixelArgbClipped(pixels, width, height, x, y, (uint8_t)(coverage * 255.0));
        }
    }
}

static void drawTextureSymbolSegment(
    uint32_t* pixels, int width, int height, double halfSize,
    double x1, double y1, double x2, double y2, double strokeRadius)
{
    double centerX = (double)width * 0.5;
    double centerY = (double)height * 0.5;
    drawTextureLine(pixels, width, height,
        centerX + x1 * halfSize, centerY + y1 * halfSize,
        centerX + x2 * halfSize, centerY + y2 * halfSize,
        strokeRadius);
}

static SDL_Texture* createIdleSymbolTexture(int type)
{
    // Cache idle symbols as alpha textures so the idle loop only animates placement.
    const int textureSize = 128;
    const double halfSize = textureSize * 0.42;
    const double strokeRadius = textureSize * 0.035;
    uint32_t pixels[textureSize * textureSize];
    memset(pixels, 0, sizeof(pixels));

    switch (type & 3)
    {
    case 0:
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.65, -0.65, 0.65, 0.65, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.65, 0.65, 0.65, -0.65, strokeRadius);
        break;
    case 1:
        drawTextureCircle(pixels, textureSize, textureSize,
            textureSize * 0.5, textureSize * 0.5, halfSize * 0.68, strokeRadius);
        break;
    case 2:
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.62, -0.62, 0.62, -0.62, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, 0.62, -0.62, 0.62, 0.62, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, 0.62, 0.62, -0.62, 0.62, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.62, 0.62, -0.62, -0.62, strokeRadius);
        break;
    default:
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, 0.0, -0.72, 0.72, 0.58, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, 0.72, 0.58, -0.72, 0.58, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.72, 0.58, 0.0, -0.72, strokeRadius);
        break;
    }

    SDL_Texture* texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, textureSize, textureSize);
    if (!texture)
    {
        return NULL;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    if (SDL_UpdateTexture(texture, NULL, pixels, textureSize * (int)sizeof(uint32_t)) != 0)
    {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    return texture;
}

static void resetIdleSymbolTextures(void)
{
    for (int i = 0; i < 4; ++i)
    {
        if (g_idleSymbolTextures[i])
        {
            SDL_DestroyTexture(g_idleSymbolTextures[i]);
            g_idleSymbolTextures[i] = NULL;
        }
    }
}

static SDL_Texture* idleSymbolTexture(int type)
{
    int index = type & 3;
    if (!g_idleSymbolTextures[index])
    {
        g_idleSymbolTextures[index] = createIdleSymbolTexture(index);
    }
    return g_idleSymbolTextures[index];
}

static void renderIdleSymbolTexture(SDL_Texture* texture, double centerX, double centerY, double size, double angle)
{
    SDL_FRect dst =
    {
        (float)(centerX - size * 0.5),
        (float)(centerY - size * 0.5),
        (float)size,
        (float)size
    };
    SDL_RenderCopyExF(g_renderer, texture, NULL, &dst, angle * 180.0 / kPi, NULL, SDL_FLIP_NONE);
}

static void drawIdleFloatingSymbol(int type, double centerX, double centerY, double size, double angle, SDL_Color color)
{
    SDL_Texture* texture = idleSymbolTexture(type);
    if (!texture)
    {
        return;
    }

    SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture, color.a);
    renderIdleSymbolTexture(texture, centerX, centerY, size, angle);
}

struct IdleSymbolSpec
{
    int type;
    double anchorX;
    double anchorY;
    double size;
    double driftX;
    double driftY;
    double speedX;
    double speedY;
    double phaseX;
    double phaseY;
    double rotationPhase;
    double rotationSpeed;
    double rotationAmount;
    uint8_t alpha;
};

struct IdleSymbolSample
{
    double x;
    double y;
    double angle;
};

struct IdleAnimationClock
{
    uint64_t timeMs = 0;
    uint64_t lastHostTicks = 0;

    void pause(void)
    {
        lastHostTicks = 0;
    }

    void reset(void)
    {
        timeMs = 0;
        pause();
    }

    uint64_t advance(uint64_t hostTicks)
    {
        if (!lastHostTicks)
        {
            lastHostTicks = hostTicks;
            return timeMs;
        }

        if (hostTicks > lastHostTicks)
        {
            timeMs += hostTicks - lastHostTicks;
        }
        lastHostTicks = hostTicks;
        return timeMs;
    }
};

static IdleAnimationClock g_idleAnimationClock;

static int idleSymbolCount(int width, int height, bool smallLayer)
{
    int count = smallLayer ? (width * height) / (80 * 80) : (width * height) / (120 * 120);
    int minCount = smallLayer ? 12 : 8;
    int maxCount = smallLayer ? 48 : 28;
    if (count < minCount)
    {
        return minCount;
    }
    if (count > maxCount)
    {
        return maxCount;
    }
    return count;
}

static IdleSymbolSpec buildIdleSymbolSpec(int width, int height, int index, bool smallLayer)
{
    uint32_t seed = smallLayer ? (uint32_t)index + 1009u : (uint32_t)index + 17u;
    double sizeBase = (double)(height < width ? height : width);
    double size = smallLayer ?
        sizeBase * (0.050 + idleSymbolUnit(seed * 19u + 301u) * 0.035) :
        sizeBase * (0.125 + idleSymbolUnit(seed * 19u + 3u) * 0.070);
    double minSize = smallLayer ? 14.0 : 28.0;
    if (size < minSize)
    {
        size = minSize;
    }

    IdleSymbolSpec spec = {};
    spec.type = index < 4 ? index : (int)(idleSymbolHash(seed * 71u + 37u) & 3u);
    spec.size = size;
    spec.anchorX = idleSymbolUnit(seed * 31u + 5u) * (double)width;
    spec.anchorY = idleSymbolUnit(seed * 41u + 7u) * (double)height;
    spec.driftX = smallLayer ?
        (10.0 + idleSymbolUnit(seed * 43u + 13u) * 16.0) :
        (20.0 + idleSymbolUnit(seed * 43u + 13u) * 28.0);
    spec.driftY = smallLayer ?
        (8.0 + idleSymbolUnit(seed * 47u + 17u) * 14.0) :
        (18.0 + idleSymbolUnit(seed * 47u + 17u) * 26.0);
    spec.speedX = smallLayer ?
        (0.30 + idleSymbolUnit(seed * 53u + 19u) * 0.22) :
        (0.20 + idleSymbolUnit(seed * 53u + 19u) * 0.20);
    spec.speedY = smallLayer ?
        (0.24 + idleSymbolUnit(seed * 59u + 23u) * 0.20) :
        (0.16 + idleSymbolUnit(seed * 59u + 23u) * 0.18);
    spec.phaseX = idleSymbolUnit(seed * 61u + 29u) * 2.0 * kPi;
    spec.phaseY = idleSymbolUnit(seed * 67u + 31u) * 2.0 * kPi;
    spec.rotationPhase = idleSymbolUnit(seed * 83u + 47u) * 2.0 * kPi;
    spec.rotationSpeed = smallLayer ?
        (0.22 + idleSymbolUnit(seed * 89u + 53u) * 0.18) :
        (0.14 + idleSymbolUnit(seed * 89u + 53u) * 0.14);
    spec.rotationAmount = smallLayer ? 0.34 : 0.42;
    spec.alpha = (uint8_t)(smallLayer ?
        (9 + (int)(idleSymbolUnit(seed * 13u + 11u) * 9.0)) :
        (24 + (int)(idleSymbolUnit(seed * 13u + 11u) * 17.0)));

    if (!smallLayer && index < 4)
    {
        static const double kAnchorX[4] = { 0.20, 0.80, 0.24, 0.76 };
        static const double kAnchorY[4] = { 0.25, 0.30, 0.75, 0.72 };
        spec.anchorX = kAnchorX[index] * (double)width +
            (idleSymbolUnit(seed * 73u + 41u) - 0.5) * (double)width * 0.06;
        spec.anchorY = kAnchorY[index] * (double)height +
            (idleSymbolUnit(seed * 79u + 43u) - 0.5) * (double)height * 0.06;
        spec.driftX = clampDouble(spec.driftX, 6.0, (double)width * 0.055);
        spec.driftY = clampDouble(spec.driftY, 6.0, (double)height * 0.055);
    }

    double visibleMargin = spec.size * 0.55;
    spec.anchorX = clampDouble(spec.anchorX,
        smallLayer ? -spec.size : visibleMargin,
        smallLayer ? (double)width + spec.size : (double)width - visibleMargin);
    spec.anchorY = clampDouble(spec.anchorY,
        smallLayer ? -spec.size : visibleMargin,
        smallLayer ? (double)height + spec.size : (double)height - visibleMargin);
    return spec;
}

static IdleSymbolSample sampleIdleSymbolMotion(const IdleSymbolSpec& spec, int width, int height, double t, int index, bool smallLayer)
{
    IdleSymbolSample sample =
    {
        spec.anchorX +
            sin(t * spec.speedX + spec.phaseX) * spec.driftX +
            sin(t * (spec.speedY * 0.37) + spec.phaseY) * spec.driftX * 0.18,
        spec.anchorY +
            cos(t * spec.speedY + spec.phaseY) * spec.driftY +
            sin(t * (spec.speedX * 0.41) + spec.phaseX) * spec.driftY * 0.16,
        sin(t * spec.rotationSpeed + spec.rotationPhase) * spec.rotationAmount
    };

    if (!smallLayer && index < 4)
    {
        double visibleMargin = spec.size * 0.55;
        sample.x = clampDouble(sample.x, visibleMargin, (double)width - visibleMargin);
        sample.y = clampDouble(sample.y, visibleMargin, (double)height - visibleMargin);
    }
    return sample;
}

static void drawIdleFloatingSymbolLayer(int width, int height, double t, int count, bool smallLayer)
{
    for (int i = 0; i < count; ++i)
    {
        IdleSymbolSpec spec = buildIdleSymbolSpec(width, height, i, smallLayer);
        IdleSymbolSample sample = sampleIdleSymbolMotion(spec, width, height, t, i, smallLayer);
        SDL_Color color = { 255, 255, 255, spec.alpha };
        drawIdleFloatingSymbol(spec.type, sample.x, sample.y, spec.size, sample.angle, color);
    }
}

static void drawIdleFloatingSymbols(int width, int height, double t)
{
    int mainCount = idleSymbolCount(width, height, false);
    int smallCount = idleSymbolCount(width, height, true);

    drawIdleFloatingSymbolLayer(width, height, t, smallCount, true);
    drawIdleFloatingSymbolLayer(width, height, t, mainCount, false);
}

static void resetIdleTitleTexture(void)
{
    if (g_idleTitleTexture)
    {
        SDL_DestroyTexture(g_idleTitleTexture);
        g_idleTitleTexture = NULL;
    }
    g_idleTitleTextureWidth = 0;
    g_idleTitleTextureHeight = 0;
}

static void resetIdleTextures(void)
{
    resetIdleTitleTexture();
    resetIdleSymbolTextures();
}

void frontendReleaseIdleResources(void)
{
    g_idleAnimationClock.reset();
    resetIdleTextures();
}

#ifdef _WIN32
static bool ensureIdleTitleTexture(int rendererWidth, int rendererHeight)
{
    if (g_idleTitleTexture && g_idleTitleTextureWidth > 0 && g_idleTitleTextureHeight > 0)
    {
        return true;
    }

    int fontSize = rendererHeight / 7;
    if (fontSize < 42)
    {
        fontSize = 42;
    }
    if (fontSize > 96)
    {
        fontSize = 96;
    }

    HDC measureDc = CreateCompatibleDC(NULL);
    if (!measureDc)
    {
        return false;
    }

    HFONT font = CreateFontW(
        -fontSize, 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    if (!font)
    {
        DeleteDC(measureDc);
        return false;
    }

    const wchar_t* title = L"DingooPie";
    HGDIOBJ oldFont = SelectObject(measureDc, font);
    SIZE textSize = {};
    bool ok = GetTextExtentPoint32W(measureDc, title, (int)wcslen(title), &textSize) != 0;
    SelectObject(measureDc, oldFont);
    DeleteDC(measureDc);
    if (!ok || textSize.cx <= 0 || textSize.cy <= 0)
    {
        DeleteObject(font);
        return false;
    }

    int textureWidth = textSize.cx + fontSize;
    int textureHeight = textSize.cy + fontSize / 2;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = textureWidth;
    bmi.bmiHeader.biHeight = -textureHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bitmap = dc ? CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
    if (!dc || !bitmap || !bits)
    {
        if (bitmap)
        {
            DeleteObject(bitmap);
        }
        if (dc)
        {
            DeleteDC(dc);
        }
        DeleteObject(font);
        return false;
    }

    memset(bits, 0, (size_t)textureWidth * (size_t)textureHeight * 4u);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT textRect = { fontSize / 2, fontSize / 6, textureWidth, textureHeight };
    DrawTextW(dc, title, -1, &textRect, DT_LEFT | DT_TOP | DT_NOCLIP | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);

    uint8_t* pixels = (uint8_t*)bits;
    for (int y = 0; y < textureHeight; ++y)
    {
        for (int x = 0; x < textureWidth; ++x)
        {
            uint8_t* p = pixels + ((size_t)y * (size_t)textureWidth + (size_t)x) * 4u;
            uint8_t alpha = p[0] > p[1] ? p[0] : p[1];
            alpha = alpha > p[2] ? alpha : p[2];
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = alpha;
        }
    }

    SDL_Texture* texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_BGRA32,
        SDL_TEXTUREACCESS_STATIC, textureWidth, textureHeight);
    if (texture)
    {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        if (SDL_UpdateTexture(texture, NULL, bits, textureWidth * 4) != 0)
        {
            SDL_DestroyTexture(texture);
            texture = NULL;
        }
    }

    DeleteObject(bitmap);
    DeleteDC(dc);
    DeleteObject(font);

    if (!texture)
    {
        return false;
    }

    g_idleTitleTexture = texture;
    g_idleTitleTextureWidth = textureWidth;
    g_idleTitleTextureHeight = textureHeight;
    return true;
}

static bool drawIdleTitle(int width, int height)
{
    if (!ensureIdleTitleTexture(width, height))
    {
        return false;
    }

    SDL_Rect dst =
    {
        (width - g_idleTitleTextureWidth) / 2,
        (height - g_idleTitleTextureHeight) / 2,
        g_idleTitleTextureWidth,
        g_idleTitleTextureHeight
    };
    SDL_SetTextureAlphaMod(g_idleTitleTexture, 248);
    SDL_RenderCopy(g_renderer, g_idleTitleTexture, NULL, &dst);
    return true;
}
#else
static bool drawIdleTitle(int width, int height)
{
    const char* title = "DingooPie";
    int scale = 10;
    int maxTitleWidth = width > 48 ? width - 48 : width;
    int maxTitleHeight = height > 32 ? height - 32 : height;
    while (scale > 2 &&
        (rendererTextWidth(title, scale) > maxTitleWidth || 7 * scale > maxTitleHeight))
    {
        --scale;
    }
    int titleWidth = rendererTextWidth(title, scale);
    int titleHeight = 7 * scale;
    int titleX = (width - titleWidth) / 2;
    int titleY = (height - titleHeight) / 2;

    SDL_Color shadow = { 0, 0, 0, 120 };
    SDL_Color text = { 244, 250, 255, 245 };
    drawRendererText(title, titleX + scale / 2, titleY + scale / 2, scale, shadow);
    drawRendererText(title, titleX, titleY, scale, text);
    return true;
}
#endif

static bool drawIdleScreen(uint64_t animationTimeMs)
{
    if (!g_renderer)
    {
        return false;
    }

    int width = 0;
    int height = 0;
    SDL_GetRendererOutputSize(g_renderer, &width, &height);
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
    SDL_Color bgTop = { 56, 110, 160, 255 };
    SDL_Color bgBottom = { 22, 58, 92, 255 };
    SDL_SetRenderDrawColor(g_renderer, bgTop.r, bgTop.g, bgTop.b, bgTop.a);
    if (SDL_RenderClear(g_renderer) != 0)
    {
        printf("frontend: idle SDL_RenderClear failed: %s\n", SDL_GetError());
        return false;
    }
    drawVerticalGradientRect(0, 0, width, height, bgTop, bgBottom);
    g_lastDisplayFrameValid = false;

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    double t = (double)animationTimeMs / 1000.0;
    drawIdleFloatingSymbols(width, height, t);
    if (!drawIdleTitle(width, height))
    {
        SDL_Color fallback = { 244, 250, 255, 245 };
        drawRendererText("DingooPie", width / 2 - rendererTextWidth("DingooPie", 7) / 2,
            height / 2 - 24, 7, fallback);
    }

    SDL_RenderPresent(g_renderer);
    return true;
}

static uint32_t virtualControlMask(uint32_t controlBit)
{
    return 1u << controlBit;
}

static bool virtualButtonHasControl(const VirtualControlButton& button, uint32_t controlBit)
{
    return (button.controlMask & virtualControlMask(controlBit)) != 0;
}

static bool virtualButtonPressed(const VirtualControlButton& button)
{
    return button.controlMask && (g_virtualMouseControls & button.controlMask) == button.controlMask;
}

static int buildVirtualControls(VirtualControlButton* outButtons, int maxButtons)
{
    if (!outButtons || maxButtons <= 0 || !g_renderer)
    {
        return 0;
    }

    int width = 0;
    int height = 0;
    if (!getVirtualControlCoordinateSize(&width, &height))
    {
        return 0;
    }

    const int unit = width < 500 ? 34 : 42;
    const int gap = unit / 5;
    const int margin = unit / 2;
    const int dpadX = margin;
    const int dpadY = height - margin - unit * 3;
    const int faceX = width - margin - unit * 3;
    const int faceY = height - margin - unit * 3;
    const int centerY = height - margin - unit;
    const int startSelectW = unit * 3 / 2;
    const int shoulderW = unit * 2;
    const int shoulderH = unit;

    int count = 0;
#define ADD_VIRTUAL_BUTTON_EX(text, mask, rx, ry, rw, rh, dx, dy, framed) \
    do { \
        if (count < maxButtons) { \
            outButtons[count].label = text; \
            outButtons[count].controlMask = mask; \
            outButtons[count].rect = SDL_Rect{ rx, ry, rw, rh }; \
            outButtons[count].dpadDx = dx; \
            outButtons[count].dpadDy = dy; \
            outButtons[count].drawFrame = framed; \
            ++count; \
        } \
    } while (0)

    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_UP) | virtualControlMask(CONTROL_DPAD_LEFT),
        dpadX, dpadY, unit, unit, -1, -1, false);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_UP) | virtualControlMask(CONTROL_DPAD_RIGHT),
        dpadX + unit * 2, dpadY, unit, unit, 1, -1, false);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_DOWN) | virtualControlMask(CONTROL_DPAD_RIGHT),
        dpadX + unit * 2, dpadY + unit * 2, unit, unit, 1, 1, false);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_DOWN) | virtualControlMask(CONTROL_DPAD_LEFT),
        dpadX, dpadY + unit * 2, unit, unit, -1, 1, false);

    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_UP),
        dpadX + unit, dpadY, unit, unit, 0, -1, true);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_LEFT),
        dpadX, dpadY + unit, unit, unit, -1, 0, true);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_RIGHT),
        dpadX + unit * 2, dpadY + unit, unit, unit, 1, 0, true);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_DOWN),
        dpadX + unit, dpadY + unit * 2, unit, unit, 0, 1, true);

    ADD_VIRTUAL_BUTTON_EX("X", virtualControlMask(CONTROL_BUTTON_X), faceX + unit, faceY, unit, unit, 0, 0, true);
    ADD_VIRTUAL_BUTTON_EX("Y", virtualControlMask(CONTROL_BUTTON_Y), faceX, faceY + unit, unit, unit, 0, 0, true);
    ADD_VIRTUAL_BUTTON_EX("A", virtualControlMask(CONTROL_BUTTON_A), faceX + unit * 2, faceY + unit, unit, unit, 0, 0, true);
    ADD_VIRTUAL_BUTTON_EX("B", virtualControlMask(CONTROL_BUTTON_B), faceX + unit, faceY + unit * 2, unit, unit, 0, 0, true);

    ADD_VIRTUAL_BUTTON_EX("SELECT", virtualControlMask(CONTROL_BUTTON_SELECT), width / 2 - startSelectW - gap, centerY + unit / 8, startSelectW, unit * 3 / 4, 0, 0, true);
    ADD_VIRTUAL_BUTTON_EX("START", virtualControlMask(CONTROL_BUTTON_START), width / 2 + gap, centerY + unit / 8, startSelectW, unit * 3 / 4, 0, 0, true);

    ADD_VIRTUAL_BUTTON_EX("L", virtualControlMask(CONTROL_TRIGGER_LEFT), margin, margin, shoulderW, shoulderH, 0, 0, true);
    ADD_VIRTUAL_BUTTON_EX("R", virtualControlMask(CONTROL_TRIGGER_RIGHT), width - margin - shoulderW, margin, shoulderW, shoulderH, 0, 0, true);

#undef ADD_VIRTUAL_BUTTON_EX
    return count;
}

static void releaseVirtualMouseControls(void)
{
    g_virtualMouseButtonHeld = false;
    if (!g_virtualMouseControls)
    {
        g_virtualMouseReleaseTicks = 0;
        return;
    }

    uint32_t oldSyntheticMask = frontendSyntheticControlMask();
    g_virtualMouseControls = 0;
    g_virtualMouseReleaseTicks = 0;
    applyFrontendSyntheticControlMask(oldSyntheticMask, frontendSyntheticControlMask());
}

static uint32_t hitTestVirtualControls(int x, int y)
{
    mapRendererPointToVirtualControls(&x, &y);

    VirtualControlButton buttons[16];
    int count = buildVirtualControls(buttons, 16);
    for (int i = 0; i < count; ++i)
    {
        if (pointInRect(x, y, buttons[i].rect))
        {
            return buttons[i].controlMask;
        }
    }
    return 0;
}

static void updateVirtualMouseControls(uint32_t newMask)
{
    if (newMask)
    {
        g_virtualMouseReleaseTicks = 0;
    }

    uint32_t changed = g_virtualMouseControls ^ newMask;
    if (!changed)
    {
        return;
    }

    uint32_t oldSyntheticMask = frontendSyntheticControlMask();
    g_virtualMouseControls = newMask;
    applyFrontendSyntheticControlMask(oldSyntheticMask, frontendSyntheticControlMask());
}

static void scheduleVirtualMouseRelease(void)
{
    if (!g_virtualMouseControls)
    {
        g_virtualMouseReleaseTicks = 0;
        return;
    }
    g_virtualMouseReleaseTicks = SDL_GetTicks64() + kVirtualMouseClickHoldMs;
}

static void updateVirtualMouseReleaseTimer(void)
{
    if (!g_virtualMouseButtonHeld && g_virtualMouseReleaseTicks && SDL_GetTicks64() >= g_virtualMouseReleaseTicks)
    {
        releaseVirtualMouseControls();
    }
}

static bool handleVirtualControlMouseEvent(const SDL_Event& ev)
{
    if (!virtualControlsVisible())
    {
        releaseVirtualMouseControls();
        return false;
    }

    bool leftMouseEvent =
        (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) ||
        (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) ||
        (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_LMASK));
    if (leftMouseEvent && frontendPostRestoreInputBlocked())
    {
        releaseVirtualMouseControls();
        if (inputTraceEnabled())
        {
            printf("frontend: virtual mouse ignored after restore type=%u\n",
                (unsigned int)ev.type);
        }
        return true;
    }

    if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT)
    {
        uint32_t mask = hitTestVirtualControls(ev.button.x, ev.button.y);
        if (mask)
        {
            g_virtualMouseButtonHeld = true;
            updateVirtualMouseControls(mask);
            if (inputTraceEnabled())
            {
                printf("frontend: virtual mouse down mask=0x%08X x=%d y=%d\n",
                    (unsigned int)mask, ev.button.x, ev.button.y);
            }
            return true;
        }
    }
    else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT)
    {
        if (g_virtualMouseButtonHeld || g_virtualMouseControls)
        {
            g_virtualMouseButtonHeld = false;
            scheduleVirtualMouseRelease();
            if (inputTraceEnabled())
            {
                printf("frontend: virtual mouse release scheduled mask=0x%08X hold_ms=%llu\n",
                    (unsigned int)g_virtualMouseControls,
                    (unsigned long long)kVirtualMouseClickHoldMs);
            }
            return true;
        }
    }
    else if (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_LMASK))
    {
        if (g_virtualMouseButtonHeld)
        {
            updateVirtualMouseControls(hitTestVirtualControls(ev.motion.x, ev.motion.y));
            return true;
        }
    }

    return false;
}

static void drawVirtualButton(const VirtualControlButton& button)
{
    bool pressed = virtualButtonPressed(button);
    if (button.dpadDx || button.dpadDy)
    {
        const int cx = button.rect.x + button.rect.w / 2;
        const int cy = button.rect.y + button.rect.h / 2;
        const int minSide = button.rect.w < button.rect.h ? button.rect.w : button.rect.h;
        const int spread = minSide / 5;
        const int depth = minSide / 7;
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        if (button.drawFrame)
        {
            SDL_SetRenderDrawColor(g_renderer, 16, 18, 20, pressed ? 220 : 155);
            renderVirtualFillRect(button.rect);
            SDL_SetRenderDrawColor(g_renderer, pressed ? 255 : 210, pressed ? 255 : 220, pressed ? 255 : 230, 210);
            renderVirtualDrawRect(button.rect);
        }

        SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, pressed ? 255 : 235);
        const int tipX = cx + button.dpadDx * depth;
        const int tipY = cy + button.dpadDy * depth;
        const int baseX = cx - button.dpadDx * depth;
        const int baseY = cy - button.dpadDy * depth;
        const int perpX = -button.dpadDy * spread;
        const int perpY = button.dpadDx * spread;
        renderVirtualDrawLine(tipX, tipY, baseX + perpX, baseY + perpY);
        renderVirtualDrawLine(tipX, tipY, baseX - perpX, baseY - perpY);
        return;
    }

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 16, 18, 20, pressed ? 220 : 155);
    renderVirtualFillRect(button.rect);
    SDL_SetRenderDrawColor(g_renderer, pressed ? 255 : 210, pressed ? 255 : 220, pressed ? 255 : 230, 210);
    renderVirtualDrawRect(button.rect);

    int scale = 1;
    if (virtualButtonHasControl(button, CONTROL_BUTTON_A) ||
        virtualButtonHasControl(button, CONTROL_BUTTON_B) ||
        virtualButtonHasControl(button, CONTROL_BUTTON_X) ||
        virtualButtonHasControl(button, CONTROL_BUTTON_Y) ||
        virtualButtonHasControl(button, CONTROL_TRIGGER_LEFT) ||
        virtualButtonHasControl(button, CONTROL_TRIGGER_RIGHT))
    {
        scale = 2;
    }
    else if (virtualButtonHasControl(button, CONTROL_BUTTON_START) ||
        virtualButtonHasControl(button, CONTROL_BUTTON_SELECT))
    {
        scale = 1;
    }
    else if (button.rect.w >= 70)
    {
        scale = 2;
    }
    int textWidth = (int)strlen(button.label) * 6 * scale;
    int textHeight = 7 * scale;
    int textX = button.rect.x + (button.rect.w - textWidth) / 2;
    int textY = button.rect.y + (button.rect.h - textHeight) / 2;
    SDL_Color color = { 255, 255, 255, 235 };
    drawVirtualText(button.label, textX, textY, scale, color);
}

static void drawVirtualControlsOverlay(void)
{
    if (!virtualControlsVisible())
    {
        releaseVirtualMouseControls();
        return;
    }

    VirtualControlButton buttons[16];
    int count = buildVirtualControls(buttons, 16);
    for (int i = 0; i < count; ++i)
    {
        drawVirtualButton(buttons[i]);
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

static bool inputTraceEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_INPUT_TRACE");
        enabled = (value && value[0] && value[0] != '0') ? 1 : 0;
    }
    return enabled != 0;
}

static bool pathEndsWithIgnoreCase(const char* path, const char* suffix)
{
    if (!path || !suffix)
    {
        return false;
    }

    size_t pathLen = strlen(path);
    size_t suffixLen = strlen(suffix);
    if (pathLen < suffixLen)
    {
        return false;
    }

    const char* tail = path + pathLen - suffixLen;
    for (size_t i = 0; i < suffixLen; ++i)
    {
        if (tolower((unsigned char)tail[i]) != tolower((unsigned char)suffix[i]))
        {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32
static std::wstring utf8ToWideLocal(const char* text)
{
    if (!text)
    {
        return L"";
    }

    int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (size <= 0)
    {
        return L"";
    }

    std::wstring out((size_t)size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &out[0], size);
    return out;
}

static FILE* openWriteBinaryUtf8(const char* path)
{
    std::wstring widePath = utf8ToWideLocal(path);
    if (widePath.empty())
    {
        return NULL;
    }
    return _wfopen(widePath.c_str(), L"wb");
}
#else
static FILE* openWriteBinaryUtf8(const char* path)
{
    return fopen(path, "wb");
}
#endif

static void writeLe16File(FILE* fp, uint16_t value)
{
    fputc((int)(value & 0xff), fp);
    fputc((int)((value >> 8) & 0xff), fp);
}

static void writeLe32File(FILE* fp, uint32_t value)
{
    fputc((int)(value & 0xff), fp);
    fputc((int)((value >> 8) & 0xff), fp);
    fputc((int)((value >> 16) & 0xff), fp);
    fputc((int)((value >> 24) & 0xff), fp);
}

static void rotateFrameCcw(uint16_t* dst, const uint16_t* src)
{
    if (!dst || !src)
    {
        return;
    }

    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        for (int x = 0; x < SCREEN_WIDTH; ++x)
        {
            int dstX = y;
            int dstY = SCREEN_WIDTH - 1 - x;
            dst[dstY * SCREEN_HEIGHT + dstX] = src[y * SCREEN_WIDTH + x];
        }
    }
}

static bool writeRgb565Bmp(const char* path, const uint16_t* pixels, int width, int height)
{
    if (!path || !path[0] || !pixels)
    {
        return false;
    }

    FILE* fp = openWriteBinaryUtf8(path);
    if (!fp)
    {
        printf("frontend: failed to open screenshot path: %s\n", path);
        return false;
    }

    const uint32_t rowBytes = (uint32_t)width * 2;
    const uint32_t paddedRowBytes = (rowBytes + 3u) & ~3u;
    const uint32_t imageBytes = paddedRowBytes * (uint32_t)height;
    const uint32_t dibHeaderBytes = 40;
    const uint32_t bitfieldBytes = 12;
    const uint32_t pixelOffset = 14 + dibHeaderBytes + bitfieldBytes;
    const uint32_t fileBytes = pixelOffset + imageBytes;

    fwrite("BM", 1, 2, fp);
    writeLe32File(fp, fileBytes);
    writeLe16File(fp, 0);
    writeLe16File(fp, 0);
    writeLe32File(fp, pixelOffset);
    writeLe32File(fp, dibHeaderBytes);
    writeLe32File(fp, (uint32_t)width);
    writeLe32File(fp, (uint32_t)height);
    writeLe16File(fp, 1);
    writeLe16File(fp, 16);
    writeLe32File(fp, 3);
    writeLe32File(fp, imageBytes);
    writeLe32File(fp, 0);
    writeLe32File(fp, 0);
    writeLe32File(fp, 0);
    writeLe32File(fp, 0);
    writeLe32File(fp, 0x0000F800);
    writeLe32File(fp, 0x000007E0);
    writeLe32File(fp, 0x0000001F);

    const uint8_t padding[3] = { 0, 0, 0 };
    const uint32_t paddingBytes = paddedRowBytes - rowBytes;
    for (int y = height - 1; y >= 0; --y)
    {
        fwrite(pixels + y * width, 1, rowBytes, fp);
        if (paddingBytes)
        {
            fwrite(padding, 1, paddingBytes, fp);
        }
    }

    bool ok = ferror(fp) == 0;
    fclose(fp);
    return ok;
}

#ifdef _WIN32
static int getImageEncoderClsid(const wchar_t* mimeType, CLSID* clsid)
{
    UINT encoderCount = 0;
    UINT encoderBytes = 0;
    Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes);
    if (encoderBytes == 0)
    {
        return -1;
    }

    Gdiplus::ImageCodecInfo* encoders = (Gdiplus::ImageCodecInfo*)malloc(encoderBytes);
    if (!encoders)
    {
        return -1;
    }

    int result = -1;
    if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders) == Gdiplus::Ok)
    {
        for (UINT i = 0; i < encoderCount; ++i)
        {
            if (wcscmp(encoders[i].MimeType, mimeType) == 0)
            {
                *clsid = encoders[i].Clsid;
                result = 0;
                break;
            }
        }
    }

    free(encoders);
    return result;
}

static bool writeRgb565GdiplusImage(const char* path, const uint16_t* pixels, int width, int height, const wchar_t* mimeType)
{
    if (!path || !path[0] || !pixels)
    {
        return false;
    }

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, NULL) != Gdiplus::Ok)
    {
        printf("frontend: GDI+ startup failed for screenshot\n");
        return false;
    }

    bool ok = false;
    CLSID encoderClsid;
    std::wstring widePath = utf8ToWideLocal(path);
    if (!widePath.empty() && getImageEncoderClsid(mimeType, &encoderClsid) == 0)
    {
        // Keep all GDI+ objects inside this scope so their destructors run
        // before GdiplusShutdown. Destroying Bitmap after shutdown can crash.
        Gdiplus::Bitmap bitmap(width, height, PixelFormat24bppRGB);
        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData data;
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &data) == Gdiplus::Ok)
        {
            for (int y = 0; y < height; ++y)
            {
                uint8_t* row = (uint8_t*)data.Scan0 + y * data.Stride;
                for (int x = 0; x < width; ++x)
                {
                    uint16_t c = pixels[y * width + x];
                    uint8_t r = (uint8_t)(((c >> 11) & 0x1f) * 255 / 31);
                    uint8_t g = (uint8_t)(((c >> 5) & 0x3f) * 255 / 63);
                    uint8_t b = (uint8_t)((c & 0x1f) * 255 / 31);
                    row[x * 3 + 0] = b;
                    row[x * 3 + 1] = g;
                    row[x * 3 + 2] = r;
                }
            }
            bitmap.UnlockBits(&data);

            ok = bitmap.Save(widePath.c_str(), &encoderClsid, NULL) == Gdiplus::Ok;
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return ok;
}

static bool writeRgb565Png(const char* path, const uint16_t* pixels, int width, int height)
{
    return writeRgb565GdiplusImage(path, pixels, width, height, L"image/png");
}

static bool writeRgb565Jpeg(const char* path, const uint16_t* pixels, int width, int height)
{
    return writeRgb565GdiplusImage(path, pixels, width, height, L"image/jpeg");
}
#else
static bool writeRgb565Png(const char* path, const uint16_t* pixels, int width, int height)
{
    (void)path;
    (void)pixels;
    (void)width;
    (void)height;
    return false;
}

static bool writeRgb565Jpeg(const char* path, const uint16_t* pixels, int width, int height)
{
    (void)path;
    (void)pixels;
    (void)width;
    (void)height;
    return false;
}
#endif

static bool writeScreenshotByExtension(const char* path, const uint16_t* pixels, int width, int height)
{
    if (pathEndsWithIgnoreCase(path, ".bmp"))
    {
        return writeRgb565Bmp(path, pixels, width, height);
    }
    if (pathEndsWithIgnoreCase(path, ".jpg") || pathEndsWithIgnoreCase(path, ".jpeg"))
    {
        return writeRgb565Jpeg(path, pixels, width, height);
    }
    return writeRgb565Png(path, pixels, width, height);
}

static bool buildAutoScreenshotPath(char* path, size_t pathSize)
{
    if (!path || pathSize == 0)
    {
        return false;
    }

    time_t now = time(NULL);
    struct tm tmValue;
#ifdef _WIN32
    localtime_s(&tmValue, &now);
#else
    localtime_r(&now, &tmValue);
#endif
    int written = snprintf(path, pathSize, "dingoo-screenshot-%04d%02d%02d-%02d%02d%02d.png",
        tmValue.tm_year + 1900,
        tmValue.tm_mon + 1,
        tmValue.tm_mday,
        tmValue.tm_hour,
        tmValue.tm_min,
        tmValue.tm_sec);
    return written > 0 && (size_t)written < pathSize;
}

static bool getScreenshotOutputSize(int* outWidth, int* outHeight)
{
    if (!outWidth || !outHeight)
    {
        return false;
    }

    int width = 0;
    int height = 0;
    if (g_renderer)
    {
        SDL_GetRendererOutputSize(g_renderer, &width, &height);
    }
    if ((width <= 0 || height <= 0) && g_window)
    {
        SDL_GetWindowSize(g_window, &width, &height);
    }
    if (width <= 0 || height <= 0)
    {
        width = g_lastDisplayFrameWidth;
        height = g_lastDisplayFrameHeight;
    }
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    *outWidth = width;
    *outHeight = height;
    return true;
}

static void applyPixelGridToDisplayPixels(uint16_t* pixels, int width, int height, int sourceWidth, int sourceHeight);

static bool buildDisplaySizedScreenshot(std::vector<uint16_t>* outPixels, int* outWidth, int* outHeight)
{
    if (!outPixels || !outWidth || !outHeight || !g_lastDisplayFrameValid)
    {
        return false;
    }

    int width = 0;
    int height = 0;
    if (!getScreenshotOutputSize(&width, &height))
    {
        return false;
    }

    outPixels->resize((size_t)width * (size_t)height);
    for (int y = 0; y < height; ++y)
    {
        int srcY = (int)(((int64_t)y * g_lastDisplayFrameHeight) / height);
        if (srcY >= g_lastDisplayFrameHeight)
        {
            srcY = g_lastDisplayFrameHeight - 1;
        }
        for (int x = 0; x < width; ++x)
        {
            int srcX = (int)(((int64_t)x * g_lastDisplayFrameWidth) / width);
            if (srcX >= g_lastDisplayFrameWidth)
            {
                srcX = g_lastDisplayFrameWidth - 1;
            }
            (*outPixels)[(size_t)y * (size_t)width + (size_t)x] =
                g_lastDisplayFrame[(size_t)srcY * (size_t)g_lastDisplayFrameWidth + (size_t)srcX];
        }
    }

    if (pixelGridEffectEnabled())
    {
        applyPixelGridToDisplayPixels(outPixels->data(), width, height,
            g_lastDisplayFrameWidth, g_lastDisplayFrameHeight);
    }

    *outWidth = width;
    *outHeight = height;
    return true;
}

static void applyPixelGridToDisplayPixels(uint16_t* pixels, int width, int height, int sourceWidth, int sourceHeight)
{
    if (!pixels || width <= sourceWidth || height <= sourceHeight || sourceWidth <= 0 || sourceHeight <= 0)
    {
        return;
    }

    for (int y = 0; y < height; ++y)
    {
        int sourceY = (int)(((int64_t)y * sourceHeight) / height);
        int nextSourceY = (int)(((int64_t)(y + 1) * sourceHeight) / height);
        bool horizontalEdge = nextSourceY > sourceY;
        if (sourceY >= sourceHeight)
        {
            sourceY = sourceHeight - 1;
        }

        for (int x = 0; x < width; ++x)
        {
            int sourceX = (int)(((int64_t)x * sourceWidth) / width);
            int nextSourceX = (int)(((int64_t)(x + 1) * sourceWidth) / width);
            bool verticalEdge = nextSourceX > sourceX;
            if (!horizontalEdge && !verticalEdge)
            {
                continue;
            }

            size_t index = (size_t)y * (size_t)width + (size_t)x;
            uint32_t alpha = (horizontalEdge && verticalEdge) ? 52u : 34u;
            pixels[index] = blendRgb565WithBlack(pixels[index], alpha);
        }
    }
}

static uint64_t parsePositiveEnv(const char* name, uint64_t defaultValue, uint64_t minValue, uint64_t maxValue)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return defaultValue;
    }

    uint64_t parsed = strtoull(value, NULL, 10);
    if (parsed < minValue)
    {
        return minValue;
    }
    if (parsed > maxValue)
    {
        return maxValue;
    }
    return parsed;
}

static void disableTextComposition(void)
{
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "0");
    SDL_StopTextInput();
    SDL_EventState(SDL_TEXTINPUT, SDL_IGNORE);
    SDL_EventState(SDL_TEXTEDITING, SDL_IGNORE);
}

static void enableTextComposition(void)
{
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);
    SDL_EventState(SDL_TEXTEDITING, SDL_ENABLE);
}

#ifdef _WIN32
static void registerRawKeyboardInput(void)
{
    if (!g_nativeWindow)
    {
        return;
    }

    RAWINPUTDEVICE device;
    memset(&device, 0, sizeof(device));
    device.usUsagePage = 0x01;
    device.usUsage = 0x06;
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = g_nativeWindow;

    if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
    {
        printf("frontend: RegisterRawInputDevices failed: %lu\n", GetLastError());
        return;
    }

    printf("frontend: raw keyboard input registered\n");
}

static int normalizeRawVirtualKey(const RAWKEYBOARD& keyboard)
{
    int virtualKey = (int)keyboard.VKey;
    if (virtualKey == VK_SHIFT)
    {
        UINT scanCode = keyboard.MakeCode;
        if (keyboard.Flags & RI_KEY_E0)
        {
            scanCode |= 0xE000;
        }
        virtualKey = (int)MapVirtualKey(scanCode, MAPVK_VSC_TO_VK_EX);
    }
    return virtualKey;
}

static void handleRawKeyboardInput(HRAWINPUT rawInput)
{
    UINT size = 0;
    if (GetRawInputData(rawInput, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
    {
        return;
    }

    uint8_t stackBuffer[sizeof(RAWINPUT) + 64];
    uint8_t* buffer = stackBuffer;
    if (size > sizeof(stackBuffer))
    {
        buffer = (uint8_t*)malloc(size);
        if (!buffer)
        {
            return;
        }
    }

    UINT read = GetRawInputData(rawInput, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER));
    if (read == size)
    {
        const RAWINPUT* raw = (const RAWINPUT*)buffer;
        if (raw->header.dwType == RIM_TYPEKEYBOARD)
        {
            const RAWKEYBOARD& keyboard = raw->data.keyboard;
            int virtualKey = normalizeRawVirtualKey(keyboard);
            bool pressed = (keyboard.Flags & RI_KEY_BREAK) == 0;
            uint32_t windowFlags = g_window ? SDL_GetWindowFlags(g_window) : 0;
            bool focused = g_nativeWindow && GetForegroundWindow() == g_nativeWindow;
            bool acceptsInput = focused ||
                ((windowFlags & SDL_WINDOW_SHOWN) != 0 && (windowFlags & SDL_WINDOW_MINIMIZED) == 0);

            if (inputTraceEnabled())
            {
                printf("frontend: raw key vk=0x%02X make=0x%X flags=0x%X pressed=%u focused=%u accepted=%u\n",
                    virtualKey,
                    (unsigned int)keyboard.MakeCode,
                    (unsigned int)keyboard.Flags,
                    pressed ? 1u : 0u,
                    focused ? 1u : 0u,
                    acceptsInput ? 1u : 0u);
            }

            if (acceptsInput && !inputMappingUiKeyboardCapturePending() && !frontendPostRestoreInputBlocked())
            {
                inputHandleHostVirtualKey(virtualKey, pressed);
            }
        }
    }

    if (buffer != stackBuffer)
    {
        free(buffer);
    }
}

static void handleSystemWindowEvent(const SDL_Event& ev)
{
    if (!ev.syswm.msg || ev.syswm.msg->subsystem != SDL_SYSWM_WINDOWS)
    {
        return;
    }

    const SDL_SysWMmsg* msg = ev.syswm.msg;
    if (msg->msg.win.msg == WM_INPUT)
    {
        handleRawKeyboardInput((HRAWINPUT)msg->msg.win.lParam);
    }
}
#endif

#ifdef _WIN32
static void setMenuLoopPauseActive(bool active)
{
    if (g_menuLoopPauseActive == active)
    {
        return;
    }

    g_menuLoopPauseActive = active;
    if (active)
    {
        frontendBeginModalPause();
    }
    else
    {
        frontendEndModalPause();
    }
}

static LRESULT CALLBACK nativeWindowSubclassProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR refData)
{
    (void)subclassId;
    (void)refData;

    if (msg == WM_ERASEBKGND)
    {
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect((HDC)wParam, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    if (msg == WM_ENTERMENULOOP)
    {
        setMenuLoopPauseActive(true);
    }
    else if (msg == WM_EXITMENULOOP)
    {
        setMenuLoopPauseActive(false);
    }
    else if (msg == WM_NCDESTROY)
    {
        setMenuLoopPauseActive(false);
    }
    else if (msg == WM_INITMENUPOPUP)
    {
        frontendMenuRefresh();
    }
    else if (msg == WM_COMMAND)
    {
        if (frontendMenuHandleCommand(LOWORD(wParam)))
        {
            return 0;
        }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void installNativeWindowSubclass(void)
{
    if (!g_nativeWindow)
    {
        return;
    }
    if (!SetWindowSubclass(g_nativeWindow, nativeWindowSubclassProc, 1, 0))
    {
        printf("frontend: SetWindowSubclass failed: %lu\n", GetLastError());
    }
}

static void uninstallNativeWindowSubclass(void)
{
    if (g_nativeWindow)
    {
        RemoveWindowSubclass(g_nativeWindow, nativeWindowSubclassProc, 1);
    }
}

static bool updateNativeWindowHandle(void)
{
    if (!g_window)
    {
        return false;
    }

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(g_window, &info))
    {
        printf("frontend: SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        return false;
    }

    if (info.subsystem != SDL_SYSWM_WINDOWS || !info.info.win.window)
    {
        printf("frontend: native HWND unavailable for SDL subsystem=%d\n", (int)info.subsystem);
        return false;
    }

    g_nativeWindow = info.info.win.window;
    return true;
}
#endif

static void disableWindowIme(void)
{
#ifdef _WIN32
    if (!g_nativeWindow && !updateNativeWindowHandle())
    {
        return;
    }

    if (!g_defaultImeContext)
    {
        g_defaultImeContext = ImmGetContext(g_nativeWindow);
        if (g_defaultImeContext)
        {
            ImmReleaseContext(g_nativeWindow, g_defaultImeContext);
        }
    }
    ImmAssociateContext(g_nativeWindow, NULL);
    disableTextComposition();
    printf("frontend: Windows IME disabled for SDL window\n");
#else
    disableTextComposition();
#endif
}

static void enableWindowIme(void)
{
#ifdef _WIN32
    if (!g_nativeWindow)
    {
        updateNativeWindowHandle();
    }
    if (g_nativeWindow)
    {
        ImmAssociateContext(g_nativeWindow, g_defaultImeContext);
    }
#endif
    enableTextComposition();
    printf("frontend: IME enabled for SDL window\n");
}

static const char* windowEventName(uint8_t eventType)
{
    switch (eventType)
    {
    case SDL_WINDOWEVENT_SHOWN: return "shown";
    case SDL_WINDOWEVENT_HIDDEN: return "hidden";
    case SDL_WINDOWEVENT_EXPOSED: return "exposed";
    case SDL_WINDOWEVENT_MOVED: return "moved";
    case SDL_WINDOWEVENT_RESIZED: return "resized";
    case SDL_WINDOWEVENT_SIZE_CHANGED: return "size_changed";
    case SDL_WINDOWEVENT_MINIMIZED: return "minimized";
    case SDL_WINDOWEVENT_MAXIMIZED: return "maximized";
    case SDL_WINDOWEVENT_RESTORED: return "restored";
    case SDL_WINDOWEVENT_ENTER: return "enter";
    case SDL_WINDOWEVENT_LEAVE: return "leave";
    case SDL_WINDOWEVENT_FOCUS_GAINED: return "focus_gained";
    case SDL_WINDOWEVENT_FOCUS_LOST: return "focus_lost";
    case SDL_WINDOWEVENT_CLOSE: return "close";
    case SDL_WINDOWEVENT_TAKE_FOCUS: return "take_focus";
    case SDL_WINDOWEVENT_HIT_TEST: return "hit_test";
    default: return "unknown";
    }
}

static SDL_JoystickID activeGameControllerInstanceId(void)
{
    if (!g_gameController)
    {
        return -1;
    }
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(g_gameController);
    return joystick ? SDL_JoystickInstanceID(joystick) : -1;
}

static void applyGameControllerControlMasks(uint32_t buttonMask, uint32_t axisMask)
{
    uint32_t oldSyntheticMask = frontendSyntheticControlMask();
    g_gameControllerButtonControls = buttonMask;
    g_gameControllerAxisControls = axisMask;
    applyFrontendSyntheticControlMask(oldSyntheticMask, frontendSyntheticControlMask());
}

static void releaseGameControllerControls(void)
{
    memset(g_gameControllerAxes, 0, sizeof(g_gameControllerAxes));
    applyGameControllerControlMasks(0, 0);
}

static void releaseFrontendInputControls(void)
{
    releaseVirtualMouseControls();
    releaseGameControllerControls();
    inputClearSyntheticControls();
    inputClearControls();
}

void frontendResetInputAfterStateRestore(void)
{
    releaseVirtualMouseControls();
    releaseGameControllerControls();
    inputResetTransientControls();
    g_postRestoreInputBlockUntilTicks = SDL_GetTicks64() + kPostRestoreInputBlockMs;
    if (inputTraceEnabled())
    {
        printf("frontend: post-restore input reset block_ms=%llu\n",
            (unsigned long long)kPostRestoreInputBlockMs);
    }
}

bool frontendGamePaused(void)
{
    return SDL_AtomicGet(&g_gamePaused) != 0;
}

bool frontendUserGamePaused(void)
{
    return g_userPauseRequested;
}

static bool frontendEffectivePauseRequested(void)
{
    return g_userPauseRequested || g_minimizedPauseActive || g_modalPauseDepth > 0;
}

static bool isWindowMinimized(void)
{
    return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) != 0;
}

static MinimizedBehavior currentMinimizedBehavior(void)
{
    return g_frontendSettings ? g_frontendSettings->minimizedBehavior : MINIMIZED_BEHAVIOR_PAUSE;
}

static void applyFrontendPauseState(bool refreshMenu)
{
    bool paused = frontendEffectivePauseRequested();
    if (frontendGamePaused() == paused)
    {
        if (refreshMenu)
        {
            frontendMenuRefresh();
        }
        return;
    }

    SDL_AtomicSet(&g_gamePaused, paused ? 1 : 0);
    if (paused)
    {
        // Menus and dialogs can block this thread, so freeze idle motion before
        // control enters native modal code.
        g_idleAnimationClock.pause();
        releaseFrontendInputControls();
    }
    pauseGateSetPaused(paused);
    if (paused)
    {
        emulatorRuntimeNotifyPauseRequested();
    }
    MixerSetFrontendPaused(paused);
    printf("frontend: game pause %s\n", paused ? "on" : "off");
    if (refreshMenu)
    {
        frontendMenuRefresh();
    }
}

static void setMinimizedPauseActive(bool active)
{
    if (g_minimizedPauseActive == active)
    {
        return;
    }
    g_minimizedPauseActive = active;
    applyFrontendPauseState(true);
}

void frontendSetGamePaused(bool paused)
{
    bool changed = g_userPauseRequested != paused;
    if (changed)
    {
        g_userPauseRequested = paused;
    }
    if (!paused)
    {
        g_minimizedPauseActive = false;
    }
    if (!changed && frontendGamePaused() == frontendEffectivePauseRequested())
    {
        return;
    }
    applyFrontendPauseState(true);
}

void frontendToggleGamePaused(void)
{
    frontendSetGamePaused(!frontendUserGamePaused());
}

void frontendBeginModalPause(void)
{
    ++g_modalPauseDepth;
    applyFrontendPauseState(true);
}

void frontendEndModalPause(void)
{
    if (g_modalPauseDepth == 0)
    {
        return;
    }
    bool wasPaused = frontendGamePaused();
    --g_modalPauseDepth;
    applyFrontendPauseState(true);
    if (wasPaused && !frontendGamePaused() && !pauseGateWaitForNoWaiters(2000))
    {
        printf("frontend: modal pause drain timeout waiters=%u\n", pauseGateWaiterCount());
    }
}

bool frontendWaitForRuntimePaused(uint32_t timeoutMs)
{
    return pauseGateWaitForPaused(timeoutMs);
}

bool frontendWaitForRuntimePausedWaiters(uint32_t timeoutMs, uint32_t minimumWaiters)
{
    return pauseGateWaitForPausedWaiters(timeoutMs, minimumWaiters);
}

uint32_t frontendRuntimePausedWaiterCount(void)
{
    return pauseGateWaiterCount();
}

static void resetFrontendPauseRequests(void)
{
    g_userPauseRequested = false;
    g_minimizedPauseActive = false;
    g_modalPauseDepth = 0;
#ifdef _WIN32
    g_menuLoopPauseActive = false;
#endif
}

static void clearFrontendPauseRequests(bool refreshMenu)
{
    resetFrontendPauseRequests();
    applyFrontendPauseState(refreshMenu);
}

void frontendClearPauseRequests(void)
{
    clearFrontendPauseRequests(true);
}

struct ControllerPhysicalSource
{
    bool axis;
    int index;
    int direction;
    const char* name;
};

static const Sint16 kControllerInputThreshold = 16000;

static const ControllerPhysicalSource kControllerMappingSources[] =
{
    { false, SDL_CONTROLLER_BUTTON_A, 0, "A" },
    { false, SDL_CONTROLLER_BUTTON_B, 0, "B" },
    { false, SDL_CONTROLLER_BUTTON_X, 0, "X" },
    { false, SDL_CONTROLLER_BUTTON_Y, 0, "Y" },
    { false, SDL_CONTROLLER_BUTTON_BACK, 0, "Back" },
    { false, SDL_CONTROLLER_BUTTON_GUIDE, 0, "Guide" },
    { false, SDL_CONTROLLER_BUTTON_START, 0, "Start" },
    { false, SDL_CONTROLLER_BUTTON_LEFTSTICK, 0, "LeftStick" },
    { false, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0, "RightStick" },
    { false, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0, "LeftShoulder" },
    { false, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0, "RightShoulder" },
    { false, SDL_CONTROLLER_BUTTON_DPAD_UP, 0, "DPadUp" },
    { false, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0, "DPadDown" },
    { false, SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0, "DPadLeft" },
    { false, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0, "DPadRight" },
    { false, SDL_CONTROLLER_BUTTON_MISC1, 0, "Misc1" },
    { false, SDL_CONTROLLER_BUTTON_PADDLE1, 0, "Paddle1" },
    { false, SDL_CONTROLLER_BUTTON_PADDLE2, 0, "Paddle2" },
    { false, SDL_CONTROLLER_BUTTON_PADDLE3, 0, "Paddle3" },
    { false, SDL_CONTROLLER_BUTTON_PADDLE4, 0, "Paddle4" },
    { false, SDL_CONTROLLER_BUTTON_TOUCHPAD, 0, "Touchpad" },
    { true, SDL_CONTROLLER_AXIS_LEFTX, 0, "LeftX-" },
    { true, SDL_CONTROLLER_AXIS_LEFTX, 1, "LeftX+" },
    { true, SDL_CONTROLLER_AXIS_LEFTY, 0, "LeftY-" },
    { true, SDL_CONTROLLER_AXIS_LEFTY, 1, "LeftY+" },
    { true, SDL_CONTROLLER_AXIS_RIGHTX, 0, "RightX-" },
    { true, SDL_CONTROLLER_AXIS_RIGHTX, 1, "RightX+" },
    { true, SDL_CONTROLLER_AXIS_RIGHTY, 0, "RightY-" },
    { true, SDL_CONTROLLER_AXIS_RIGHTY, 1, "RightY+" },
    { true, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1, "LeftTrigger" },
    { true, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1, "RightTrigger" },
};

static std::string trimString(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
        text[begin] == '\r' || text[begin] == '\n'))
    {
        begin++;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
        text[end - 1] == '\r' || text[end - 1] == '\n'))
    {
        end--;
    }
    return text.substr(begin, end - begin);
}

static std::string normalizeMappingName(const std::string& text, bool keepTrailingAxisSign)
{
    std::string out;
    std::string trimmed = trimString(text);
    for (size_t i = 0; i < trimmed.size(); ++i)
    {
        unsigned char ch = (unsigned char)trimmed[i];
        if ((ch == '+' || ch == '-') && keepTrailingAxisSign && i == trimmed.size() - 1)
        {
            out.push_back((char)ch);
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '_' || ch == '-' || ch == '+')
        {
            continue;
        }
        out.push_back((char)tolower(ch));
    }
    return out;
}

static bool controllerSourceMatches(const ControllerPhysicalSource& source, bool axis, int index, int direction)
{
    return source.axis == axis && source.index == index && source.direction == direction;
}

static const ControllerPhysicalSource* findControllerSource(bool axis, int index, int direction)
{
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        if (controllerSourceMatches(kControllerMappingSources[i], axis, index, direction))
        {
            return &kControllerMappingSources[i];
        }
    }
    return NULL;
}

static const ControllerPhysicalSource* findControllerSourceByName(const std::string& name)
{
    std::string normalized = normalizeMappingName(name, true);
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        if (normalized == normalizeMappingName(kControllerMappingSources[i].name, true))
        {
            return &kControllerMappingSources[i];
        }
    }
    if (normalized == "triggerleft")
    {
        return findControllerSource(true, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
    }
    if (normalized == "triggerright")
    {
        return findControllerSource(true, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
    }
    return NULL;
}

static const char* controllerTargetName(uint32_t mask)
{
    if (!mask) return "None";
    if (mask == controlMask(CONTROL_BUTTON_A)) return "A";
    if (mask == controlMask(CONTROL_BUTTON_B)) return "B";
    if (mask == controlMask(CONTROL_BUTTON_X)) return "X";
    if (mask == controlMask(CONTROL_BUTTON_Y)) return "Y";
    if (mask == controlMask(CONTROL_BUTTON_START)) return "Start";
    if (mask == controlMask(CONTROL_BUTTON_SELECT)) return "Select";
    if (mask == controlMask(CONTROL_TRIGGER_LEFT)) return "L";
    if (mask == controlMask(CONTROL_TRIGGER_RIGHT)) return "R";
    if (mask == controlMask(CONTROL_DPAD_UP)) return "Up";
    if (mask == controlMask(CONTROL_DPAD_DOWN)) return "Down";
    if (mask == controlMask(CONTROL_DPAD_LEFT)) return "Left";
    if (mask == controlMask(CONTROL_DPAD_RIGHT)) return "Right";
    if (mask == controlMask(CONTROL_POWER)) return "Power";
    return "None";
}

static bool parseControllerTargetName(const std::string& name, uint32_t* outMask)
{
    if (!outMask)
    {
        return false;
    }
    std::string normalized = normalizeMappingName(name, false);
    if (normalized.empty() || normalized == "none" || normalized == "off" ||
        normalized == "unmapped" || normalized == "disabled" || normalized == "0")
    {
        *outMask = 0;
        return true;
    }
    if (normalized == "a" || normalized == "buttona") { *outMask = controlMask(CONTROL_BUTTON_A); return true; }
    if (normalized == "b" || normalized == "buttonb") { *outMask = controlMask(CONTROL_BUTTON_B); return true; }
    if (normalized == "x" || normalized == "buttonx") { *outMask = controlMask(CONTROL_BUTTON_X); return true; }
    if (normalized == "y" || normalized == "buttony") { *outMask = controlMask(CONTROL_BUTTON_Y); return true; }
    if (normalized == "start") { *outMask = controlMask(CONTROL_BUTTON_START); return true; }
    if (normalized == "select" || normalized == "back") { *outMask = controlMask(CONTROL_BUTTON_SELECT); return true; }
    if (normalized == "l" || normalized == "leftshoulder" || normalized == "triggerleft" || normalized == "lefttrigger")
    {
        *outMask = controlMask(CONTROL_TRIGGER_LEFT);
        return true;
    }
    if (normalized == "r" || normalized == "rightshoulder" || normalized == "triggerright" || normalized == "righttrigger")
    {
        *outMask = controlMask(CONTROL_TRIGGER_RIGHT);
        return true;
    }
    if (normalized == "up" || normalized == "dpadup") { *outMask = controlMask(CONTROL_DPAD_UP); return true; }
    if (normalized == "down" || normalized == "dpaddown") { *outMask = controlMask(CONTROL_DPAD_DOWN); return true; }
    if (normalized == "left" || normalized == "dpadleft") { *outMask = controlMask(CONTROL_DPAD_LEFT); return true; }
    if (normalized == "right" || normalized == "dpadright") { *outMask = controlMask(CONTROL_DPAD_RIGHT); return true; }
    if (normalized == "power") { *outMask = controlMask(CONTROL_POWER); return true; }
    return false;
}

static void setDefaultGameControllerMapping(uint32_t buttonMap[SDL_CONTROLLER_BUTTON_MAX],
    uint32_t axisMap[SDL_CONTROLLER_AXIS_MAX][2])
{
    memset(buttonMap, 0, sizeof(uint32_t) * SDL_CONTROLLER_BUTTON_MAX);
    memset(axisMap, 0, sizeof(uint32_t) * SDL_CONTROLLER_AXIS_MAX * 2);

    buttonMap[SDL_CONTROLLER_BUTTON_A] = controlMask(CONTROL_BUTTON_A);
    buttonMap[SDL_CONTROLLER_BUTTON_B] = controlMask(CONTROL_BUTTON_B);
    buttonMap[SDL_CONTROLLER_BUTTON_X] = controlMask(CONTROL_BUTTON_X);
    buttonMap[SDL_CONTROLLER_BUTTON_Y] = controlMask(CONTROL_BUTTON_Y);
    buttonMap[SDL_CONTROLLER_BUTTON_BACK] = controlMask(CONTROL_BUTTON_SELECT);
    buttonMap[SDL_CONTROLLER_BUTTON_START] = controlMask(CONTROL_BUTTON_START);
    buttonMap[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = controlMask(CONTROL_TRIGGER_LEFT);
    buttonMap[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = controlMask(CONTROL_TRIGGER_RIGHT);
    buttonMap[SDL_CONTROLLER_BUTTON_DPAD_UP] = controlMask(CONTROL_DPAD_UP);
    buttonMap[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = controlMask(CONTROL_DPAD_DOWN);
    buttonMap[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = controlMask(CONTROL_DPAD_LEFT);
    buttonMap[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = controlMask(CONTROL_DPAD_RIGHT);

    axisMap[SDL_CONTROLLER_AXIS_LEFTX][0] = controlMask(CONTROL_DPAD_LEFT);
    axisMap[SDL_CONTROLLER_AXIS_LEFTX][1] = controlMask(CONTROL_DPAD_RIGHT);
    axisMap[SDL_CONTROLLER_AXIS_LEFTY][0] = controlMask(CONTROL_DPAD_UP);
    axisMap[SDL_CONTROLLER_AXIS_LEFTY][1] = controlMask(CONTROL_DPAD_DOWN);
    axisMap[SDL_CONTROLLER_AXIS_TRIGGERLEFT][1] = controlMask(CONTROL_TRIGGER_LEFT);
    axisMap[SDL_CONTROLLER_AXIS_TRIGGERRIGHT][1] = controlMask(CONTROL_TRIGGER_RIGHT);
}

static uint32_t controllerSourceMaskFromMaps(const ControllerPhysicalSource& source,
    const uint32_t buttonMap[SDL_CONTROLLER_BUTTON_MAX],
    const uint32_t axisMap[SDL_CONTROLLER_AXIS_MAX][2])
{
    if (source.axis)
    {
        return axisMap[source.index][source.direction];
    }
    return buttonMap[source.index];
}

static uint32_t currentControllerSourceMask(const ControllerPhysicalSource& source)
{
    return controllerSourceMaskFromMaps(source, g_gameControllerButtonMap, g_gameControllerAxisMap);
}

static void setCurrentControllerSourceMask(const ControllerPhysicalSource& source, uint32_t mask)
{
    if (source.axis)
    {
        g_gameControllerAxisMap[source.index][source.direction] = mask;
    }
    else
    {
        g_gameControllerButtonMap[source.index] = mask;
    }
}

static void applyControllerMappingToken(const std::string& token)
{
    std::string trimmed = trimString(token);
    if (trimmed.empty())
    {
        return;
    }

    size_t separator = trimmed.find('=');
    if (separator == std::string::npos)
    {
        separator = trimmed.find(':');
    }
    if (separator == std::string::npos)
    {
        printf("frontend: invalid controller mapping token='%s'\n", trimmed.c_str());
        return;
    }

    std::string sourceName = trimString(trimmed.substr(0, separator));
    std::string targetName = trimString(trimmed.substr(separator + 1));
    const ControllerPhysicalSource* source = findControllerSourceByName(sourceName);
    if (!source)
    {
        printf("frontend: unknown controller mapping source='%s'\n", sourceName.c_str());
        return;
    }

    uint32_t targetMask = 0;
    if (!parseControllerTargetName(targetName, &targetMask))
    {
        printf("frontend: unknown controller mapping target='%s'\n", targetName.c_str());
        return;
    }
    setCurrentControllerSourceMask(*source, targetMask);
}

static void applyGameControllerMappingSettings(const std::string& mapping)
{
    if (g_controllerMappingInitialized && mapping == g_appliedControllerMapping)
    {
        return;
    }

    releaseGameControllerControls();
    setDefaultGameControllerMapping(g_gameControllerButtonMap, g_gameControllerAxisMap);

    size_t begin = 0;
    while (begin <= mapping.size())
    {
        size_t comma = mapping.find_first_of(",;\n", begin);
        std::string token = comma == std::string::npos ?
            mapping.substr(begin) : mapping.substr(begin, comma - begin);
        applyControllerMappingToken(token);
        if (comma == std::string::npos)
        {
            break;
        }
        begin = comma + 1;
    }

    g_appliedControllerMapping = mapping;
    g_controllerMappingInitialized = true;
    printf("frontend: controller mapping applied spec='%s'\n",
        mapping.empty() ? "(default)" : mapping.c_str());
}

static std::string buildCurrentControllerMappingSpec(void)
{
    uint32_t defaultButtonMap[SDL_CONTROLLER_BUTTON_MAX];
    uint32_t defaultAxisMap[SDL_CONTROLLER_AXIS_MAX][2];
    setDefaultGameControllerMapping(defaultButtonMap, defaultAxisMap);

    std::string spec;
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        const ControllerPhysicalSource& source = kControllerMappingSources[i];
        uint32_t current = currentControllerSourceMask(source);
        uint32_t fallback = controllerSourceMaskFromMaps(source, defaultButtonMap, defaultAxisMap);
        if (current == fallback)
        {
            continue;
        }
        if (!spec.empty())
        {
            spec += ",";
        }
        spec += source.name;
        spec += "=";
        spec += controllerTargetName(current);
    }
    return spec;
}

std::string frontendControllerSourceForControl(uint32_t controlBit)
{
    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }
    uint32_t targetMask = controlMask(controlBit);
    std::string out;
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        if (currentControllerSourceMask(kControllerMappingSources[i]) != targetMask)
        {
            continue;
        }
        if (!out.empty())
        {
            out += " / ";
        }
        out += kControllerMappingSources[i].name;
    }
    return out.empty() ? "None" : out;
}

static void removeControllerTargetFromCurrentMapping(uint32_t targetMask)
{
    if (!targetMask)
    {
        return;
    }
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        if (currentControllerSourceMask(kControllerMappingSources[i]) == targetMask)
        {
            setCurrentControllerSourceMask(kControllerMappingSources[i], 0);
        }
    }
}

#ifdef _WIN32
static UiLanguage frontendUiLanguage(void)
{
    return g_frontendSettings ? g_frontendSettings->uiLanguage : UI_LANGUAGE_ENGLISH;
}

static std::wstring asciiToWide(const char* text)
{
    std::wstring wide;
    if (!text)
    {
        return wide;
    }
    while (*text)
    {
        wide.push_back((wchar_t)(unsigned char)*text);
        text++;
    }
    return wide;
}

static std::wstring controllerTargetDisplayName(uint32_t targetMask)
{
    bool zh = frontendUiLanguage() == UI_LANGUAGE_CHINESE;
    if (targetMask == controlMask(CONTROL_TRIGGER_LEFT))
    {
        return zh ? L"\u5de6\u80a9\u952e" : L"Left shoulder";
    }
    if (targetMask == controlMask(CONTROL_TRIGGER_RIGHT))
    {
        return zh ? L"\u53f3\u80a9\u952e" : L"Right shoulder";
    }
    if (targetMask == controlMask(CONTROL_DPAD_UP))
    {
        return zh ? L"\u65b9\u5411\u952e\u4e0a" : L"D-pad up";
    }
    if (targetMask == controlMask(CONTROL_DPAD_DOWN))
    {
        return zh ? L"\u65b9\u5411\u952e\u4e0b" : L"D-pad down";
    }
    if (targetMask == controlMask(CONTROL_DPAD_LEFT))
    {
        return zh ? L"\u65b9\u5411\u952e\u5de6" : L"D-pad left";
    }
    if (targetMask == controlMask(CONTROL_DPAD_RIGHT))
    {
        return zh ? L"\u65b9\u5411\u952e\u53f3" : L"D-pad right";
    }
    return asciiToWide(controllerTargetName(targetMask));
}

static const wchar_t* controllerMappingTitle(void)
{
    return frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
        L"\u6309\u952e\u6620\u5c04" : L"Input Mapping";
}

static void showControllerMappingPrompt(uint32_t targetMask)
{
    std::wstring target = controllerTargetDisplayName(targetMask);
    std::wstring body;
    if (frontendUiLanguage() == UI_LANGUAGE_CHINESE)
    {
        body = L"\u70b9\u51fb\u786e\u5b9a\u540e\uff0c\u6309\u4e0b\u8981\u6620\u5c04\u4e3a\u300c";
        body += target;
        body += L"\u300d\u7684\u624b\u67c4\u6309\u952e\u3001\u6447\u6746\u65b9\u5411\u6216\u6273\u673a\u3002\n\u6309 Esc \u53ef\u53d6\u6d88\u7b49\u5f85\u3002";
    }
    else
    {
        body = L"Click OK, then press the controller button, stick direction, or trigger to map to ";
        body += target;
        body += L".\nPress Esc to cancel waiting.";
    }
    MessageBoxW(g_nativeWindow, body.c_str(), controllerMappingTitle(), MB_OK | MB_ICONINFORMATION);
}

static void showControllerMappingSaved(uint32_t targetMask, const char* sourceName)
{
    std::wstring target = controllerTargetDisplayName(targetMask);
    std::wstring source = asciiToWide(sourceName);
    std::wstring body;
    if (frontendUiLanguage() == UI_LANGUAGE_CHINESE)
    {
        body = L"\u5df2\u5c06\u300c";
        body += target;
        body += L"\u300d\u6620\u5c04\u5230\u624b\u67c4\u8f93\u5165\u300c";
        body += source;
        body += L"\u300d\u3002\n\u8bbe\u7f6e\u5df2\u4fdd\u5b58\u3002";
    }
    else
    {
        body = L"Mapped ";
        body += target;
        body += L" to controller input ";
        body += source;
        body += L".\nSettings saved.";
    }
    MessageBoxW(g_nativeWindow, body.c_str(), controllerMappingTitle(), MB_OK | MB_ICONINFORMATION);
}

static void showControllerMappingUnavailable(void)
{
    const wchar_t* body = frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
        L"\u672a\u68c0\u6d4b\u5230 SDL GameController \u517c\u5bb9\u624b\u67c4\u3002\n\u8bf7\u8fde\u63a5\u624b\u67c4\u540e\u91cd\u8bd5\u3002" :
        L"No SDL GameController-compatible pad was detected.\nConnect a controller and try again.";
    MessageBoxW(g_nativeWindow, body, controllerMappingTitle(), MB_OK | MB_ICONINFORMATION);
}

void frontendOpenInputMappingWindow(void)
{
    InputMappingUiCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.controllerSourceForControl = frontendControllerSourceForControl;
    callbacks.beginControllerMapping = frontendBeginControllerMapping;
    callbacks.resetControllerMapping = frontendResetControllerMapping;
    callbacks.settingsChanged = frontendMenuRefresh;
    inputMappingUiOpenWindow(g_nativeWindow, frontendUiLanguage(), g_frontendSettings, callbacks);
}

void frontendOpenResourceMonitorWindow(void)
{
#ifdef _WIN32
    if (!frontendMenuGameRunning())
    {
        return;
    }
    resourceMonitorOpenWindow(g_nativeWindow, frontendUiLanguage());
#endif
}

void frontendOpenMemorySearcherWindow(void)
{
#ifdef _WIN32
    if (!frontendMenuGameRunning())
    {
        return;
    }
    memorySearcherOpenWindow(g_nativeWindow, frontendUiLanguage());
#endif
}

void frontendOpenDebuggerWindow(void)
{
#ifdef _WIN32
    if (!frontendMenuGameRunning())
    {
        return;
    }
    debuggerUiOpenWindow(g_nativeWindow, frontendUiLanguage());
#endif
}

static void initCommonControlsForNativeWindows(void)
{
    INITCOMMONCONTROLSEX controls;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);
}
#endif

static bool finishControllerMapping(const ControllerPhysicalSource& source)
{
    if (!g_controllerMappingPending)
    {
        return false;
    }

    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }

    uint32_t targetMask = g_controllerMappingTarget;
    releaseGameControllerControls();
    removeControllerTargetFromCurrentMapping(targetMask);
    setCurrentControllerSourceMask(source, targetMask);

    std::string nextMapping = buildCurrentControllerMappingSpec();
    g_appliedControllerMapping = nextMapping;
    g_controllerMappingInitialized = true;
    g_controllerMappingPending = false;
    g_controllerMappingTarget = 0;

    if (g_frontendSettings)
    {
        g_frontendSettings->controllerMapping = nextMapping;
        emulatorSaveSettings(*g_frontendSettings);
    }
    frontendMenuRefresh();

    printf("frontend: controller mapping saved target=%s source=%s spec='%s'\n",
        controllerTargetName(targetMask),
        source.name,
        nextMapping.empty() ? "(default)" : nextMapping.c_str());
#ifdef _WIN32
    showControllerMappingSaved(targetMask, source.name);
    inputMappingUiRefresh();
#endif
    return true;
}

static void cancelControllerMapping(void)
{
    if (!g_controllerMappingPending)
    {
        return;
    }
    printf("frontend: controller mapping cancelled target=%s\n",
        controllerTargetName(g_controllerMappingTarget));
    g_controllerMappingPending = false;
    g_controllerMappingTarget = 0;
}

void frontendBeginControllerMapping(uint32_t controlBit)
{
    uint32_t targetMask = controlMask(controlBit);
    if (!parseControllerTargetName(controllerTargetName(targetMask), &targetMask) || !targetMask)
    {
        printf("frontend: rejected controller mapping target bit=%u\n", (unsigned int)controlBit);
        return;
    }

    openFirstGameController();
    if (!g_gameController)
    {
        printf("frontend: controller mapping unavailable because no SDL GameController is connected\n");
#ifdef _WIN32
        inputMappingUiSetStatus(frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
            L"\u672a\u68c0\u6d4b\u5230 SDL GameController \u517c\u5bb9\u624b\u67c4\u3002" :
            L"No SDL GameController-compatible pad detected.");
        showControllerMappingUnavailable();
#endif
        return;
    }

    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }

    releaseGameControllerControls();
    g_controllerMappingPending = true;
    g_controllerMappingTarget = targetMask;
    printf("frontend: waiting for controller mapping target=%s\n", controllerTargetName(targetMask));
#ifdef _WIN32
    showControllerMappingPrompt(targetMask);
#endif
}

void frontendResetControllerMapping(void)
{
    releaseGameControllerControls();
    setDefaultGameControllerMapping(g_gameControllerButtonMap, g_gameControllerAxisMap);
    g_appliedControllerMapping.clear();
    g_controllerMappingInitialized = true;
    g_controllerMappingPending = false;
    g_controllerMappingTarget = 0;
    if (g_frontendSettings)
    {
        g_frontendSettings->controllerMapping.clear();
        emulatorSaveSettings(*g_frontendSettings);
    }
    frontendMenuRefresh();
    printf("frontend: controller mapping reset to defaults\n");
#ifdef _WIN32
    inputMappingUiRefresh();
#endif
}

static uint32_t gameControllerButtonControlMask(SDL_GameControllerButton button)
{
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX)
    {
        return 0;
    }
    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }
    return g_gameControllerButtonMap[button];
}

static uint32_t gameControllerAxisControlMask(void)
{
    uint32_t mask = 0;

    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }

    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis)
    {
        if (g_gameControllerAxes[axis] <= -kControllerInputThreshold)
        {
            mask |= g_gameControllerAxisMap[axis][0];
        }
        else if (g_gameControllerAxes[axis] >= kControllerInputThreshold)
        {
            mask |= g_gameControllerAxisMap[axis][1];
        }
    }

    return mask;
}

static void openFirstGameController(void)
{
    if (g_gameController)
    {
        return;
    }

    int joystickCount = SDL_NumJoysticks();
    for (int i = 0; i < joystickCount; ++i)
    {
        if (!SDL_IsGameController(i))
        {
            continue;
        }

        g_gameController = SDL_GameControllerOpen(i);
        if (g_gameController)
        {
            memset(g_gameControllerAxes, 0, sizeof(g_gameControllerAxes));
            const char* name = SDL_GameControllerName(g_gameController);
            printf("frontend: game controller opened index=%d name=%s\n",
                i, name ? name : "(unknown)");
            return;
        }
        printf("frontend: SDL_GameControllerOpen failed index=%d error=%s\n", i, SDL_GetError());
    }
}

static void closeGameController(void)
{
    releaseGameControllerControls();
    if (g_gameController)
    {
        const char* name = SDL_GameControllerName(g_gameController);
        printf("frontend: game controller closed name=%s\n",
            name ? name : "(unknown)");
        SDL_GameControllerClose(g_gameController);
        g_gameController = NULL;
    }
}

static void handleGameControllerDeviceAdded(int deviceIndex)
{
    (void)deviceIndex;
    openFirstGameController();
}

static void handleGameControllerDeviceRemoved(SDL_JoystickID instanceId)
{
    if (activeGameControllerInstanceId() != instanceId)
    {
        return;
    }
    if (g_controllerMappingPending)
    {
        cancelControllerMapping();
#ifdef _WIN32
        inputMappingUiSetStatus(frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
            L"\u624b\u67c4\u5df2\u65ad\u5f00\uff0c\u6620\u5c04\u7b49\u5f85\u5df2\u53d6\u6d88\u3002" :
            L"Controller disconnected; mapping wait cancelled.");
#endif
    }
    closeGameController();
    openFirstGameController();
}

static void handleGameControllerButtonEvent(const SDL_ControllerButtonEvent& button)
{
    if (button.which != activeGameControllerInstanceId())
    {
        return;
    }

    if (g_controllerMappingPending)
    {
        if (button.state == SDL_PRESSED)
        {
            const ControllerPhysicalSource* source = findControllerSource(false, button.button, 0);
            if (source)
            {
                finishControllerMapping(*source);
            }
        }
        return;
    }

    uint32_t mask = gameControllerButtonControlMask((SDL_GameControllerButton)button.button);
    if (!mask)
    {
        return;
    }

    uint32_t buttonMask = g_gameControllerButtonControls;
    if (button.state == SDL_PRESSED)
    {
        buttonMask |= mask;
    }
    else
    {
        buttonMask &= ~mask;
    }
    applyGameControllerControlMasks(buttonMask, g_gameControllerAxisControls);

    if (inputTraceEnabled())
    {
        printf("frontend: controller button=%u state=%u mask=0x%08X\n",
            (unsigned int)button.button,
            (unsigned int)button.state,
            (unsigned int)(g_gameControllerButtonControls | g_gameControllerAxisControls));
    }
}

static void handleGameControllerAxisEvent(const SDL_ControllerAxisEvent& axis)
{
    if (axis.which != activeGameControllerInstanceId() || axis.axis >= SDL_CONTROLLER_AXIS_MAX)
    {
        return;
    }

    if (g_controllerMappingPending)
    {
        if (axis.value <= -kControllerInputThreshold || axis.value >= kControllerInputThreshold)
        {
            int direction = axis.value < 0 ? 0 : 1;
            const ControllerPhysicalSource* source = findControllerSource(true, axis.axis, direction);
            if (source)
            {
                finishControllerMapping(*source);
            }
        }
        return;
    }

    g_gameControllerAxes[axis.axis] = axis.value;
    applyGameControllerControlMasks(g_gameControllerButtonControls, gameControllerAxisControlMask());

    if (inputTraceEnabled())
    {
        printf("frontend: controller axis=%u value=%d mask=0x%08X\n",
            (unsigned int)axis.axis,
            (int)axis.value,
            (unsigned int)(g_gameControllerButtonControls | g_gameControllerAxisControls));
    }
}

static void resetFpsOverlayTexture(void)
{
    if (g_fpsOverlayTexture)
    {
        SDL_DestroyTexture(g_fpsOverlayTexture);
        g_fpsOverlayTexture = NULL;
    }
    g_fpsOverlayValue = -1;
    g_fpsOverlayWidth = 0;
    g_fpsOverlayHeight = 0;
}

static void putPixelRgba(uint32_t* pixels, int pitchPixels, int width, int height,
    int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < width && y < height)
    {
        pixels[y * pitchPixels + x] = color;
    }
}

static void fillRectRgba(uint32_t* pixels, int pitchPixels, int width, int height,
    int x, int y, int w, int h, uint32_t color)
{
    for (int row = 0; row < h; ++row)
    {
        for (int col = 0; col < w; ++col)
        {
            putPixelRgba(pixels, pitchPixels, width, height, x + col, y + row, color);
        }
    }
}

static void drawTextToPixels(uint32_t* pixels, int pitchPixels, int width, int height,
    const char* text, int x, int y, int scale, uint32_t color)
{
    int cursor = x;
    for (const char* p = text; *p; ++p)
    {
        const uint8_t* glyph = glyphForChar(*p);
        if (glyph)
        {
            for (int row = 0; row < 7; ++row)
            {
                for (int col = 0; col < 5; ++col)
                {
                    if (glyph[row] & (1 << (4 - col)))
                    {
                        fillRectRgba(pixels, pitchPixels, width, height,
                            cursor + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
        }
        cursor += 6 * scale;
    }
}

static bool rebuildFpsOverlayTexture(int displayedFps)
{
    if (!g_renderer)
    {
        return false;
    }

    resetFpsOverlayTexture();

    char text[16];
    snprintf(text, sizeof(text), "FPS:%d", displayedFps);
    int scale = 2;
    g_fpsOverlayWidth = (int)strlen(text) * 6 * scale + 4;
    g_fpsOverlayHeight = 7 * scale + 4;

    g_fpsOverlayTexture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, g_fpsOverlayWidth, g_fpsOverlayHeight);
    if (!g_fpsOverlayTexture)
    {
        printf("frontend: FPS overlay texture creation failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(g_fpsOverlayTexture, SDL_BLENDMODE_BLEND);

    void* lockedPixels = NULL;
    int pitchBytes = 0;
    if (SDL_LockTexture(g_fpsOverlayTexture, NULL, &lockedPixels, &pitchBytes) != 0)
    {
        printf("frontend: FPS overlay texture lock failed: %s\n", SDL_GetError());
        resetFpsOverlayTexture();
        return false;
    }

    memset(lockedPixels, 0, (size_t)pitchBytes * (size_t)g_fpsOverlayHeight);
    uint32_t* pixels = (uint32_t*)lockedPixels;
    int pitchPixels = pitchBytes / (int)sizeof(uint32_t);
    fillRectRgba(pixels, pitchPixels, g_fpsOverlayWidth, g_fpsOverlayHeight,
        0, 0, g_fpsOverlayWidth, g_fpsOverlayHeight, 0x000000a0u);
    drawTextToPixels(pixels, pitchPixels, g_fpsOverlayWidth, g_fpsOverlayHeight,
        text, 2, 2, scale, 0xffffffffu);
    SDL_UnlockTexture(g_fpsOverlayTexture);
    g_fpsOverlayValue = displayedFps;
    return true;
}

static void drawFpsOverlay(int displayedFps)
{
    if (g_frontendSettings && !g_frontendSettings->showFps)
    {
        return;
    }
    if (displayedFps < 0)
    {
        displayedFps = 0;
    }
    if (!g_fpsOverlayTexture || g_fpsOverlayValue != displayedFps)
    {
        if (!rebuildFpsOverlayTexture(displayedFps))
        {
            return;
        }
    }

    SDL_Rect dst = { 2, 2, g_fpsOverlayWidth, g_fpsOverlayHeight };
    if (SDL_RenderCopy(g_renderer, g_fpsOverlayTexture, NULL, &dst) != 0)
    {
        printf("frontend: FPS overlay render failed: %s\n", SDL_GetError());
    }
}

static int clampedWindowScale(const EmulatorSettings* settings)
{
    int scale = settings ? settings->windowScale : 2;
    if (scale < 1)
    {
        scale = 1;
    }
    if (scale > 3)
    {
        scale = 3;
    }
    return scale;
}

static void applyMaximizedWindow(bool maximized)
{
    if (!g_window)
    {
        return;
    }

    if (maximized)
    {
        SDL_MaximizeWindow(g_window);
    }
    else
    {
        SDL_RestoreWindow(g_window);
    }
}

static bool textureLinearSamplingEnabled(const EmulatorSettings& settings)
{
    return settings.antiAliasing != ANTI_ALIASING_OFF;
}

void frontendApplyVideoSettings(const EmulatorSettings& settings)
{
    int displayWidth = displayWidthForSettings(&settings);
    int displayHeight = displayHeightForSettings(&settings);

    if (g_lastDisplayFrameValid &&
        (g_lastDisplayFrameWidth != displayWidth || g_lastDisplayFrameHeight != displayHeight))
    {
        g_lastDisplayFrameValid = false;
    }
    if (!g_lastDisplayFrameValid)
    {
        g_lastDisplayFrameWidth = displayWidth;
        g_lastDisplayFrameHeight = displayHeight;
    }
    resetIdleTextures();

    if (g_window)
    {
        int scale = clampedWindowScale(&settings);
        applyMaximizedWindow(settings.fullscreen);
        if (!settings.fullscreen)
        {
            SDL_SetWindowSize(g_window, displayWidth * scale, displayHeight * scale);
            SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }

    if (g_frameTexture)
    {
        SDL_SetTextureScaleMode(g_frameTexture,
            textureLinearSamplingEnabled(settings) ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }

    printf("frontend: video settings scale=%d fullscreen=%u anti_aliasing=%s effect=%s brightness=%d contrast=%d gamma=%d saturation=%d minimized_behavior=%s portrait=%u show_fps=%u virtual_controls=%u\n",
        clampedWindowScale(&settings),
        settings.fullscreen ? 1u : 0u,
        emulatorAntiAliasingName(settings.antiAliasing),
        emulatorColorEffectName(settings.colorEffect),
        settings.brightnessPercent,
        settings.contrastPercent,
        settings.gammaPercent,
        settings.saturationPercent,
        emulatorMinimizedBehaviorName(settings.minimizedBehavior),
        settings.portraitMode ? 1u : 0u,
        settings.showFps ? 1u : 0u,
        settings.showVirtualControls ? 1u : 0u);
}

void frontendApplyAudioSettings(const EmulatorSettings& settings)
{
    MixerSetMasterVolumePercent(settings.audioVolumePercent);
    MixerSetBufferSamples(settings.audioBufferSamples);
    MixerSetAudioEffect(settings.audioEffect);
    printf("frontend: audio settings volume=%d buffer_samples=%d effect=%s audio_disabled=%u\n",
        settings.audioVolumePercent,
        settings.audioBufferSamples,
        emulatorAudioEffectName(settings.audioEffect),
        settings.audioDisabled ? 1u : 0u);
}

void frontendApplyInputSettings(const EmulatorSettings& settings)
{
    inputApplyKeyboardMapping(settings.keyboardMapping);
    applyGameControllerMappingSettings(settings.controllerMapping);
    if (settings.disableIme)
    {
        disableWindowIme();
    }
    else
    {
        enableWindowIme();
    }
    printf("frontend: input settings disable_ime=%u virtual_controls=%u keyboard_mapping=%s controller_mapping=%s\n",
        settings.disableIme ? 1u : 0u,
        settings.showVirtualControls ? 1u : 0u,
        settings.keyboardMapping.empty() ? "(default)" : settings.keyboardMapping.c_str(),
        settings.controllerMapping.empty() ? "(default)" : settings.controllerMapping.c_str());
}

static uint32_t hashFramePixels(const uint16_t* pixels)
{
    const uint32_t* words = (const uint32_t*)pixels;
    const size_t wordCount = (SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t)) / sizeof(uint32_t);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < wordCount; i += 16)
    {
        hash ^= words[i];
        hash *= 16777619u;
    }
    hash ^= words[wordCount - 1];
    hash *= 16777619u;
    return hash;
}

struct AutoPressPlan
{
    bool enabled;
    uint32_t controlBit;
    uint64_t startDelayMs;
    int count;
    uint64_t periodMs;
    uint64_t holdMs;
};

struct AutoControlSequenceEvent
{
    uint32_t controlBit;
    uint64_t startMs;
    uint64_t holdMs;
};

struct AutoVirtualClickEvent
{
    AutoControlSequenceEvent timing;
    uint64_t downElapsedMs;
    bool downSent;
    bool upSent;
};

struct AutoStateAction
{
    int slot;
    uint64_t startMs;
    bool done;
};

static const int kMaxAutoPressSequenceEvents = 64;
static const int kMaxAutoVirtualClickEvents = 64;
static const int kMaxAutoStateActions = 32;

static bool parseControlName(const char* text, size_t length, uint32_t* outControlBit)
{
    if (!text || !outControlBit || length == 0)
    {
        return false;
    }

    if (length == 1)
    {
        switch (text[0])
        {
        case 'A':
        case 'a':
            *outControlBit = CONTROL_BUTTON_A;
            return true;
        case 'B':
        case 'b':
            *outControlBit = CONTROL_BUTTON_B;
            return true;
        case 'X':
        case 'x':
            *outControlBit = CONTROL_BUTTON_X;
            return true;
        case 'Y':
        case 'y':
            *outControlBit = CONTROL_BUTTON_Y;
            return true;
        case 'U':
        case 'u':
            *outControlBit = CONTROL_DPAD_UP;
            return true;
        case 'D':
        case 'd':
            *outControlBit = CONTROL_DPAD_DOWN;
            return true;
        case 'L':
        case 'l':
            *outControlBit = CONTROL_DPAD_LEFT;
            return true;
        case 'R':
        case 'r':
            *outControlBit = CONTROL_DPAD_RIGHT;
            return true;
        default:
            break;
        }
    }

    if (length == 5 && _strnicmp(text, "START", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_START;
        return true;
    }
    if (length == 5 && _strnicmp(text, "ENTER", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_A;
        return true;
    }
    if (length == 4 && _strnicmp(text, "MENU", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_X;
        return true;
    }
    if (length == 2 && _strnicmp(text, "AB", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_B;
        return true;
    }
    if (length == 2 && _strnicmp(text, "EQ", length) == 0)
    {
        *outControlBit = 22;
        return true;
    }
    if (length == 6 && _strnicmp(text, "CAMERA", length) == 0)
    {
        *outControlBit = 30;
        return true;
    }
    if (length == 6 && _strnicmp(text, "SELECT", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_SELECT;
        return true;
    }
    if (length == 2 && _strnicmp(text, "UP", length) == 0)
    {
        *outControlBit = CONTROL_DPAD_UP;
        return true;
    }
    if (length == 4 && _strnicmp(text, "DOWN", length) == 0)
    {
        *outControlBit = CONTROL_DPAD_DOWN;
        return true;
    }
    if (length == 4 && _strnicmp(text, "LEFT", length) == 0)
    {
        *outControlBit = CONTROL_DPAD_LEFT;
        return true;
    }
    if (length == 5 && _strnicmp(text, "RIGHT", length) == 0)
    {
        *outControlBit = CONTROL_DPAD_RIGHT;
        return true;
    }
    return false;
}

static bool parseUnsignedField(const char* text, uint64_t* out)
{
    if (!text || !text[0] || !out)
    {
        return false;
    }

    char* end = NULL;
    uint64_t value = strtoull(text, &end, 10);
    if (!end || *end)
    {
        return false;
    }
    *out = value;
    return true;
}

static int parseAutoStateSequence(const char* envName, AutoStateAction* actions, int capacity)
{
    if (!envName || !actions || capacity <= 0)
    {
        return 0;
    }

    const char* spec = getenv(envName);
    if (!spec || !spec[0])
    {
        return 0;
    }

    char buffer[2048];
    snprintf(buffer, sizeof(buffer), "%s", spec);

    int count = 0;
    char* token = buffer;
    while (token && *token && count < capacity)
    {
        char* next = strchr(token, ',');
        if (next)
        {
            *next = 0;
            next++;
        }

        while (*token == ' ' || *token == '\t')
        {
            token++;
        }
        size_t tokenLength = strlen(token);
        while (tokenLength > 0 && (token[tokenLength - 1] == ' ' || token[tokenLength - 1] == '\t'))
        {
            token[--tokenLength] = 0;
        }

        char* at = strchr(token, '@');
        if (at)
        {
            *at = 0;
            uint64_t slot = 0;
            uint64_t startMs = 0;
            if (parseUnsignedField(token, &slot) &&
                parseUnsignedField(at + 1, &startMs) &&
                slot >= 1 && slot <= kSaveStateSlotCount)
            {
                actions[count].slot = (int)slot;
                actions[count].startMs = startMs;
                actions[count].done = false;
                count++;
            }
            else
            {
                printf("frontend: invalid %s token='%s@%s'\n", envName, token, at + 1);
            }
        }
        else if (tokenLength > 0)
        {
            printf("frontend: invalid %s token='%s'\n", envName, token);
        }

        token = next;
    }

    printf("frontend: %s actions=%d spec='%s'\n", envName, count, spec);
    return count;
}

static void runAutoStateActions(AutoStateAction* actions, int count, uint64_t elapsedMs, bool save)
{
    if (!actions || count <= 0)
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        if (actions[i].done || elapsedMs < actions[i].startMs)
        {
            continue;
        }
        actions[i].done = true;
        if (save)
        {
            frontendMenuSaveStateSlotForAutomation(actions[i].slot);
        }
        else
        {
            frontendMenuLoadStateSlotForAutomation(actions[i].slot);
        }
    }
}

static int parseTimedControlSequence(const char* envName, const char* logName,
    AutoControlSequenceEvent* events, int capacity)
{
    if (!envName || !logName || !events || capacity <= 0)
    {
        return 0;
    }

    const char* spec = getenv(envName);
    if (!spec || !spec[0])
    {
        return 0;
    }

    char buffer[2048];
    snprintf(buffer, sizeof(buffer), "%s", spec);

    int count = 0;
    char* token = buffer;
    while (token && *token && count < capacity)
    {
        char* next = strchr(token, ',');
        if (next)
        {
            *next = 0;
            next++;
        }

        while (*token == ' ' || *token == '\t')
        {
            token++;
        }
        size_t tokenLength = strlen(token);
        while (tokenLength > 0 && (token[tokenLength - 1] == ' ' || token[tokenLength - 1] == '\t'))
        {
            token[--tokenLength] = 0;
        }

        char* at = strchr(token, '@');
        char* colon = at ? strchr(at + 1, ':') : NULL;
        if (at && colon)
        {
            *at = 0;
            *colon = 0;
            uint32_t controlBit = 0;
            uint64_t startMs = 0;
            uint64_t holdMs = 0;
            if (parseControlName(token, strlen(token), &controlBit) &&
                parseUnsignedField(at + 1, &startMs) &&
                parseUnsignedField(colon + 1, &holdMs))
            {
                if (holdMs < 20)
                {
                    holdMs = 20;
                }
                if (holdMs > 5000)
                {
                    holdMs = 5000;
                }
                events[count].controlBit = controlBit;
                events[count].startMs = startMs;
                events[count].holdMs = holdMs;
                count++;
            }
            else
            {
                printf("frontend: invalid %s token='%s@%s:%s'\n",
                    logName, token, at + 1, colon + 1);
            }
        }
        else if (tokenLength > 0)
        {
            printf("frontend: invalid %s token='%s'\n", logName, token);
        }

        token = next;
    }

    printf("frontend: %s events=%d spec='%s'\n", logName, count, spec);
    return count;
}

static int parseAutoPressSequence(AutoControlSequenceEvent* events, int capacity)
{
    static int initialized = 0;
    static AutoControlSequenceEvent parsedEvents[kMaxAutoPressSequenceEvents] = {};
    static int parsedCount = 0;

    if (!initialized)
    {
        parsedCount = parseTimedControlSequence(
            "DINGOO_PIE_AUTOPRESS_SEQUENCE",
            "autopress sequence",
            parsedEvents,
            kMaxAutoPressSequenceEvents);
        initialized = 1;
    }

    if (!events || capacity <= 0)
    {
        return parsedCount;
    }
    int copyCount = parsedCount < capacity ? parsedCount : capacity;
    for (int i = 0; i < copyCount; ++i)
    {
        events[i] = parsedEvents[i];
    }
    return copyCount;
}

static int parseAutoVirtualClickSequence(AutoVirtualClickEvent* events, int capacity)
{
    if (!events || capacity <= 0)
    {
        return 0;
    }

    AutoControlSequenceEvent parsedEvents[kMaxAutoVirtualClickEvents] = {};
    int count = parseTimedControlSequence(
        "DINGOO_PIE_AUTOTEST_VIRTUAL_CLICK_SEQUENCE",
        "virtual click sequence",
        parsedEvents,
        capacity < kMaxAutoVirtualClickEvents ? capacity : kMaxAutoVirtualClickEvents);
    for (int i = 0; i < count; ++i)
    {
        events[i].timing = parsedEvents[i];
        events[i].downElapsedMs = 0;
        events[i].downSent = false;
        events[i].upSent = false;
    }
    return count;
}

static void updateAutoPressSequence(uint64_t now, uint64_t startTicks)
{
    AutoControlSequenceEvent events[kMaxAutoPressSequenceEvents];
    int count = parseAutoPressSequence(events, kMaxAutoPressSequenceEvents);
    if (count <= 0)
    {
        return;
    }

    uint64_t elapsed = now - startTicks;
    uint32_t referencedControls = 0;
    uint32_t activeControls = 0;
    for (int i = 0; i < count; ++i)
    {
        uint64_t begin = events[i].startMs;
        uint64_t end = begin + events[i].holdMs;
        bool down = elapsed >= begin && elapsed < end;
        uint32_t mask = 1u << events[i].controlBit;
        referencedControls |= mask;
        if (down)
        {
            activeControls |= mask;
        }
    }

    // A sequence can press the same control more than once. Apply the combined
    // state after evaluating every event so future repeats do not clear the
    // current press before the guest polls it.
    for (uint32_t controlBit = 0; controlBit < 32; ++controlBit)
    {
        uint32_t mask = 1u << controlBit;
        if (referencedControls & mask)
        {
            inputSetSyntheticControl(controlBit, (activeControls & mask) != 0);
        }
    }
}

static bool findVirtualControlClickPoint(uint32_t controlBit, int* outX, int* outY)
{
    if (!outX || !outY)
    {
        return false;
    }

    uint32_t targetMask = virtualControlMask(controlBit);
    VirtualControlButton buttons[16];
    int count = buildVirtualControls(buttons, 16);
    for (int i = 0; i < count; ++i)
    {
        if (buttons[i].controlMask == targetMask)
        {
            int x = buttons[i].rect.x + buttons[i].rect.w / 2;
            int y = buttons[i].rect.y + buttons[i].rect.h / 2;
            if (portraitModeEnabled() && g_renderer)
            {
                int rendererWidth = 0;
                int rendererHeight = 0;
                SDL_GetRendererOutputSize(g_renderer, &rendererWidth, &rendererHeight);
                if (rendererWidth > 0 && rendererHeight > 0)
                {
                    int rendererX = y;
                    int rendererY = rendererHeight - 1 - x;
                    x = rendererX;
                    y = rendererY;
                }
            }
            *outX = x;
            *outY = y;
            return true;
        }
    }
    return false;
}

static bool dispatchAutoVirtualClick(uint32_t controlBit, bool down)
{
    int x = 0;
    int y = 0;
    if (!findVirtualControlClickPoint(controlBit, &x, &y))
    {
        return false;
    }

    SDL_Event ev = {};
    if (down)
    {
        ev.type = SDL_MOUSEBUTTONDOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.state = SDL_PRESSED;
        ev.button.x = x;
        ev.button.y = y;
    }
    else
    {
        ev.type = SDL_MOUSEBUTTONUP;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.state = SDL_RELEASED;
        ev.button.x = x;
        ev.button.y = y;
    }

    bool handled = handleVirtualControlMouseEvent(ev);
    printf("frontend: autotest virtual click %s control=%u handled=%u x=%d y=%d\n",
        down ? "down" : "up",
        (unsigned int)controlBit,
        handled ? 1u : 0u,
        x,
        y);
    return handled;
}

static void runAutoVirtualClickActions(AutoVirtualClickEvent* events, int count, uint64_t elapsedMs)
{
    if (!events || count <= 0 || frontendGamePaused() || frontendPostRestoreInputBlocked())
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        const AutoControlSequenceEvent& timing = events[i].timing;
        if (!events[i].downSent && elapsedMs >= timing.startMs)
        {
            dispatchAutoVirtualClick(timing.controlBit, true);
            events[i].downElapsedMs = elapsedMs;
            events[i].downSent = true;
        }
        if (events[i].downSent && !events[i].upSent &&
            elapsedMs >= events[i].downElapsedMs + timing.holdMs)
        {
            dispatchAutoVirtualClick(timing.controlBit, false);
            events[i].upSent = true;
        }
    }
}

static bool parseAutoPressPlan(AutoPressPlan* out)
{
    static int initialized = 0;
    static AutoPressPlan plan = {};
    if (!initialized)
    {
        const char* spec = getenv("DINGOO_PIE_AUTOPRESS_KEYS");
        if (spec && spec[0])
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "%s", spec);
            char* fields[5] = {};
            int fieldCount = 0;
            char* cursor = buffer;
            while (fieldCount < 5)
            {
                fields[fieldCount++] = cursor;
                char* sep = strchr(cursor, ':');
                if (!sep)
                {
                    break;
                }
                *sep = 0;
                cursor = sep + 1;
            }

            uint32_t controlBit = 0;
            if (fieldCount == 5 && parseControlName(fields[0], strlen(fields[0]), &controlBit))
            {
                plan.enabled = true;
                plan.controlBit = controlBit;
                plan.startDelayMs = strtoull(fields[1], NULL, 10);
                plan.count = atoi(fields[2]);
                plan.periodMs = strtoull(fields[3], NULL, 10);
                plan.holdMs = strtoull(fields[4], NULL, 10);
                if (plan.count < 0)
                {
                    plan.count = 0;
                }
                if (plan.count > 64)
                {
                    plan.count = 64;
                }
                if (plan.periodMs < 100)
                {
                    plan.periodMs = 100;
                }
                if (plan.holdMs < 20)
                {
                    plan.holdMs = 20;
                }
                if (plan.holdMs > plan.periodMs)
                {
                    plan.holdMs = plan.periodMs;
                }
                printf("frontend: autopress key control=%u delay=%llums count=%d period=%llums hold=%llums\n",
                    (unsigned int)plan.controlBit,
                    (unsigned long long)plan.startDelayMs,
                    plan.count,
                    (unsigned long long)plan.periodMs,
                    (unsigned long long)plan.holdMs);
            }
            else
            {
                printf("frontend: invalid DINGOO_PIE_AUTOPRESS_KEYS='%s'\n", spec);
            }
        }
        initialized = 1;
    }

    if (out)
    {
        *out = plan;
    }
    return plan.enabled;
}

static void updateAutoPressPlan(uint64_t now, uint64_t startTicks)
{
    AutoPressPlan plan;
    if (!parseAutoPressPlan(&plan) || plan.count == 0)
    {
        return;
    }

    uint64_t elapsed = now - startTicks;
    if (elapsed < plan.startDelayMs)
    {
        inputSetSyntheticControl(plan.controlBit, false);
        return;
    }

    uint64_t sequence = elapsed - plan.startDelayMs;
    int pressIndex = (int)(sequence / plan.periodMs);
    uint64_t phase = sequence % plan.periodMs;
    bool down = pressIndex < plan.count && phase < plan.holdMs;
    inputSetSyntheticControl(plan.controlBit, down);
}

static int getAutoPressARequest(void)
{
    static int value = -1;
    if (value < 0)
    {
        const char* text = getenv("DINGOO_PIE_AUTOPRESS_A");
        value = text ? atoi(text) : 0;
        if (value < 0)
        {
            value = 0;
        }
        if (value > 16)
        {
            value = 16;
        }
    }
    return value;
}

static uint64_t getAutoPressAStartDelayMs(void)
{
    static int value = -1;
    if (value < 0)
    {
        const char* text = getenv("DINGOO_PIE_AUTOPRESS_A_DELAY_MS");
        value = text ? atoi(text) : 1500;
        if (value < 0)
        {
            value = 0;
        }
        if (value > 60000)
        {
            value = 60000;
        }
    }
    return (uint64_t)value;
}

static uint64_t getAutoPressAPeriodMs(void)
{
    static int value = -1;
    if (value < 0)
    {
        const char* text = getenv("DINGOO_PIE_AUTOPRESS_A_PERIOD_MS");
        value = text ? atoi(text) : 900;
        if (value < 100)
        {
            value = 100;
        }
        if (value > 10000)
        {
            value = 10000;
        }
    }
    return (uint64_t)value;
}

static uint64_t getAutoPressAHoldMs(void)
{
    static int value = -1;
    if (value < 0)
    {
        const char* text = getenv("DINGOO_PIE_AUTOPRESS_A_HOLD_MS");
        value = text ? atoi(text) : 180;
        if (value < 20)
        {
            value = 20;
        }
        if (value > 5000)
        {
            value = 5000;
        }
    }
    return (uint64_t)value;
}

static void updateAutoPressA(uint64_t now, uint64_t startTicks)
{
    int requested = getAutoPressARequest();
    if (!requested)
    {
        return;
    }

    uint64_t elapsed = now - startTicks;
    const uint64_t initialDelayMs = getAutoPressAStartDelayMs();
    const uint64_t periodMs = getAutoPressAPeriodMs();
    const uint64_t holdMs = getAutoPressAHoldMs();
    if (elapsed < initialDelayMs)
    {
        inputSetSyntheticControl(CONTROL_BUTTON_A, false);
        return;
    }

    uint64_t sequence = elapsed - initialDelayMs;
    int pressIndex = (int)(sequence / periodMs);
    uint64_t phase = sequence % periodMs;
    bool down = pressIndex < requested && phase < holdMs;
    inputSetSyntheticControl(CONTROL_BUTTON_A, down);
}

static uint16_t rgb565ToGrayscale(uint16_t pixel)
{
    uint32_t r5 = (pixel >> 11) & 0x1f;
    uint32_t g6 = (pixel >> 5) & 0x3f;
    uint32_t b5 = pixel & 0x1f;
    uint32_t r8 = (r5 << 3) | (r5 >> 2);
    uint32_t g8 = (g6 << 2) | (g6 >> 4);
    uint32_t b8 = (b5 << 3) | (b5 >> 2);
    uint32_t y8 = (77 * r8 + 150 * g8 + 29 * b8) >> 8;
    uint32_t y5 = y8 >> 3;
    uint32_t y6 = y8 >> 2;
    return (uint16_t)((y5 << 11) | (y6 << 5) | y5);
}

static uint16_t rgb565Invert(uint16_t pixel)
{
    return (uint16_t)((~pixel) & 0xffff);
}

static uint16_t rgb888ToRgb565(uint32_t r8, uint32_t g8, uint32_t b8)
{
    if (r8 > 255)
    {
        r8 = 255;
    }
    if (g8 > 255)
    {
        g8 = 255;
    }
    if (b8 > 255)
    {
        b8 = 255;
    }
    return (uint16_t)(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
}

static uint32_t clampColor8(int value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > 255)
    {
        return 255;
    }
    return (uint32_t)value;
}

static void rgb565ToRgb888(uint16_t pixel, uint32_t* r8, uint32_t* g8, uint32_t* b8)
{
    uint32_t r5 = (pixel >> 11) & 0x1f;
    uint32_t g6 = (pixel >> 5) & 0x3f;
    uint32_t b5 = pixel & 0x1f;
    *r8 = (r5 << 3) | (r5 >> 2);
    *g8 = (g6 << 2) | (g6 >> 4);
    *b8 = (b5 << 3) | (b5 >> 2);
}

static uint16_t blendRgb565WithBlack(uint16_t pixel, uint32_t blackAlpha);

static uint16_t rgb565ToSepia(uint16_t pixel)
{
    uint32_t r8 = 0;
    uint32_t g8 = 0;
    uint32_t b8 = 0;
    rgb565ToRgb888(pixel, &r8, &g8, &b8);

    int r = (101 * (int)r8 + 197 * (int)g8 + 48 * (int)b8) >> 8;
    int g = (89 * (int)r8 + 176 * (int)g8 + 43 * (int)b8) >> 8;
    int b = (69 * (int)r8 + 136 * (int)g8 + 33 * (int)b8) >> 8;
    return rgb888ToRgb565(clampColor8(r), clampColor8(g), clampColor8(b));
}

static void applyLcdScanlineEffect(uint16_t* dst, const uint16_t* src)
{
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        bool darkLine = (y & 1) != 0;
        for (int x = 0; x < SCREEN_WIDTH; ++x)
        {
            uint16_t pixel = src[(size_t)y * SCREEN_WIDTH + (size_t)x];
            if (darkLine)
            {
                pixel = blendRgb565WithBlack(pixel, 48);
            }
            else if ((x & 1) != 0)
            {
                pixel = blendRgb565WithBlack(pixel, 12);
            }
            dst[(size_t)y * SCREEN_WIDTH + (size_t)x] = pixel;
        }
    }
}

static void applyLightCrtEffect(uint16_t* dst, const uint16_t* src)
{
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        int scanline = (y & 1) ? 90 : 100;
        for (int x = 0; x < SCREEN_WIDTH; ++x)
        {
            uint32_t r8 = 0;
            uint32_t g8 = 0;
            uint32_t b8 = 0;
            rgb565ToRgb888(src[(size_t)y * SCREEN_WIDTH + (size_t)x], &r8, &g8, &b8);

            int r = (int)r8;
            int g = (int)g8;
            int b = (int)b8;
            r = ((r - 128) * 106) / 100 + 128;
            g = ((g - 128) * 104) / 100 + 128;
            b = ((b - 128) * 102) / 100 + 128;
            r = (r * 104 * scanline) / 10000;
            g = (g * 100 * scanline) / 10000;
            b = (b * 94 * scanline) / 10000;
            if ((x & 1) != 0)
            {
                r = (r * 97) / 100;
                g = (g * 97) / 100;
                b = (b * 97) / 100;
            }

            dst[(size_t)y * SCREEN_WIDTH + (size_t)x] =
                rgb888ToRgb565(clampColor8(r), clampColor8(g), clampColor8(b));
        }
    }
}

static void applyVividEffect(uint16_t* dst, const uint16_t* src)
{
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        for (int x = 0; x < SCREEN_WIDTH; ++x)
        {
            uint32_t r8 = 0;
            uint32_t g8 = 0;
            uint32_t b8 = 0;
            rgb565ToRgb888(src[(size_t)y * SCREEN_WIDTH + (size_t)x], &r8, &g8, &b8);

            int r = ((int)r8 - 128) * 108 / 100 + 128;
            int g = ((int)g8 - 128) * 108 / 100 + 128;
            int b = ((int)b8 - 128) * 108 / 100 + 128;
            int y8 = (77 * r + 150 * g + 29 * b) >> 8;
            r = y8 + ((r - y8) * 116) / 100;
            g = y8 + ((g - y8) * 116) / 100;
            b = y8 + ((b - y8) * 116) / 100;

            dst[(size_t)y * SCREEN_WIDTH + (size_t)x] =
                rgb888ToRgb565(clampColor8(r), clampColor8(g), clampColor8(b));
        }
    }
}

static void applySoftBlurEffect(uint16_t* dst, const uint16_t* src)
{
    memcpy(dst, src, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    for (int y = 1; y < SCREEN_HEIGHT - 1; ++y)
    {
        for (int x = 1; x < SCREEN_WIDTH - 1; ++x)
        {
            int r = 0;
            int g = 0;
            int b = 0;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    uint32_t r8 = 0;
                    uint32_t g8 = 0;
                    uint32_t b8 = 0;
                    int weight = (dx == 0 && dy == 0) ? 4 : 1;
                    rgb565ToRgb888(src[(size_t)(y + dy) * SCREEN_WIDTH + (size_t)(x + dx)], &r8, &g8, &b8);
                    r += (int)r8 * weight;
                    g += (int)g8 * weight;
                    b += (int)b8 * weight;
                }
            }
            dst[(size_t)y * SCREEN_WIDTH + (size_t)x] =
                rgb888ToRgb565((uint32_t)(r / 12), (uint32_t)(g / 12), (uint32_t)(b / 12));
        }
    }
}

static void applySharpenEffect(uint16_t* dst, const uint16_t* src)
{
    memcpy(dst, src, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    for (int y = 1; y < SCREEN_HEIGHT - 1; ++y)
    {
        for (int x = 1; x < SCREEN_WIDTH - 1; ++x)
        {
            uint32_t cr = 0;
            uint32_t cg = 0;
            uint32_t cb = 0;
            uint32_t lr = 0;
            uint32_t lg = 0;
            uint32_t lb = 0;
            uint32_t rr = 0;
            uint32_t rg = 0;
            uint32_t rb = 0;
            uint32_t ur = 0;
            uint32_t ug = 0;
            uint32_t ub = 0;
            uint32_t dr = 0;
            uint32_t dg = 0;
            uint32_t db = 0;
            size_t index = (size_t)y * SCREEN_WIDTH + (size_t)x;

            rgb565ToRgb888(src[index], &cr, &cg, &cb);
            rgb565ToRgb888(src[index - 1], &lr, &lg, &lb);
            rgb565ToRgb888(src[index + 1], &rr, &rg, &rb);
            rgb565ToRgb888(src[index - SCREEN_WIDTH], &ur, &ug, &ub);
            rgb565ToRgb888(src[index + SCREEN_WIDTH], &dr, &dg, &db);

            int r = ((int)cr * 5) - (int)lr - (int)rr - (int)ur - (int)dr;
            int g = ((int)cg * 5) - (int)lg - (int)rg - (int)ug - (int)dg;
            int b = ((int)cb * 5) - (int)lb - (int)rb - (int)ub - (int)db;
            dst[index] = rgb888ToRgb565(clampColor8(r), clampColor8(g), clampColor8(b));
        }
    }
}

static void applyClearAntiAliasingEffect(uint16_t* dst, const uint16_t* src)
{
    memcpy(dst, src, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    for (int y = 1; y < SCREEN_HEIGHT - 1; ++y)
    {
        for (int x = 1; x < SCREEN_WIDTH - 1; ++x)
        {
            uint32_t cr = 0;
            uint32_t cg = 0;
            uint32_t cb = 0;
            uint32_t lr = 0;
            uint32_t lg = 0;
            uint32_t lb = 0;
            uint32_t rr = 0;
            uint32_t rg = 0;
            uint32_t rb = 0;
            uint32_t ur = 0;
            uint32_t ug = 0;
            uint32_t ub = 0;
            uint32_t dr = 0;
            uint32_t dg = 0;
            uint32_t db = 0;
            size_t index = (size_t)y * SCREEN_WIDTH + (size_t)x;

            rgb565ToRgb888(src[index], &cr, &cg, &cb);
            rgb565ToRgb888(src[index - 1], &lr, &lg, &lb);
            rgb565ToRgb888(src[index + 1], &rr, &rg, &rb);
            rgb565ToRgb888(src[index - SCREEN_WIDTH], &ur, &ug, &ub);
            rgb565ToRgb888(src[index + SCREEN_WIDTH], &dr, &dg, &db);

            int avgR = ((int)lr + (int)rr + (int)ur + (int)dr) / 4;
            int avgG = ((int)lg + (int)rg + (int)ug + (int)dg) / 4;
            int avgB = ((int)lb + (int)rb + (int)ub + (int)db) / 4;
            int r = (int)cr + (((int)cr - avgR) / 3);
            int g = (int)cg + (((int)cg - avgG) / 3);
            int b = (int)cb + (((int)cb - avgB) / 3);

            dst[index] = rgb888ToRgb565(clampColor8(r), clampColor8(g), clampColor8(b));
        }
    }
}

static int clampPercent(int value, int minValue, int maxValue)
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

struct VideoAdjustmentParams
{
    int brightness;
    int contrast;
    int gamma;
    int saturation;
    uint8_t gammaTable[256];
};

static void buildGammaTable(uint8_t* table, int gammaPercent)
{
    if (!table)
    {
        return;
    }

    double gamma = (double)gammaPercent / 100.0;
    for (int i = 0; i < 256; ++i)
    {
        double normalized = (double)i / 255.0;
        int adjusted = (int)(pow(normalized, gamma) * 255.0 + 0.5);
        table[i] = (uint8_t)clampColor8(adjusted);
    }
}

static bool buildVideoAdjustmentParams(VideoAdjustmentParams* params)
{
    if (!params || !g_frontendSettings)
    {
        return false;
    }

    params->brightness = clampPercent(g_frontendSettings->brightnessPercent, 50, 150);
    params->contrast = clampPercent(g_frontendSettings->contrastPercent, 50, 150);
    params->gamma = clampPercent(g_frontendSettings->gammaPercent, 50, 150);
    params->saturation = clampPercent(g_frontendSettings->saturationPercent, 0, 200);
    bool enabled = params->brightness != 100 ||
        params->contrast != 100 ||
        params->gamma != 100 ||
        params->saturation != 100;
    if (params->gamma != 100)
    {
        buildGammaTable(params->gammaTable, params->gamma);
    }
    return enabled;
}

static uint16_t applyVideoAdjustmentsToPixel(
    uint16_t pixel,
    const VideoAdjustmentParams& params)
{
    if (params.brightness == 100 &&
        params.contrast == 100 &&
        params.gamma == 100 &&
        params.saturation == 100)
    {
        return pixel;
    }

    uint32_t r8 = 0;
    uint32_t g8 = 0;
    uint32_t b8 = 0;
    rgb565ToRgb888(pixel, &r8, &g8, &b8);

    int r = (int)r8;
    int g = (int)g8;
    int b = (int)b8;

    r = ((r - 128) * params.contrast) / 100 + 128;
    g = ((g - 128) * params.contrast) / 100 + 128;
    b = ((b - 128) * params.contrast) / 100 + 128;

    r = (r * params.brightness) / 100;
    g = (g * params.brightness) / 100;
    b = (b * params.brightness) / 100;

    int y = (77 * r + 150 * g + 29 * b) >> 8;
    r = y + ((r - y) * params.saturation) / 100;
    g = y + ((g - y) * params.saturation) / 100;
    b = y + ((b - y) * params.saturation) / 100;

    if (params.gamma != 100)
    {
        r = params.gammaTable[clampColor8(r)];
        g = params.gammaTable[clampColor8(g)];
        b = params.gammaTable[clampColor8(b)];
    }

    return rgb888ToRgb565(clampColor8(r), clampColor8(g), clampColor8(b));
}

static void applyVideoAdjustments(
    uint16_t* pixels,
    size_t pixelCount,
    const VideoAdjustmentParams& params)
{
    if (!pixels)
    {
        return;
    }

    if (params.brightness == 100 &&
        params.contrast == 100 &&
        params.gamma == 100 &&
        params.saturation == 100)
    {
        return;
    }

    for (size_t i = 0; i < pixelCount; ++i)
    {
        pixels[i] = applyVideoAdjustmentsToPixel(pixels[i], params);
    }
}

static uint16_t blendRgb565WithBlack(uint16_t pixel, uint32_t blackAlpha)
{
    uint32_t r8 = 0;
    uint32_t g8 = 0;
    uint32_t b8 = 0;
    rgb565ToRgb888(pixel, &r8, &g8, &b8);
    uint32_t keep = 255 - blackAlpha;
    r8 = (r8 * keep) / 255;
    g8 = (g8 * keep) / 255;
    b8 = (b8 * keep) / 255;
    return rgb888ToRgb565(r8, g8, b8);
}

static void applyColorEffect(uint16_t* dst, const uint16_t* src, size_t pixelCount)
{
    if (!dst || !src)
    {
        return;
    }

    ColorEffectMode effect = g_frontendSettings ? g_frontendSettings->colorEffect : COLOR_EFFECT_NORMAL;
    if (effect == COLOR_EFFECT_GRAYSCALE)
    {
        for (size_t i = 0; i < pixelCount; ++i)
        {
            dst[i] = rgb565ToGrayscale(src[i]);
        }
        return;
    }
    if (effect == COLOR_EFFECT_INVERT)
    {
        for (size_t i = 0; i < pixelCount; ++i)
        {
            dst[i] = rgb565Invert(src[i]);
        }
        return;
    }
    if (effect == COLOR_EFFECT_SOFT_BLUR)
    {
        applySoftBlurEffect(dst, src);
        return;
    }
    if (effect == COLOR_EFFECT_SHARPEN)
    {
        applySharpenEffect(dst, src);
        return;
    }
    if (effect == COLOR_EFFECT_VIVID)
    {
        applyVividEffect(dst, src);
        return;
    }
    if (effect == COLOR_EFFECT_SEPIA)
    {
        for (size_t i = 0; i < pixelCount; ++i)
        {
            dst[i] = rgb565ToSepia(src[i]);
        }
        return;
    }
    // Pixel Grid is applied after SDL scaling so the grid follows the output size.
    if (effect == COLOR_EFFECT_LCD_SCANLINE)
    {
        applyLcdScanlineEffect(dst, src);
        return;
    }
    if (effect == COLOR_EFFECT_LIGHT_CRT)
    {
        applyLightCrtEffect(dst, src);
        return;
    }

    if (dst != src)
    {
        memcpy(dst, src, pixelCount * sizeof(uint16_t));
    }
}

static AntiAliasingMode currentAntiAliasingMode(void)
{
    return g_frontendSettings ? g_frontendSettings->antiAliasing : ANTI_ALIASING_OFF;
}

static bool antiAliasingNeedsPostProcess(AntiAliasingMode mode)
{
    return mode == ANTI_ALIASING_CLEAR;
}

static bool drawFrame(uint16_t* pixels, int displayedFps)
{
    if (!g_renderer || !g_frameTexture || !pixels)
    {
        return false;
    }

    uint16_t effectPixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    uint16_t antiAliasPixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    uint16_t* uploadPixels = pixels;
    ColorEffectMode colorEffect = g_frontendSettings ? g_frontendSettings->colorEffect : COLOR_EFFECT_NORMAL;
    bool hasColorEffect = colorEffectNeedsPixelPostProcess(colorEffect);
    VideoAdjustmentParams videoAdjustments;
    bool hasVideoAdjustments = buildVideoAdjustmentParams(&videoAdjustments);
    AntiAliasingMode antiAliasing = currentAntiAliasingMode();
    bool hasAntiAliasPostProcess = antiAliasingNeedsPostProcess(antiAliasing);

    if (hasColorEffect)
    {
        applyColorEffect(effectPixels, pixels, SCREEN_WIDTH * SCREEN_HEIGHT);
        uploadPixels = effectPixels;
    }
    else if (hasVideoAdjustments || hasAntiAliasPostProcess)
    {
        memcpy(effectPixels, pixels, sizeof(effectPixels));
        uploadPixels = effectPixels;
    }

    if (hasAntiAliasPostProcess)
    {
        applyClearAntiAliasingEffect(antiAliasPixels, uploadPixels);
        uploadPixels = antiAliasPixels;
    }

    if (hasVideoAdjustments)
    {
        applyVideoAdjustments(uploadPixels, SCREEN_WIDTH * SCREEN_HEIGHT, videoAdjustments);
    }
    if (portraitModeEnabled())
    {
        rotateFrameCcw(g_lastDisplayFrame, uploadPixels);
        g_lastDisplayFrameWidth = SCREEN_HEIGHT;
        g_lastDisplayFrameHeight = SCREEN_WIDTH;
    }
    else
    {
        memcpy(g_lastDisplayFrame, uploadPixels, sizeof(g_lastDisplayFrame));
        g_lastDisplayFrameWidth = SCREEN_WIDTH;
        g_lastDisplayFrameHeight = SCREEN_HEIGHT;
    }
    g_lastDisplayFrameValid = true;

    if (SDL_UpdateTexture(g_frameTexture, NULL, uploadPixels, SCREEN_WIDTH * sizeof(uint16_t)) != 0)
    {
        printf("frontend: SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    if (SDL_RenderClear(g_renderer) != 0)
    {
        printf("frontend: SDL_RenderClear failed: %s\n", SDL_GetError());
        return false;
    }
    int renderResult = 0;
    if (portraitModeEnabled())
    {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRendererOutputSize(g_renderer, &outputWidth, &outputHeight);
        SDL_Rect dst =
        {
            (outputWidth - outputHeight) / 2,
            (outputHeight - outputWidth) / 2,
            outputHeight,
            outputWidth
        };
        renderResult = SDL_RenderCopyEx(g_renderer, g_frameTexture, NULL, &dst, -90.0, NULL, SDL_FLIP_NONE);
    }
    else
    {
        renderResult = SDL_RenderCopy(g_renderer, g_frameTexture, NULL, NULL);
    }
    if (renderResult != 0)
    {
        printf("frontend: SDL_RenderCopy failed: %s\n", SDL_GetError());
        return false;
    }
    if (pixelGridEffectEnabled())
    {
        drawPixelGridOverlay();
    }
    drawVirtualControlsOverlay();
    drawFpsOverlay(displayedFps);
    SDL_RenderPresent(g_renderer);
    return true;
}

void updateFb(void)
{
    // Called by the HLE LCD bridge when the guest has submitted a complete
    // frame. The frontend consumes this signal without polling the live buffer.
    requestFbUpdate();
}

bool frontendSaveScreenshot(const char* path)
{
    if (!g_lastDisplayFrameValid)
    {
        printf("frontend: screenshot skipped because no display frame is available\n");
        return false;
    }

    std::vector<uint16_t> snapshot;
    int screenshotWidth = 0;
    int screenshotHeight = 0;
    if (!buildDisplaySizedScreenshot(&snapshot, &screenshotWidth, &screenshotHeight))
    {
        printf("frontend: screenshot skipped because display size is unavailable\n");
        return false;
    }

    bool ok = writeScreenshotByExtension(path, snapshot.data(), screenshotWidth, screenshotHeight);
    printf("frontend: screenshot %s size=%dx%d path=%s\n",
        ok ? "saved" : "failed", screenshotWidth, screenshotHeight, path ? path : "");
    return ok;
}

bool frontendSaveScreenshotThumbnail(const char* path, int maxWidth, int maxHeight)
{
    if (!g_lastDisplayFrameValid)
    {
        printf("frontend: thumbnail skipped because no display frame is available\n");
        return false;
    }
    if (!path || !path[0] || maxWidth <= 0 || maxHeight <= 0)
    {
        return false;
    }

    std::vector<uint16_t> snapshot;
    int screenshotWidth = 0;
    int screenshotHeight = 0;
    if (!buildDisplaySizedScreenshot(&snapshot, &screenshotWidth, &screenshotHeight))
    {
        printf("frontend: thumbnail skipped because display size is unavailable\n");
        return false;
    }

    int scaledWidth = maxWidth;
    int scaledHeight = (int)(((int64_t)screenshotHeight * scaledWidth) / screenshotWidth);
    if (scaledHeight > maxHeight)
    {
        scaledHeight = maxHeight;
        scaledWidth = (int)(((int64_t)screenshotWidth * scaledHeight) / screenshotHeight);
    }
    if (scaledWidth <= 0)
    {
        scaledWidth = 1;
    }
    if (scaledHeight <= 0)
    {
        scaledHeight = 1;
    }

    std::vector<uint16_t> thumbnail((size_t)maxWidth * (size_t)maxHeight, 0);
    int offsetX = (maxWidth - scaledWidth) / 2;
    int offsetY = (maxHeight - scaledHeight) / 2;
    for (int y = 0; y < scaledHeight; ++y)
    {
        int srcY = (int)(((int64_t)y * screenshotHeight) / scaledHeight);
        if (srcY >= screenshotHeight)
        {
            srcY = screenshotHeight - 1;
        }
        for (int x = 0; x < scaledWidth; ++x)
        {
            int srcX = (int)(((int64_t)x * screenshotWidth) / scaledWidth);
            if (srcX >= screenshotWidth)
            {
                srcX = screenshotWidth - 1;
            }
            thumbnail[(size_t)(offsetY + y) * (size_t)maxWidth + (size_t)(offsetX + x)] =
                snapshot[(size_t)srcY * (size_t)screenshotWidth + (size_t)srcX];
        }
    }

    bool ok = writeScreenshotByExtension(path, thumbnail.data(), maxWidth, maxHeight);
    printf("frontend: thumbnail %s size=%dx%d path=%s\n",
        ok ? "saved" : "failed", maxWidth, maxHeight, path);
    return ok;
}

bool frontendSaveAutoScreenshot(void)
{
    char path[260];
    if (!buildAutoScreenshotPath(path, sizeof(path)))
    {
        printf("frontend: failed to build automatic screenshot path\n");
        return false;
    }
    return frontendSaveScreenshot(path);
}

bool frontendInit(EmulatorSettings* settings, const char* currentAppPath)
{
    SDL_LogSetOutputFunction(frontendSdlLogOutput, NULL);
    g_frontendSettings = settings;
    g_lastDisplayFrameValid = false;
    g_lastDisplayFrameWidth = displayWidthForSettings(settings);
    g_lastDisplayFrameHeight = displayHeightForSettings(settings);
    SDL_AtomicSet(&g_quitRequested, 0);
    SDL_AtomicSet(&g_gamePaused, 0);
    resetFrontendPauseRequests();
    pauseGateSetPaused(false);
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, (!settings || settings->disableIme) ? "0" : "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    {
        printf("frontend: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    if (!settings || settings->disableIme)
    {
        disableTextComposition();
    }
    else
    {
        enableTextComposition();
    }

    int windowScale = clampedWindowScale(settings);
    g_window = SDL_CreateWindow("DingooPie", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        displayWidthForSettings(settings) * windowScale,
        displayHeightForSettings(settings) * windowScale,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!g_window)
    {
        printf("frontend: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    updateNativeWindowHandle();
    if (!settings || settings->disableIme)
    {
        disableWindowIme();
    }
#ifdef _WIN32
    installNativeWindowSubclass();
    initCommonControlsForNativeWindows();
    applyWindowIcon();
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    registerRawKeyboardInput();
#endif
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);
    openFirstGameController();

    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_renderer)
    {
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_renderer)
    {
        printf("frontend: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    g_frameTexture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!g_frameTexture)
    {
        printf("frontend: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
#ifdef _WIN32
    frontendMenuAttach(g_nativeWindow, settings, currentAppPath ? currentAppPath : "");
#else
    (void)currentAppPath;
#endif

    if (settings)
    {
        frontendApplyVideoSettings(*settings);
        frontendApplyAudioSettings(*settings);
        frontendApplyInputSettings(*settings);
    }

    // Keep startup from exposing the default Win32 client background before the
    // first emulated frame or idle screen is ready.
    g_idleAnimationClock.reset();
    drawIdleScreen(g_idleAnimationClock.advance(SDL_GetTicks64()));
    SDL_ShowWindow(g_window);
    drawIdleScreen(g_idleAnimationClock.advance(SDL_GetTicks64()));
    SDL_RaiseWindow(g_window);
    SDL_SetWindowInputFocus(g_window);

    return true;
}

void frontendRequestQuit(void)
{
    printf("frontend: quit requested\n");
    clearFrontendPauseRequests(false);
    SDL_AtomicSet(&g_quitRequested, 1);
}

bool frontendQuitRequested(void)
{
    return SDL_AtomicGet(&g_quitRequested) != 0;
}

void frontendShutdown(void)
{
    clearFrontendPauseRequests(false);
    closeGameController();
    resetFpsOverlayTexture();
    resetIdleTextures();
    if (g_frameTexture)
    {
        SDL_DestroyTexture(g_frameTexture);
        g_frameTexture = NULL;
    }
    if (g_renderer)
    {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }
    if (g_window)
    {
        enableWindowIme();
#ifdef _WIN32
        uninstallNativeWindowSubclass();
#endif
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
#ifdef _WIN32
    g_nativeWindow = NULL;
    g_defaultImeContext = NULL;
#endif
    g_frontendSettings = NULL;
    g_lastDisplayFrameValid = false;
    SDL_Quit();
    printf("frontend: shutdown complete\n");
}

void frontendRunLoop(const EmulatorOptions& options)
{
    SDL_Event ev;
    bool running = true;
    uint64_t profileLastTicks = SDL_GetTicks64();
    uint64_t fpsLastTicks = profileLastTicks;
    uint32_t profileLoops = 0;
    uint32_t profileDraws = 0;
    uint32_t presentedFrames = 0;
    uint32_t contentFrames = 0;
    uint32_t lastFrameHash = 0;
    uint64_t startTicks = profileLastTicks;
    int displayedPresentedFps = 0;
    int displayedContentFps = 0;
    bool hasPresentedFrame = false;
    uint64_t displayFpsLimit = parsePositiveEnv("DINGOO_PIE_DISPLAY_FPS", 60, 1, 240);
    uint64_t minPresentIntervalMs = 1000 / displayFpsLimit;
    if (minPresentIntervalMs == 0)
    {
        minPresentIntervalMs = 1;
    }
    AutoStateAction autotestSaveStateActions[kMaxAutoStateActions] = {};
    AutoStateAction autotestLoadStateActions[kMaxAutoStateActions] = {};
    AutoVirtualClickEvent autotestVirtualClickEvents[kMaxAutoVirtualClickEvents] = {};
    int autotestSaveStateActionCount = parseAutoStateSequence(
        "DINGOO_PIE_AUTOTEST_SAVE_STATE_SEQUENCE",
        autotestSaveStateActions,
        kMaxAutoStateActions);
    int autotestLoadStateActionCount = parseAutoStateSequence(
        "DINGOO_PIE_AUTOTEST_LOAD_STATE_SEQUENCE",
        autotestLoadStateActions,
        kMaxAutoStateActions);
    int autotestVirtualClickCount = parseAutoVirtualClickSequence(
        autotestVirtualClickEvents,
        kMaxAutoVirtualClickEvents);
    if (autotestSaveStateActionCount == 0)
    {
        uint64_t autotestSaveStateMs = parsePositiveEnv("DINGOO_PIE_AUTOTEST_SAVE_STATE_MS", 0, 0, 60 * 60 * 1000);
        if (autotestSaveStateMs)
        {
            autotestSaveStateActions[0].slot = 1;
            autotestSaveStateActions[0].startMs = autotestSaveStateMs;
            autotestSaveStateActionCount = 1;
        }
    }
    if (autotestLoadStateActionCount == 0)
    {
        uint64_t autotestLoadStateMs = parsePositiveEnv("DINGOO_PIE_AUTOTEST_LOAD_STATE_MS", 0, 0, 60 * 60 * 1000);
        if (autotestLoadStateMs)
        {
            autotestLoadStateActions[0].slot = 1;
            autotestLoadStateActions[0].startMs = autotestLoadStateMs;
            autotestLoadStateActionCount = 1;
        }
    }
    uint64_t lastPresentTicks = 0;
    uint64_t lastIdlePresentCounter = 0;
    uint64_t performanceFrequency = SDL_GetPerformanceFrequency();
    bool pendingFrameRequest = false;
    uint16_t frameCopy[SCREEN_WIDTH * SCREEN_HEIGHT];

    while (running && !SDL_AtomicGet(&g_quitRequested))
    {
        uint64_t loopNow = SDL_GetTicks64();
        uint64_t loopElapsed = loopNow - startTicks;
        runAutoStateActions(autotestSaveStateActions,
            autotestSaveStateActionCount, loopElapsed, true);
        runAutoStateActions(autotestLoadStateActions,
            autotestLoadStateActionCount, loopElapsed, false);
        runAutoVirtualClickActions(autotestVirtualClickEvents,
            autotestVirtualClickCount, loopElapsed);

        frontendMenuRefreshCheats();
        bool drewFrame = false;
        while (SDL_PollEvent(&ev))
        {
            if (!frontendGamePaused() && handleVirtualControlMouseEvent(ev))
            {
                continue;
            }

            if (ev.type == SDL_QUIT)
            {
                printf("frontend: SDL_QUIT event received ignoreQuit=%u\n",
                    options.ignoreQuit ? 1u : 0u);
                printf("frontend: close context app_sha256=%s input=0x%08x last_task=\"%s\" last_hle=\"%s\"\n",
                    bridge_get_app_identity(),
                    inputGetCurrentStatus(),
                    bridge_get_last_task_stop_summary(),
                    bridge_get_last_hle_summary());
                if (!options.ignoreQuit)
                {
                    running = false;
                }
                break;
            }

            switch (ev.type)
            {
            case SDL_KEYDOWN:
#ifdef _WIN32
                if (inputMappingUiKeyboardCapturePending())
                {
                    if (!ev.key.repeat)
                    {
                        inputMappingUiHandleKeyboardScancode(ev.key.keysym.scancode);
                    }
                    break;
                }
#endif
                if (g_controllerMappingPending && !ev.key.repeat &&
                    ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                {
                    cancelControllerMapping();
                    break;
                }
                if (!ev.key.repeat && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                {
                    if (confirmExitRequested())
                    {
                        frontendRequestQuit();
                    }
                    break;
                }
                if (!ev.key.repeat && ev.key.keysym.scancode == SDL_SCANCODE_F12)
                {
                    frontendSaveAutoScreenshot();
                    break;
                }
                if (frontendGamePaused())
                {
                    break;
                }
                if (frontendPostRestoreInputBlocked())
                {
                    break;
                }
                if (!ev.key.repeat)
                {
                    inputHandleHostScancode(ev.key.keysym.scancode, true);
                }
                if (inputTraceEnabled())
                {
                    printf("frontend: keydown key=%s scan=%s repeat=%u focus=%u\n",
                        SDL_GetKeyName(ev.key.keysym.sym),
                        SDL_GetScancodeName(ev.key.keysym.scancode),
                        (unsigned int)ev.key.repeat,
                        (unsigned int)(SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS));
                }
                break;
            case SDL_KEYUP:
#ifdef _WIN32
                if (inputMappingUiKeyboardCapturePending())
                {
                    break;
                }
#endif
                if (frontendGamePaused())
                {
                    break;
                }
                if (frontendPostRestoreInputBlocked())
                {
                    break;
                }
                inputHandleHostScancode(ev.key.keysym.scancode, false);
                if (inputTraceEnabled())
                {
                    printf("frontend: keyup key=%s scan=%s focus=%u\n",
                        SDL_GetKeyName(ev.key.keysym.sym),
                        SDL_GetScancodeName(ev.key.keysym.scancode),
                        (unsigned int)(SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS));
                }
                break;
            case SDL_DROPFILE:
                if (ev.drop.file)
                {
                    std::string appPath(ev.drop.file);
                    printf("frontend: dropped file path=%s\n", appPath.c_str());
                    frontendMenuRequestOpenApp(appPath);
                    SDL_free(ev.drop.file);
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                handleGameControllerDeviceAdded(ev.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                handleGameControllerDeviceRemoved(ev.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                if ((!frontendGamePaused() || g_controllerMappingPending) &&
                    !frontendPostRestoreInputBlocked())
                {
                    handleGameControllerButtonEvent(ev.cbutton);
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if ((!frontendGamePaused() || g_controllerMappingPending) &&
                    !frontendPostRestoreInputBlocked())
                {
                    handleGameControllerAxisEvent(ev.caxis);
                }
                break;
            case SDL_WINDOWEVENT:
                if (inputTraceEnabled())
                {
                    printf("frontend: window event=%u(%s) data1=%d data2=%d\n",
                        (unsigned int)ev.window.event,
                        windowEventName(ev.window.event),
                        ev.window.data1,
                        ev.window.data2);
                }
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                    ev.window.event == SDL_WINDOWEVENT_MINIMIZED)
                {
                    if (inputTraceEnabled())
                    {
                        printf("frontend: window event=%u clearing input\n",
                            (unsigned int)ev.window.event);
                    }
                    releaseFrontendInputControls();
                    if (ev.window.event == SDL_WINDOWEVENT_MINIMIZED &&
                        frontendMenuGameRunning() &&
                        currentMinimizedBehavior() == MINIMIZED_BEHAVIOR_PAUSE)
                    {
                        setMinimizedPauseActive(true);
                    }
                }
                else if (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                    ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                {
                    resetIdleTextures();
                }
                else if (ev.window.event == SDL_WINDOWEVENT_RESTORED)
                {
                    if (g_minimizedPauseActive)
                    {
                        setMinimizedPauseActive(false);
                    }
                }
                else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                {
                    if (g_frontendSettings && g_frontendSettings->disableIme)
                    {
                        disableWindowIme();
                    }
                }
                break;
#ifdef _WIN32
            case SDL_SYSWMEVENT:
                handleSystemWindowEvent(ev);
                break;
#endif
            default:
                break;
            }
        }

        if (frontendGamePaused())
        {
            g_idleAnimationClock.pause();
            SDL_Delay(1);
            continue;
        }

        // Throttle mode keeps the guest running, but lowers frontend polling and
        // presentation cadence while the SDL window is minimized.
        bool minimizedThrottle = isWindowMinimized() &&
            currentMinimizedBehavior() == MINIMIZED_BEHAVIOR_THROTTLE;

        updateVirtualMouseReleaseTimer();
        inputPollKeyboardState();

        if (consumeFbUpdateRequest() != 0)
        {
            pendingFrameRequest = true;
        }
        uint64_t now = SDL_GetTicks64();
        updateAutoPressA(now, startTicks);
        updateAutoPressPlan(now, startTicks);
        updateAutoPressSequence(now, startTicks);
        uint64_t activePresentIntervalMs = minimizedThrottle ?
            kMinimizedThrottlePresentIntervalMs : minPresentIntervalMs;
        bool gameRunning = frontendMenuGameRunning();
        if (gameRunning)
        {
            g_idleAnimationClock.pause();
        }
        uint64_t nowCounter = SDL_GetPerformanceCounter();
        uint64_t idleIntervalUs = idlePresentIntervalUs(activePresentIntervalMs);
        bool idlePresentDue = !hasPresentedFrame || !lastIdlePresentCounter ||
            !performanceFrequency ||
            counterToUs(nowCounter - lastIdlePresentCounter, performanceFrequency) >= idleIntervalUs;
        uint64_t presentIntervalMs = activePresentIntervalMs;
        bool gamePresentDue = !hasPresentedFrame || !lastPresentTicks ||
            now - lastPresentTicks >= presentIntervalMs;
        if (!gameRunning && idlePresentDue)
        {
            if (drawIdleScreen(g_idleAnimationClock.advance(now)))
            {
                drewFrame = true;
                hasPresentedFrame = true;
                lastPresentTicks = now;
                lastIdlePresentCounter = nowCounter;
                if (runtimeLogProfileEnabled())
                {
                    profileDraws++;
                }
                presentedFrames++;
            }
        }
        else if (gameRunning && ((pendingFrameRequest && gamePresentDue) || !hasPresentedFrame))
        {
            copyPresentedFramebuff(frameCopy, sizeof(frameCopy));
            uint32_t frameHash = hashFramePixels(frameCopy);
            bool contentChanged = !hasPresentedFrame || frameHash != lastFrameHash;
            if (contentChanged)
            {
                lastFrameHash = frameHash;
            }

            if (drawFrame(frameCopy, displayedPresentedFps))
            {
                drewFrame = true;
                hasPresentedFrame = true;
                lastPresentTicks = now;
                pendingFrameRequest = false;
                if (runtimeLogProfileEnabled())
                {
                    profileDraws++;
                }
                presentedFrames++;
                if (contentChanged)
                {
                    contentFrames++;
                }
            }
        }

        if (gameRunning && hasPresentedFrame)
        {
            frontendMenuProcessDeferredResourceMonitorOpen();
        }

        now = SDL_GetTicks64();
        if (now - fpsLastTicks >= 1000)
        {
            displayedPresentedFps = (int)((presentedFrames * 1000u) / (uint32_t)(now - fpsLastTicks));
            displayedContentFps = (int)((contentFrames * 1000u) / (uint32_t)(now - fpsLastTicks));
            presentedFrames = 0;
            contentFrames = 0;
            fpsLastTicks = now;
        }

        if (runtimeLogProfileEnabled())
        {
            profileLoops++;
            uint64_t profileElapsed = now - profileLastTicks;
            if (profileElapsed >= runtimeLogProfileIntervalMs())
            {
                bool hasFrontendActivity = profileDraws || displayedPresentedFps || displayedContentFps;
                if (hasFrontendActivity ||
                    runtimeLogShouldPrintEmptyProfile())
                {
                    uint32_t loopsPerSecond = (uint32_t)((profileLoops * 1000ull) / profileElapsed);
                    uint32_t drawsPerSecond = (uint32_t)((profileDraws * 1000ull) / profileElapsed);
                    printf("profile:frontend loops=%u/s draws=%u/s presented_fps=%d submitted_fps=%d content_fps=%d\n",
                        loopsPerSecond, drawsPerSecond, displayedPresentedFps, displayedPresentedFps, displayedContentFps);
                }
                profileLoops = 0;
                profileDraws = 0;
                profileLastTicks = now;
            }
        }
        else
        {
            profileLoops = 0;
            profileDraws = 0;
            profileLastTicks = now;
        }

        if (minimizedThrottle)
        {
            SDL_Delay(kMinimizedThrottleLoopDelayMs);
        }
        else if (!drewFrame)
        {
            SDL_Delay(gameRunning ? 1 :
                idleLoopDelayMs(nowCounter, lastIdlePresentCounter, idleIntervalUs, performanceFrequency));
        }
    }

    printf("frontend: run loop exited running=%u quit=%u\n",
        running ? 1u : 0u, (unsigned int)SDL_AtomicGet(&g_quitRequested));
    resetFpsOverlayTexture();
}

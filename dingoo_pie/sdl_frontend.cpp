#include "sdl_frontend.h"

#include "emulator_config.h"
#include "cheat_finder.h"
#include "debugger_ui.h"
#include "input_controls.h"
#include "framebuffer.h"
#include "frontend_menu.h"
#include "pause_gate.h"
#include "sdk_hle.h"
#include "sdl_audio.h"
#include "resource_ids.h"
#include "ui_strings.h"

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
static int g_fpsOverlayValue = -1;
static int g_fpsOverlayWidth = 0;
static int g_fpsOverlayHeight = 0;
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
static bool g_keyboardMappingPending = false;
static uint32_t g_keyboardMappingTarget = 0;
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
static HWND g_inputMappingWindow = NULL;
static HBRUSH g_inputMappingBackgroundBrush = NULL;
static HFONT g_inputMappingFont = NULL;
static bool g_inputMappingOwnFont = false;
static bool g_menuLoopPauseActive = false;
#endif

static const uint64_t kMinimizedThrottlePresentIntervalMs = 250;
static const uint32_t kMinimizedThrottleLoopDelayMs = 50;

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

static void presentBlackFrame(void)
{
    if (!g_renderer)
    {
        return;
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderPresent(g_renderer);
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
static const uint8_t kLetterL[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f };
static const uint8_t kLetterP[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 };
static const uint8_t kLetterR[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 };
static const uint8_t kLetterS[7] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e };
static const uint8_t kLetterT[7] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
static const uint8_t kLetterU[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
static const uint8_t kLetterX[7] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 };
static const uint8_t kLetterY[7] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 };
static const uint8_t kColon[7]   = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };

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
    case 'L': return kLetterL;
    case 'P': return kLetterP;
    case 'R': return kLetterR;
    case 'S': return kLetterS;
    case 'T': return kLetterT;
    case 'U': return kLetterU;
    case 'X': return kLetterX;
    case 'Y': return kLetterY;
    case ':': return kColon;
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

            if (acceptsInput && !g_keyboardMappingPending)
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
    active = active && frontendMenuGameRunning();
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
        releaseFrontendInputControls();
    }
    pauseGateSetPaused(paused);
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
    --g_modalPauseDepth;
    applyFrontendPauseState(true);
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

struct InputMappingControlRow
{
    uint32_t controlBit;
    const wchar_t* labelEn;
    const wchar_t* labelZh;
};

static const InputMappingControlRow kInputMappingRows[] =
{
    { CONTROL_BUTTON_A, L"A", L"A" },
    { CONTROL_BUTTON_B, L"B", L"B" },
    { CONTROL_BUTTON_X, L"X", L"X" },
    { CONTROL_BUTTON_Y, L"Y", L"Y" },
    { CONTROL_BUTTON_START, L"START", L"START" },
    { CONTROL_BUTTON_SELECT, L"SELECT", L"SELECT" },
    { CONTROL_TRIGGER_LEFT, L"Left Shoulder", L"\u5de6\u80a9\u952e" },
    { CONTROL_TRIGGER_RIGHT, L"Right Shoulder", L"\u53f3\u80a9\u952e" },
    { CONTROL_DPAD_UP, L"D-pad Up", L"\u65b9\u5411\u952e\u4e0a" },
    { CONTROL_DPAD_DOWN, L"D-pad Down", L"\u65b9\u5411\u952e\u4e0b" },
    { CONTROL_DPAD_LEFT, L"D-pad Left", L"\u65b9\u5411\u952e\u5de6" },
    { CONTROL_DPAD_RIGHT, L"D-pad Right", L"\u65b9\u5411\u952e\u53f3" },
};

static const int kInputMappingIdKeyboardBase = 41000;
static const int kInputMappingIdControllerBase = 41100;
static const int kInputMappingIdKeyboardTextBase = 41200;
static const int kInputMappingIdControllerTextBase = 41300;
static const int kInputMappingIdResetKeyboard = 41400;
static const int kInputMappingIdResetController = 41401;
static const int kInputMappingIdClose = 41402;
static const int kInputMappingIdStatus = 41403;

static std::wstring utf8ToWideSimple(const std::string& text)
{
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (size <= 0)
    {
        return asciiToWide(text.c_str());
    }
    std::wstring out((size_t)size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], size);
    return out;
}

static const wchar_t* inputMappingRowLabel(const InputMappingControlRow& row)
{
    return frontendUiLanguage() == UI_LANGUAGE_CHINESE ? row.labelZh : row.labelEn;
}

static int inputMappingRowIndexForButton(int id, int base)
{
    int index = id - base;
    if (index < 0 || index >= (int)(sizeof(kInputMappingRows) / sizeof(kInputMappingRows[0])))
    {
        return -1;
    }
    return index;
}

static HWND inputMappingItem(int id)
{
    return g_inputMappingWindow ? GetDlgItem(g_inputMappingWindow, id) : NULL;
}

static HFONT inputMappingFont(void)
{
    if (g_inputMappingFont)
    {
        return g_inputMappingFont;
    }

    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0))
    {
        g_inputMappingFont = CreateFontIndirectW(&metrics.lfMessageFont);
        g_inputMappingOwnFont = g_inputMappingFont != NULL;
    }
    if (!g_inputMappingFont)
    {
        g_inputMappingFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_inputMappingOwnFont = false;
    }
    return g_inputMappingFont;
}

static void setInputMappingStatus(const wchar_t* text)
{
    HWND status = inputMappingItem(kInputMappingIdStatus);
    if (status)
    {
        SendMessageW(status, WM_SETREDRAW, FALSE, 0);
        SetWindowTextW(status, L"");
        SetWindowTextW(status, text ? text : L"");
        SendMessageW(status, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(status, NULL, TRUE);
        UpdateWindow(status);
    }
}

static void refreshInputMappingWindow(void)
{
    if (!g_inputMappingWindow)
    {
        return;
    }
    for (int i = 0; i < (int)(sizeof(kInputMappingRows) / sizeof(kInputMappingRows[0])); ++i)
    {
        uint32_t controlBit = kInputMappingRows[i].controlBit;
        SetWindowTextW(inputMappingItem(kInputMappingIdKeyboardTextBase + i),
            utf8ToWideSimple(inputKeyboardSourceForControl(controlBit)).c_str());
        SetWindowTextW(inputMappingItem(kInputMappingIdControllerTextBase + i),
            utf8ToWideSimple(frontendControllerSourceForControl(controlBit)).c_str());
    }
}

static void saveKeyboardMappingFromRuntime(void)
{
    if (!g_frontendSettings)
    {
        return;
    }
    g_frontendSettings->keyboardMapping = inputCurrentKeyboardMapping();
    emulatorSaveSettings(*g_frontendSettings);
    frontendMenuRefresh();
}

static void beginKeyboardMappingCapture(uint32_t controlBit)
{
    g_keyboardMappingPending = true;
    g_keyboardMappingTarget = controlBit;
    std::wstring status;
    if (frontendUiLanguage() == UI_LANGUAGE_CHINESE)
    {
        status = L"\u8bf7\u6309\u4e0b\u8981\u6620\u5c04\u5230 ";
        status += controllerTargetDisplayName(controlMask(controlBit));
        status += L" \u7684\u952e\u76d8\u6309\u952e\uff0cEsc \u53d6\u6d88\u3002";
    }
    else
    {
        status = L"Press a keyboard key for ";
        status += controllerTargetDisplayName(controlMask(controlBit));
        status += L"; Esc cancels.";
    }
    setInputMappingStatus(status.c_str());
    SetFocus(g_inputMappingWindow);
}

static SDL_Scancode win32VirtualKeyToScancode(WPARAM virtualKey, LPARAM keyData)
{
    if (virtualKey >= 'A' && virtualKey <= 'Z')
    {
        return (SDL_Scancode)(SDL_SCANCODE_A + (virtualKey - 'A'));
    }
    if (virtualKey >= '1' && virtualKey <= '9')
    {
        return (SDL_Scancode)(SDL_SCANCODE_1 + (virtualKey - '1'));
    }
    if (virtualKey == '0') return SDL_SCANCODE_0;

    switch (virtualKey)
    {
    case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
    case VK_BACK: return SDL_SCANCODE_BACKSPACE;
    case VK_TAB: return SDL_SCANCODE_TAB;
    case VK_RETURN: return (keyData & (1 << 24)) ? SDL_SCANCODE_KP_ENTER : SDL_SCANCODE_RETURN;
    case VK_SPACE: return SDL_SCANCODE_SPACE;
    case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
    case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
    case VK_END: return SDL_SCANCODE_END;
    case VK_HOME: return SDL_SCANCODE_HOME;
    case VK_LEFT: return SDL_SCANCODE_LEFT;
    case VK_UP: return SDL_SCANCODE_UP;
    case VK_RIGHT: return SDL_SCANCODE_RIGHT;
    case VK_DOWN: return SDL_SCANCODE_DOWN;
    case VK_INSERT: return SDL_SCANCODE_INSERT;
    case VK_DELETE: return SDL_SCANCODE_DELETE;
    case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;
    case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
    case VK_SHIFT: return SDL_SCANCODE_LSHIFT;
    case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
    case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
    case VK_CONTROL: return SDL_SCANCODE_LCTRL;
    case VK_LMENU: return SDL_SCANCODE_LALT;
    case VK_RMENU: return SDL_SCANCODE_RALT;
    case VK_MENU: return SDL_SCANCODE_LALT;
    case VK_F1: return SDL_SCANCODE_F1;
    case VK_F2: return SDL_SCANCODE_F2;
    case VK_F3: return SDL_SCANCODE_F3;
    case VK_F4: return SDL_SCANCODE_F4;
    case VK_F5: return SDL_SCANCODE_F5;
    case VK_F6: return SDL_SCANCODE_F6;
    case VK_F7: return SDL_SCANCODE_F7;
    case VK_F8: return SDL_SCANCODE_F8;
    case VK_F9: return SDL_SCANCODE_F9;
    case VK_F10: return SDL_SCANCODE_F10;
    case VK_F11: return SDL_SCANCODE_F11;
    case VK_F12: return SDL_SCANCODE_F12;
    default:
        break;
    }

    return SDL_GetScancodeFromKey((SDL_Keycode)MapVirtualKeyW((UINT)virtualKey, MAPVK_VK_TO_CHAR));
}

static void finishKeyboardMappingCapture(SDL_Scancode scancode)
{
    if (!g_keyboardMappingPending)
    {
        return;
    }
    uint32_t target = g_keyboardMappingTarget;
    g_keyboardMappingPending = false;
    g_keyboardMappingTarget = 0;
    if (scancode != SDL_SCANCODE_ESCAPE && inputSetKeyboardMappingForControl(target, scancode))
    {
        saveKeyboardMappingFromRuntime();
        refreshInputMappingWindow();
        setInputMappingStatus(frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
            L"\u952e\u76d8\u6620\u5c04\u5df2\u4fdd\u5b58\u3002" : L"Keyboard mapping saved.");
        printf("frontend: keyboard mapping saved target=%s source=%s spec='%s'\n",
            controllerTargetName(controlMask(target)),
            SDL_GetScancodeName(scancode),
            g_frontendSettings ? g_frontendSettings->keyboardMapping.c_str() : inputCurrentKeyboardMapping().c_str());
    }
    else
    {
        setInputMappingStatus(frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
            L"\u952e\u76d8\u6620\u5c04\u5df2\u53d6\u6d88\u3002" : L"Keyboard mapping cancelled.");
    }
}

static LRESULT CALLBACK inputMappingWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int keyboardIndex = inputMappingRowIndexForButton(id, kInputMappingIdKeyboardBase);
        if (keyboardIndex >= 0)
        {
            beginKeyboardMappingCapture(kInputMappingRows[keyboardIndex].controlBit);
            return 0;
        }
        int controllerIndex = inputMappingRowIndexForButton(id, kInputMappingIdControllerBase);
        if (controllerIndex >= 0)
        {
            frontendBeginControllerMapping(kInputMappingRows[controllerIndex].controlBit);
            refreshInputMappingWindow();
            return 0;
        }
        if (id == kInputMappingIdResetKeyboard)
        {
            inputResetKeyboardMapping();
            saveKeyboardMappingFromRuntime();
            refreshInputMappingWindow();
            setInputMappingStatus(frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
                L"\u952e\u76d8\u6620\u5c04\u5df2\u6062\u590d\u9ed8\u8ba4\u3002" : L"Keyboard mapping restored to defaults.");
            return 0;
        }
        if (id == kInputMappingIdResetController)
        {
            frontendResetControllerMapping();
            refreshInputMappingWindow();
            setInputMappingStatus(frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
                L"\u624b\u67c4\u6620\u5c04\u5df2\u6062\u590d\u9ed8\u8ba4\u3002" : L"Controller mapping restored to defaults.");
            return 0;
        }
        if (id == kInputMappingIdClose)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (g_keyboardMappingPending)
        {
            finishKeyboardMappingCapture(win32VirtualKeyToScancode(wParam, lParam));
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        int rowCount = (int)(sizeof(kInputMappingRows) / sizeof(kInputMappingRows[0]));
        bool valueField =
            (id >= kInputMappingIdKeyboardTextBase && id < kInputMappingIdKeyboardTextBase + rowCount) ||
            (id >= kInputMappingIdControllerTextBase && id < kInputMappingIdControllerTextBase + rowCount);
        if (valueField || id == kInputMappingIdStatus)
        {
            SetBkMode((HDC)wParam, OPAQUE);
            if (valueField)
            {
                SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
                SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
            SetTextColor((HDC)wParam, GetSysColor(COLOR_BTNTEXT));
            return (LRESULT)(g_inputMappingBackgroundBrush ?
                g_inputMappingBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
        }
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)(g_inputMappingBackgroundBrush ?
            g_inputMappingBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_inputMappingWindow == hwnd)
        {
            g_inputMappingWindow = NULL;
            g_keyboardMappingPending = false;
            g_keyboardMappingTarget = 0;
        }
        if (g_inputMappingFont && g_inputMappingOwnFont)
        {
            DeleteObject(g_inputMappingFont);
        }
        g_inputMappingFont = NULL;
        g_inputMappingOwnFont = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensureInputMappingWindowClass(void)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = inputMappingWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DingooPieInputMappingWindow";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = loadDingooPieIcon(32);
    wc.hbrBackground = g_inputMappingBackgroundBrush;
    RegisterClassW(&wc);
    registered = true;
}

static HWND createInputMappingChild(HWND parent, const wchar_t* className, const wchar_t* text,
    DWORD exStyle, DWORD style, int x, int y, int w, int h, int id)
{
    HWND child = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (child)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)inputMappingFont(), TRUE);
        SetWindowTheme(child, L"Explorer", NULL);
    }
    return child;
}

void frontendOpenInputMappingWindow(void)
{
    if (g_inputMappingWindow)
    {
        ShowWindow(g_inputMappingWindow, SW_SHOWNOACTIVATE);
        return;
    }

    bool zh = frontendUiLanguage() == UI_LANGUAGE_CHINESE;
    g_inputMappingBackgroundBrush = GetSysColorBrush(COLOR_BTNFACE);
    ensureInputMappingWindowClass();
    int rowCount = (int)(sizeof(kInputMappingRows) / sizeof(kInputMappingRows[0]));
    int width = 760;
    int height = 100 + rowCount * 34 + 54;
    g_inputMappingWindow = CreateWindowExW(WS_EX_CONTROLPARENT,
        L"DingooPieInputMappingWindow",
        zh ? L"\u6309\u952e\u6620\u5c04" : L"Input Mapping",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_inputMappingWindow)
    {
        return;
    }
    applyDingooPieIconToWindow(g_inputMappingWindow);

    createInputMappingChild(g_inputMappingWindow, L"STATIC", zh ? L"\u63a7\u5236" : L"Control",
        0, 0, 18, 16, 120, 20, -1);
    createInputMappingChild(g_inputMappingWindow, L"STATIC", zh ? L"\u952e\u76d8" : L"Keyboard",
        0, 0, 150, 16, 160, 20, -1);
    createInputMappingChild(g_inputMappingWindow, L"STATIC", zh ? L"\u624b\u67c4 / \u6447\u6746" : L"Controller / Stick",
        0, 0, 390, 16, 180, 20, -1);

    for (int i = 0; i < rowCount; ++i)
    {
        int y = 44 + i * 34;
        createInputMappingChild(g_inputMappingWindow, L"STATIC", inputMappingRowLabel(kInputMappingRows[i]),
            0, 0, 18, y + 4, 120, 22, -1);
        createInputMappingChild(g_inputMappingWindow, L"EDIT", L"",
            WS_EX_CLIENTEDGE, ES_READONLY | ES_AUTOHSCROLL,
            150, y, 150, 24, kInputMappingIdKeyboardTextBase + i);
        createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u8bbe\u7f6e\u952e\u76d8" : L"Set Key",
            0, 0, 306, y - 1, 74, 26, kInputMappingIdKeyboardBase + i);
        createInputMappingChild(g_inputMappingWindow, L"EDIT", L"",
            WS_EX_CLIENTEDGE, ES_READONLY | ES_AUTOHSCROLL,
            390, y, 170, 24, kInputMappingIdControllerTextBase + i);
        createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u8bbe\u7f6e\u624b\u67c4" : L"Set Pad",
            0, 0, 566, y - 1, 82, 26, kInputMappingIdControllerBase + i);
    }

    int bottomY = 48 + rowCount * 34;
    createInputMappingChild(g_inputMappingWindow, L"STATIC", L"",
        0, 0, 18, bottomY, 520, 24, kInputMappingIdStatus);
    createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u6062\u590d\u952e\u76d8\u9ed8\u8ba4" : L"Reset Keyboard",
        0, 0, 18, bottomY + 30, 136, 28, kInputMappingIdResetKeyboard);
    createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u6062\u590d\u624b\u67c4\u9ed8\u8ba4" : L"Reset Controller",
        0, 0, 162, bottomY + 30, 140, 28, kInputMappingIdResetController);
    createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u5173\u95ed" : L"Close",
        0, 0, 650, bottomY + 30, 74, 28, kInputMappingIdClose);

    refreshInputMappingWindow();
    setInputMappingStatus(zh ?
        L"\u70b9\u51fb\u8bbe\u7f6e\u952e\u76d8\u6216\u8bbe\u7f6e\u624b\u67c4\u540e\u6309\u4e0b\u76ee\u6807\u6309\u952e\u3002" :
        L"Choose Set Key or Set Pad, then press the target input.");
    ShowWindow(g_inputMappingWindow, SW_SHOWNOACTIVATE);
    UpdateWindow(g_inputMappingWindow);
}

void frontendOpenCheatFinderWindow(void)
{
#ifdef _WIN32
    if (!frontendMenuGameRunning())
    {
        return;
    }
    cheatFinderOpenWindow(g_nativeWindow, frontendUiLanguage());
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
    refreshInputMappingWindow();
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
        setInputMappingStatus(frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
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
    refreshInputMappingWindow();
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
        setInputMappingStatus(frontendUiLanguage() == UI_LANGUAGE_CHINESE ?
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
    printf("frontend: audio settings volume=%d buffer_samples=%d drop_audio=%u\n",
        settings.audioVolumePercent,
        settings.audioBufferSamples,
        settings.dropAudio ? 1u : 0u);
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

struct AutoPressSequenceEvent
{
    uint32_t controlBit;
    uint64_t startMs;
    uint64_t holdMs;
};

static const int kMaxAutoPressSequenceEvents = 64;

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

static int parseAutoPressSequence(AutoPressSequenceEvent* events, int capacity)
{
    static int initialized = 0;
    static AutoPressSequenceEvent parsedEvents[kMaxAutoPressSequenceEvents] = {};
    static int parsedCount = 0;

    if (!initialized)
    {
        const char* spec = getenv("DINGOO_PIE_AUTOPRESS_SEQUENCE");
        if (spec && spec[0])
        {
            char buffer[2048];
            snprintf(buffer, sizeof(buffer), "%s", spec);

            char* token = buffer;
            while (token && *token && parsedCount < kMaxAutoPressSequenceEvents)
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
                        parsedEvents[parsedCount].controlBit = controlBit;
                        parsedEvents[parsedCount].startMs = startMs;
                        parsedEvents[parsedCount].holdMs = holdMs;
                        parsedCount++;
                    }
                    else
                    {
                        printf("frontend: invalid autopress sequence token='%s@%s:%s'\n",
                            token, at + 1, colon + 1);
                    }
                }
                else if (tokenLength > 0)
                {
                    printf("frontend: invalid autopress sequence token='%s'\n", token);
                }

                token = next;
            }

            printf("frontend: autopress sequence events=%d spec='%s'\n", parsedCount, spec);
        }
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

static void updateAutoPressSequence(uint64_t now, uint64_t startTicks)
{
    AutoPressSequenceEvent events[kMaxAutoPressSequenceEvents];
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

    // Keep restart relaunches from exposing the default Win32 client background
    // before the first emulated frame is ready.
    presentBlackFrame();
    SDL_ShowWindow(g_window);
    presentBlackFrame();
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
    uint64_t autotestSaveStateMs = parsePositiveEnv("DINGOO_PIE_AUTOTEST_SAVE_STATE_MS", 0, 0, 60 * 60 * 1000);
    uint64_t autotestLoadStateMs = parsePositiveEnv("DINGOO_PIE_AUTOTEST_LOAD_STATE_MS", 0, 0, 60 * 60 * 1000);
    bool autotestSaveStateDone = autotestSaveStateMs == 0;
    bool autotestLoadStateDone = autotestLoadStateMs == 0;
    uint64_t lastPresentTicks = 0;
    bool pendingFrameRequest = false;
    uint16_t frameCopy[SCREEN_WIDTH * SCREEN_HEIGHT];

    while (running && !SDL_AtomicGet(&g_quitRequested))
    {
        uint64_t loopNow = SDL_GetTicks64();
        uint64_t loopElapsed = loopNow - startTicks;
        if (!autotestSaveStateDone && loopElapsed >= autotestSaveStateMs)
        {
            autotestSaveStateDone = true;
            frontendMenuSaveStateSlotForAutomation(1);
        }
        if (!autotestLoadStateDone && loopElapsed >= autotestLoadStateMs)
        {
            autotestLoadStateDone = true;
            frontendMenuLoadStateSlotForAutomation(1);
        }

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
                if (g_keyboardMappingPending)
                {
                    if (!ev.key.repeat)
                    {
                        finishKeyboardMappingCapture(ev.key.keysym.scancode);
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
                if (g_keyboardMappingPending)
                {
                    break;
                }
#endif
                if (frontendGamePaused())
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
                if (!frontendGamePaused() || g_controllerMappingPending)
                {
                    handleGameControllerButtonEvent(ev.cbutton);
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (!frontendGamePaused() || g_controllerMappingPending)
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
        bool presentDue = !hasPresentedFrame || !lastPresentTicks || now - lastPresentTicks >= activePresentIntervalMs;
        if ((pendingFrameRequest && presentDue) || !hasPresentedFrame)
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
                profileDraws++;
                presentedFrames++;
                if (contentChanged)
                {
                    contentFrames++;
                }
            }
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

        if (g_frontendSettings && g_frontendSettings->debugProfile)
        {
            profileLoops++;
            if (now - profileLastTicks >= 1000)
            {
                printf("profile frontend: loops=%u/s draws=%u/s presented_fps=%d submitted_fps=%d content_fps=%d\n",
                    profileLoops, profileDraws, displayedPresentedFps, displayedPresentedFps, displayedContentFps);
                profileLoops = 0;
                profileDraws = 0;
                profileLastTicks = now;
            }
        }

        if (minimizedThrottle)
        {
            SDL_Delay(kMinimizedThrottleLoopDelayMs);
        }
        else if (!drewFrame)
        {
            SDL_Delay(1);
        }
    }

    printf("frontend: run loop exited running=%u quit=%u\n",
        running ? 1u : 0u, (unsigned int)SDL_AtomicGet(&g_quitRequested));
    resetFpsOverlayTexture();
}


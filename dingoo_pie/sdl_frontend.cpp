#include "sdl_frontend.h"

#include "emulator_config.h"
#include "input_controls.h"
#include "framebuffer.h"
#include "frontend_menu.h"
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
#include <propidl.h>
#include <imm.h>
#include <gdiplus.h>
#endif
#include <ctype.h>
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
static SDL_atomic_t g_quitRequested;
static EmulatorSettings* g_frontendSettings = NULL;
static uint16_t g_lastDisplayFrame[SCREEN_WIDTH * SCREEN_HEIGHT];
static int g_lastDisplayFrameWidth = SCREEN_WIDTH;
static int g_lastDisplayFrameHeight = SCREEN_HEIGHT;
static bool g_lastDisplayFrameValid = false;
#ifdef _WIN32
static HWND g_nativeWindow = NULL;
static HIMC g_defaultImeContext = NULL;
#endif

static bool inputTraceEnabled(void);

static bool confirmExitRequested(void)
{
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
static void applyWindowIcon(void)
{
    if (!g_window)
    {
        return;
    }

    HICON largeIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    HICON smallIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    HWND window = g_nativeWindow;
    if (window)
    {
        if (largeIcon)
        {
            SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
        }
        if (smallIcon)
        {
            SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
        }
    }
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

static void drawGlyph(const uint8_t* glyph, int x, int y, int scale, SDL_Color color)
{
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

static void drawText(const char* text, int x, int y, int scale, SDL_Color color)
{
    int cursor = x;
    for (const char* p = text; *p; ++p)
    {
        const uint8_t* glyph = glyphForChar(*p);
        if (glyph)
        {
            drawGlyph(glyph, cursor, y, scale, color);
        }
        cursor += 6 * scale;
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

    uint32_t held = g_virtualMouseControls;
    g_virtualMouseControls = 0;
    g_virtualMouseReleaseTicks = 0;
    for (uint32_t bit = 0; bit < 32; ++bit)
    {
        if (held & (1u << bit))
        {
            inputSetSyntheticControl(bit, false);
        }
    }
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

    for (uint32_t bit = 0; bit < 32; ++bit)
    {
        uint32_t mask = 1u << bit;
        if (changed & mask)
        {
            inputSetSyntheticControl(bit, (newMask & mask) != 0);
        }
    }
    g_virtualMouseControls = newMask;
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

    *outWidth = width;
    *outHeight = height;
    return true;
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

            if (acceptsInput)
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
    else if (msg->msg.win.msg == WM_COMMAND)
    {
        frontendMenuHandleCommand(LOWORD(msg->msg.win.wParam));
    }
}
#endif

#ifdef _WIN32
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
            settings.videoFilter == VIDEO_FILTER_LINEAR ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }

    printf("frontend: video settings scale=%d fullscreen=%u filter=%s effect=%s brightness=%d contrast=%d saturation=%d portrait=%u show_fps=%u virtual_controls=%u\n",
        clampedWindowScale(&settings),
        settings.fullscreen ? 1u : 0u,
        emulatorVideoFilterName(settings.videoFilter),
        emulatorColorEffectName(settings.colorEffect),
        settings.brightnessPercent,
        settings.contrastPercent,
        settings.saturationPercent,
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
    if (settings.disableIme)
    {
        disableWindowIme();
    }
    else
    {
        enableWindowIme();
    }
    printf("frontend: input settings virtual_controls=%u disable_ime=%u\n",
        settings.showVirtualControls ? 1u : 0u,
        settings.disableIme ? 1u : 0u);
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

static uint16_t rgb565ToInvertedGrayscale(uint16_t pixel)
{
    uint16_t gray = rgb565ToGrayscale(pixel);
    return rgb565Invert(gray);
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

static uint16_t rgb565ToAmber(uint16_t pixel)
{
    uint32_t r8 = 0;
    uint32_t g8 = 0;
    uint32_t b8 = 0;
    rgb565ToRgb888(pixel, &r8, &g8, &b8);
    uint32_t y8 = (77 * r8 + 150 * g8 + 29 * b8) >> 8;

    int r = 22 + ((int)y8 * 234) / 255;
    int g = 10 + ((int)y8 * 142) / 255;
    int b = ((int)y8 * 24) / 255;
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

static uint16_t applyVideoAdjustmentsToPixel(uint16_t pixel)
{
    if (!g_frontendSettings)
    {
        return pixel;
    }

    int brightness = clampPercent(g_frontendSettings->brightnessPercent, 50, 150);
    int contrast = clampPercent(g_frontendSettings->contrastPercent, 50, 150);
    int saturation = clampPercent(g_frontendSettings->saturationPercent, 0, 200);
    if (brightness == 100 && contrast == 100 && saturation == 100)
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

    r = ((r - 128) * contrast) / 100 + 128;
    g = ((g - 128) * contrast) / 100 + 128;
    b = ((b - 128) * contrast) / 100 + 128;

    r = (r * brightness) / 100;
    g = (g * brightness) / 100;
    b = (b * brightness) / 100;

    int y = (77 * r + 150 * g + 29 * b) >> 8;
    r = y + ((r - y) * saturation) / 100;
    g = y + ((g - y) * saturation) / 100;
    b = y + ((b - y) * saturation) / 100;

    return rgb888ToRgb565(clampColor8(r), clampColor8(g), clampColor8(b));
}

static void applyVideoAdjustments(uint16_t* pixels, size_t pixelCount)
{
    if (!pixels || !g_frontendSettings)
    {
        return;
    }

    int brightness = clampPercent(g_frontendSettings->brightnessPercent, 50, 150);
    int contrast = clampPercent(g_frontendSettings->contrastPercent, 50, 150);
    int saturation = clampPercent(g_frontendSettings->saturationPercent, 0, 200);
    if (brightness == 100 && contrast == 100 && saturation == 100)
    {
        return;
    }

    for (size_t i = 0; i < pixelCount; ++i)
    {
        pixels[i] = applyVideoAdjustmentsToPixel(pixels[i]);
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
    if (effect == COLOR_EFFECT_INVERT_GRAYSCALE)
    {
        for (size_t i = 0; i < pixelCount; ++i)
        {
            dst[i] = rgb565ToInvertedGrayscale(src[i]);
        }
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
    if (effect == COLOR_EFFECT_AMBER)
    {
        for (size_t i = 0; i < pixelCount; ++i)
        {
            dst[i] = rgb565ToAmber(src[i]);
        }
        return;
    }
    if (effect == COLOR_EFFECT_SHARPEN)
    {
        applySharpenEffect(dst, src);
        return;
    }
    if (effect == COLOR_EFFECT_SOFT_BLUR)
    {
        applySoftBlurEffect(dst, src);
        return;
    }
    if (effect == COLOR_EFFECT_LCD_SCANLINE)
    {
        applyLcdScanlineEffect(dst, src);
        return;
    }

    if (dst != src)
    {
        memcpy(dst, src, pixelCount * sizeof(uint16_t));
    }
}

static bool drawFrame(uint16_t* pixels, int displayedFps)
{
    if (!g_renderer || !g_frameTexture || !pixels)
    {
        return false;
    }

    uint16_t effectPixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    uint16_t* uploadPixels = pixels;
    bool hasColorEffect = g_frontendSettings && g_frontendSettings->colorEffect != COLOR_EFFECT_NORMAL;
    bool hasVideoAdjustments = g_frontendSettings &&
        (g_frontendSettings->brightnessPercent != 100 ||
            g_frontendSettings->contrastPercent != 100 ||
            g_frontendSettings->saturationPercent != 100);

    if (hasColorEffect)
    {
        applyColorEffect(effectPixels, pixels, SCREEN_WIDTH * SCREEN_HEIGHT);
        uploadPixels = effectPixels;
    }
    else if (hasVideoAdjustments)
    {
        memcpy(effectPixels, pixels, sizeof(effectPixels));
        uploadPixels = effectPixels;
    }

    if (hasVideoAdjustments)
    {
        applyVideoAdjustments(uploadPixels, SCREEN_WIDTH * SCREEN_HEIGHT);
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
    g_frontendSettings = settings;
    g_lastDisplayFrameValid = false;
    g_lastDisplayFrameWidth = displayWidthForSettings(settings);
    g_lastDisplayFrameHeight = displayHeightForSettings(settings);
    SDL_AtomicSet(&g_quitRequested, 0);
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, (!settings || settings->disableIme) ? "0" : "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0)
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
        SDL_WINDOW_RESIZABLE);
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
    applyWindowIcon();
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    registerRawKeyboardInput();
#endif

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

    SDL_RaiseWindow(g_window);
    SDL_SetWindowInputFocus(g_window);

    return true;
}

void frontendRequestQuit(void)
{
    printf("frontend: quit requested\n");
    SDL_AtomicSet(&g_quitRequested, 1);
}

void frontendShutdown(void)
{
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
    uint64_t lastPresentTicks = 0;
    bool pendingFrameRequest = false;
    uint16_t frameCopy[SCREEN_WIDTH * SCREEN_HEIGHT];

    while (running && !SDL_AtomicGet(&g_quitRequested))
    {
        bool drewFrame = false;
        while (SDL_PollEvent(&ev))
        {
            if (handleVirtualControlMouseEvent(ev))
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
                inputHandleHostScancode(ev.key.keysym.scancode, false);
                if (inputTraceEnabled())
                {
                    printf("frontend: keyup key=%s scan=%s focus=%u\n",
                        SDL_GetKeyName(ev.key.keysym.sym),
                        SDL_GetScancodeName(ev.key.keysym.scancode),
                        (unsigned int)(SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS));
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
                    releaseVirtualMouseControls();
                    inputClearControls();
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
        bool presentDue = !hasPresentedFrame || !lastPresentTicks || now - lastPresentTicks >= minPresentIntervalMs;
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

        if (!drewFrame)
        {
            SDL_Delay(1);
        }
    }

    printf("frontend: run loop exited running=%u quit=%u\n",
        running ? 1u : 0u, (unsigned int)SDL_AtomicGet(&g_quitRequested));
    resetFpsOverlayTexture();
}


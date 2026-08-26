#include "frontend/video/frame_processor.h"

#include "shared/config/runtime_constants.h"

#include <math.h>
#include <string.h>

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
                pixel = frontendBlendRgb565WithBlack(pixel, 48);
            }
            else if ((x & 1) != 0)
            {
                pixel = frontendBlendRgb565WithBlack(pixel, 12);
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

static bool buildVideoAdjustmentParams(const EmulatorSettings* settings, VideoAdjustmentParams* params)
{
    if (!params || !settings)
    {
        return false;
    }

    params->brightness = clampPercent(settings->brightnessPercent, 50, 150);
    params->contrast = clampPercent(settings->contrastPercent, 50, 150);
    params->gamma = clampPercent(settings->gammaPercent, 50, 150);
    params->saturation = clampPercent(settings->saturationPercent, 0, 200);
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

uint16_t frontendBlendRgb565WithBlack(uint16_t pixel, uint32_t blackAlpha)
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

static void applyColorEffect(
    uint16_t* dst,
    const uint16_t* src,
    size_t pixelCount,
    ColorEffectMode effect)
{
    if (!dst || !src)
    {
        return;
    }

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
static bool colorEffectNeedsPixelPostProcess(ColorEffectMode effect)
{
    return effect != COLOR_EFFECT_NORMAL && effect != COLOR_EFFECT_PIXEL_GRID;
}

static bool antiAliasingNeedsPostProcess(AntiAliasingMode mode)
{
    return mode == ANTI_ALIASING_CLEAR;
}

uint16_t* frontendProcessFramePixels(
    uint16_t* sourcePixels,
    uint16_t* effectPixels,
    uint16_t* antiAliasPixels,
    size_t pixelCount,
    const EmulatorSettings* settings)
{
    if (!sourcePixels || !effectPixels || !antiAliasPixels ||
        pixelCount != (size_t)SCREEN_WIDTH * SCREEN_HEIGHT)
    {
        return sourcePixels;
    }

    ColorEffectMode colorEffect = settings ? settings->colorEffect : COLOR_EFFECT_NORMAL;
    AntiAliasingMode antiAliasing = settings ? settings->antiAliasing : ANTI_ALIASING_OFF;
    VideoAdjustmentParams videoAdjustments;
    bool hasColorEffect = colorEffectNeedsPixelPostProcess(colorEffect);
    bool hasVideoAdjustments = buildVideoAdjustmentParams(settings, &videoAdjustments);
    bool hasAntiAliasPostProcess = antiAliasingNeedsPostProcess(antiAliasing);
    uint16_t* processedPixels = sourcePixels;

    if (hasColorEffect)
    {
        applyColorEffect(effectPixels, sourcePixels, pixelCount, colorEffect);
        processedPixels = effectPixels;
    }
    else if (hasVideoAdjustments || hasAntiAliasPostProcess)
    {
        memcpy(effectPixels, sourcePixels, pixelCount * sizeof(uint16_t));
        processedPixels = effectPixels;
    }

    if (hasAntiAliasPostProcess)
    {
        applyClearAntiAliasingEffect(antiAliasPixels, processedPixels);
        processedPixels = antiAliasPixels;
    }

    if (hasVideoAdjustments)
    {
        applyVideoAdjustments(processedPixels, pixelCount, videoAdjustments);
    }
    return processedPixels;
}

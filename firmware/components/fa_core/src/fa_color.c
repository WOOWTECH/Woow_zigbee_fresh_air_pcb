#include "fa_color.h"
#include <math.h>

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static float gamma_srgb(float v) { return v <= 0.0031308f ? 12.92f * v : 1.055f * powf(v, 1 / 2.4f) - 0.055f; }

/* 三個分量先正規化到最大＝1，再乘亮度與上限；亮度 ≥1 時主色至少 1，最暗也看得到 */
static fa_rgb_t scale(float r, float g, float b, uint8_t level, uint8_t max)
{
    float m = fmaxf(r, fmaxf(g, b));
    if (m <= 0) r = g = b = m = 1;
    float k = (float)max * (level > 254 ? 254 : level) / 254;
    float v[3] = {r / m, g / m, b / m};
    uint8_t o[3];
    for (int i = 0; i < 3; i++) {
        o[i] = (uint8_t)(v[i] * k + 0.5f);
        if (o[i] == 0 && level > 0 && v[i] > 0.999f) o[i] = 1;
    }
    return (fa_rgb_t){o[0], o[1], o[2]};
}

fa_rgb_t fa_color_from_xy(uint16_t xi, uint16_t yi, uint8_t level, uint8_t max)
{
    float x = xi / 65536.0f, y = yi / 65536.0f;
    if (y < 0.0001f) return scale(1, 1, 1, level, max);
    float X = x / y, Z = (1 - x - y) / y;                 /* Y＝1 */
    float r = 3.2406f * X - 1.5372f - 0.4986f * Z;         /* XYZ → 線性 sRGB（D65） */
    float g = -0.9689f * X + 1.8758f + 0.0415f * Z;
    float b = 0.0557f * X - 0.2040f + 1.0570f * Z;
    r = fmaxf(r, 0); g = fmaxf(g, 0); b = fmaxf(b, 0);
    float m = fmaxf(r, fmaxf(g, b));
    if (m <= 0) return scale(1, 1, 1, level, max);
    return scale(gamma_srgb(r / m), gamma_srgb(g / m), gamma_srgb(b / m), level, max);
}

fa_rgb_t fa_color_from_mireds(uint16_t mireds, uint8_t level, uint8_t max)
{
    float kelvin = mireds ? 1e6f / mireds : 40000;
    if (kelvin < 1000) kelvin = 1000;
    if (kelvin > 40000) kelvin = 40000;
    float t = kelvin / 100, r, g, b;                       /* Tanner Helland 近似，輸出 0–255 */
    if (t <= 66) {
        r = 255;
        g = 99.4708025861f * logf(t) - 161.1195681661f;
        b = t <= 19 ? 0 : 138.5177312231f * logf(t - 10) - 305.0447927307f;
    } else {
        r = 329.698727446f * powf(t - 60, -0.1332047592f);
        g = 288.1221695283f * powf(t - 60, -0.0755148492f);
        b = 255;
    }
    return scale(clamp01(r / 255), clamp01(g / 255), clamp01(b / 255), level, max);
}

uint16_t fa_color_hue8_to16(uint8_t hue8)
{
    uint32_t h = (uint32_t)(hue8 > 254 ? 254 : hue8) * 65536u / 254u;   /* 254＝360° */
    return h > 65535 ? 65535 : (uint16_t)h;
}

fa_rgb_t fa_color_from_hs(uint16_t hue16, uint8_t sat, uint8_t level, uint8_t max)
{
    float h = hue16 * 6.0f / 65536.0f;                     /* 0–6：紅黃綠青藍洋紅 */
    float s = (sat > 254 ? 254 : sat) / 254.0f;
    int i = (int)h;
    float f = h - i, p = 1 - s, q = 1 - s * f, t = 1 - s * (1 - f);
    float r, g, b;
    switch (i % 6) {
    case 0: r = 1; g = t; b = p; break;
    case 1: r = q; g = 1; b = p; break;
    case 2: r = p; g = 1; b = t; break;
    case 3: r = p; g = q; b = 1; break;
    case 4: r = t; g = p; b = 1; break;
    default: r = 1; g = p; b = q; break;
    }
    return scale(r, g, b, level, max);
}

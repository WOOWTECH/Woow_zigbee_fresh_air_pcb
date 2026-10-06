/* Matter Color Control → RGB（開發板板載燈當 Extended Color Light 用）。純函式，主機可測。
 * Matter 單位：CurrentX／CurrentY＝CIE 1931 xy × 65536；色溫用 mireds（10^6／K）；CurrentLevel 1–254。
 * 輸出已乘上亮度，最大值壓在 max（WS2812 全亮太刺眼）。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fa_led.h"

fa_rgb_t fa_color_from_xy(uint16_t x, uint16_t y, uint8_t level, uint8_t max);
fa_rgb_t fa_color_from_mireds(uint16_t mireds, uint8_t level, uint8_t max);
/* 色相／飽和度（塗鴉等 controller 的色盤用這個）：hue16＝一圈 65536（EnhancedCurrentHue 原值；
 * 8-bit CurrentHue 0–254 用 fa_color_hue8_to16 換算），sat 0–254 */
uint16_t fa_color_hue8_to16(uint8_t hue8);
fa_rgb_t fa_color_from_hs(uint16_t hue16, uint8_t sat, uint8_t level, uint8_t max);

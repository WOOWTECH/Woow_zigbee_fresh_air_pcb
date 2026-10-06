/* Matter Color Control → RGB（開發板板載燈當 Extended Color Light 用）。純函式，主機可測。
 * Matter 單位：CurrentX／CurrentY＝CIE 1931 xy × 65536；色溫用 mireds（10^6／K）；CurrentLevel 1–254。
 * 輸出已乘上亮度，最大值壓在 max（WS2812 全亮太刺眼）。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fa_led.h"

fa_rgb_t fa_color_from_xy(uint16_t x, uint16_t y, uint8_t level, uint8_t max);
fa_rgb_t fa_color_from_mireds(uint16_t mireds, uint8_t level, uint8_t max);

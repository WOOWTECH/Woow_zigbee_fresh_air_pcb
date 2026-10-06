/* 單顆 WS2812（開發板板載 RGB 燈）：RMT bytes encoder，不另外拉 led_strip 元件。 */
#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t ws2812_init(int gpio);
esp_err_t ws2812_set(uint8_t r, uint8_t g, uint8_t b);   /* 沒變就不送 */

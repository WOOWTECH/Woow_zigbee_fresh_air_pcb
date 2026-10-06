/* 狀態燈（LED = IO8）：依按鍵、學習、Identify、網路狀態決定這一刻亮或滅。純函式，主機可測。
 * 優先順序（高到低）與燈號見 docs/v4-matter-spec.md §6：
 *   按住 ≥10s 恆亮（放開就重置）→ 按住 3s 起快閃（放開進學習）→ 學習中快閃 → Identify 極快閃
 *   → 未配對慢閃 → 已配對但網路斷線「兩短一長」→ 已連線恆亮 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FA_LED_NET_UNPAIRED = 0,   /* 沒有任何 Matter fabric：可配對 */
    FA_LED_NET_OFFLINE,        /* 已配對、Thread（或測試版 Wi-Fi）沒連上 */
    FA_LED_NET_ONLINE,
} fa_led_net_t;

typedef struct {
    uint32_t     held_ms;      /* 按鍵目前按住多久（沒按＝0） */
    bool         learning;     /* 遙控器學習中 */
    bool         identifying;  /* Matter Identify 或 NFC 找裝置 */
    fa_led_net_t net;
} fa_led_in_t;

#define FA_LED_BEACON_PERIOD_MS 2000   /* 兩短一長的週期 */

bool fa_led_level(const fa_led_in_t *in, uint32_t t_ms);

/* 開發板模式（板載 WS2812 RGB 燈）：燈號的亮／滅同上，亮的時候用顏色顯示 K1–K4：
 *   全關＝暗白；K1 紅、K2 綠、K3 藍；K4 開時再加白（顏色變淡）。亮度壓低，避免刺眼。 */
typedef struct { uint8_t r, g, b; } fa_rgb_t;
fa_rgb_t fa_led_rgb(bool level, const bool on[4]);
/* 狀態顯示這一刻要不要點亮 RGB：有繼電器開著、或狀態不是「正常已連線」（閃燈）時才亮；
 * 正常且全關時不亮（開發板彩色燈關掉時要全暗） */
bool fa_led_rgb_status_lit(const fa_led_in_t *in, uint32_t t_ms, const bool on[4]);

/* WS2812 類燈珠送出的位元組順序：標準 WS2812 是 GRB；有些開發板的燈珠是 RGB（紅綠會對調）。 */
typedef enum { FA_LED_ORDER_GRB = 0, FA_LED_ORDER_RGB = 1 } fa_led_order_t;
void fa_led_wire_bytes(fa_rgb_t c, fa_led_order_t order, uint8_t out[3]);

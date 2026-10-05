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

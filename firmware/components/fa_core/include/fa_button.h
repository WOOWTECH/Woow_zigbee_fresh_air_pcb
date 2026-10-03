/* 按鍵（B1 = IO9，按下為低）：去彈跳、依放開時的按住長度分類。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FA_BTN_DEBOUNCE_MS 50
#define FA_BTN_SHORT_MAX   1000   /* 50ms–1s：短按 */
#define FA_BTN_LONG_MIN    3000   /* 3–8s：學習遙控器 */
#define FA_BTN_LONG_MAX    8000
#define FA_BTN_VLONG_MIN   10000  /* ≥10s：恢復出廠（Zigbee 離網＋清遙控器） */

typedef enum { FA_BTN_NONE = 0, FA_BTN_SHORT, FA_BTN_LONG, FA_BTN_VLONG } fa_btn_evt_t;

typedef struct {
    bool     pressed;
    uint32_t since_ms;
} fa_button_t;

void         fa_button_init(fa_button_t *b);
fa_btn_evt_t fa_button_update(fa_button_t *b, bool pressed, uint32_t now_ms);
uint32_t     fa_button_held_ms(const fa_button_t *b, uint32_t now_ms);   /* 未按下為 0 */

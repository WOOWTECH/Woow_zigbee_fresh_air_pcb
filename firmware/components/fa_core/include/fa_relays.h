/* 4 路繼電器邏輯：指撥模式、風速互鎖（先斷後通）、同時吸合上限。純 C，不碰硬體。
 * 呼叫端把回傳的動作清單照 delay_ms 排程到 GPIO。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FA_CH 4
#define FA_ERR_ARG   (-1)
#define FA_ERR_LIMIT (-2)

typedef enum {
    FA_MODE_4CH  = 0,   /* 指撥 OFF/OFF：4 路獨立開關 */
    FA_MODE_FAN3 = 1,   /* OFF/ON：繼電器 1–3 = 三段風速（互斥），4 獨立（閥／加熱） */
    FA_MODE_FAN2 = 2,   /* ON/OFF：繼電器 1–2 = 兩段風速（互斥），3、4 獨立 */
    FA_MODE_SEL4 = 3,   /* ON/ON ：4 路全部互斥（四段選擇） */
} fa_mode_t;

typedef struct {
    uint8_t  ch;
    bool     on;
    uint32_t delay_ms;
} fa_action_t;

typedef struct {
    fa_mode_t mode;
    bool      on[FA_CH];
    uint8_t   max_on;     /* 同時吸合上限（電源預算） */
    uint16_t  dead_ms;    /* 互鎖切換：斷開後多久才吸合新的一路 */
    uint8_t   group;      /* 互鎖群組遮罩（V3.2 起由 DO 模式決定，見 fa_io；V3.1 以前由指撥模式決定） */
} fa_relays_t;

fa_mode_t fa_mode_from_dip(bool bit1, bool bit0);
uint8_t   fa_mode_group_mask(fa_mode_t mode);          /* 互斥群組的通道位元遮罩 */
void      fa_relays_init(fa_relays_t *r, fa_mode_t mode, uint8_t max_on, uint16_t dead_ms);
void      fa_relays_init_group(fa_relays_t *r, uint8_t group_mask, uint8_t max_on, uint16_t dead_ms);
/* 回傳動作數（>=0），或 FA_ERR_ARG / FA_ERR_LIMIT（狀態不變） */
int       fa_relays_set(fa_relays_t *r, uint8_t ch, bool on, fa_action_t *out, int max_out);
int       fa_relays_toggle(fa_relays_t *r, uint8_t ch, fa_action_t *out, int max_out);
int       fa_relays_cycle(fa_relays_t *r, fa_action_t *out, int max_out);   /* 按鍵短按 */
int       fa_relays_all_off(fa_relays_t *r, fa_action_t *out, int max_out);

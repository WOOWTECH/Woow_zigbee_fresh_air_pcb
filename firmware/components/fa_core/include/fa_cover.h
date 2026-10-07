/* 時間式窗簾（比例控制）：K1＝開、K2＝關，用「全關到全開的行程時間」推算位置。純 C，不碰硬體，主機可測。
 *
 * 位置用 Matter Window Covering 的慣例：0＝全開、10000＝全關（百分之一 %）。
 * 規則：
 *   - 換方向前先停 FA_COVER_DEAD_MS（保護交流馬達）；同方向改目標不停車
 *   - 目標是端點（0 或 10000）時，到了之後多跑 overrun（行程 1/10，至少 1 秒），把累積誤差歸零
 *   - 已經在端點又下同方向指令：只跑 overrun（用來校正）
 *   - 時間用 now_ms 參數傳入；位置由「這段開始的時間與位置」算，不逐 tick 累加，沒有捨入漂移 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fa_led.h"

#define FA_COVER_FULL     10000
#define FA_COVER_DEAD_MS  500

typedef enum { FA_COVER_STOP = 0, FA_COVER_OPENING = 1, FA_COVER_CLOSING = 2 } fa_cover_motor_t;

typedef struct {
    uint32_t         travel_ms;
    int32_t          pos, target;
    fa_cover_motor_t motor;        /* 目前輸出給繼電器的方向 */
    fa_cover_motor_t pending;      /* 死區結束後要開始的方向 */
    uint32_t         dead_until;
    uint32_t         seg_ms;       /* 這段開始的時間 */
    int32_t          seg_pos;      /* 這段開始的位置 */
    bool             overrun;
    uint32_t         overrun_until;
    fa_cover_motor_t last_dir;     /* 最後一次真的在跑的方向（判斷要不要死區） */
    uint32_t         stopped_ms;
} fa_cover_t;

void             fa_cover_init(fa_cover_t *c, uint32_t travel_ms, uint16_t pos);
void             fa_cover_set_travel(fa_cover_t *c, uint32_t travel_ms, uint32_t now_ms);
void             fa_cover_goto(fa_cover_t *c, uint16_t target, uint32_t now_ms);
void             fa_cover_stop(fa_cover_t *c, uint32_t now_ms);
/* 每 10ms 左右呼叫：更新位置、到點停車／overrun、死區結束後起步。回傳此刻該輸出的方向 */
fa_cover_motor_t fa_cover_tick(fa_cover_t *c, uint32_t now_ms);
uint16_t         fa_cover_pos(const fa_cover_t *c);
bool             fa_cover_busy(const fa_cover_t *c);    /* 在跑、overrun 或死區等待中 */
/* 按鍵一顆循環：跑著就停；停著時上次是開（或已全開）就關，否則開 */
void             fa_cover_cycle(fa_cover_t *c, uint32_t now_ms);

/* 行程時間下拉選單：5–60 秒每 1 秒、65–120 秒每 5 秒、150、180 秒，共 70 項 */
#define FA_COVER_TRAVEL_PRESETS 70
#define FA_COVER_TRAVEL_DEFAULT_MS 30000
uint32_t fa_cover_travel_preset_ms(uint8_t i);
uint8_t  fa_cover_travel_nearest(uint32_t ms);
void     fa_cover_travel_label(uint8_t i, char out[12]);

/* 開發板 RGB：開中綠、關中紅；停著用白光亮度表示開度（全開最亮、全關最暗但看得到） */
fa_rgb_t fa_cover_rgb(fa_cover_motor_t motor, uint16_t pos, uint8_t max);

/* 網路層介面：app_main 只認這組函式。V3.x 由 fa_zigbee.c 實作，V4 起由 fa_matter.cpp 實作（Matter over Thread）。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fa_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* controller 要求開／關第 ch 路；回傳 false＝拒絕（例如超過同時吸合上限），屬性會被打回實際狀態 */
    bool (*on_set)(uint8_t ch, bool on);
    /* controller 改了進階設定（HA 下拉選單）；回傳 false＝不合法，屬性會被打回目前設定 */
    bool (*on_cfg)(const fa_io_cfg_t *cfg);
    /* controller 要裝置「找自己」（Identify）：seconds＞0 開始快閃 seconds 秒，0＝停止 */
    void (*identify)(uint16_t seconds);
    /* 開發板彩色燈（CONFIG_FA_DEVKIT_LIGHT）：板載 RGB 燈當 Extended Color Light，controller 改了就回呼（已換算成 RGB＋亮度） */
    void (*light)(bool on, uint8_t r, uint8_t g, uint8_t b);
    /* 窗簾（實驗 CONFIG_FA_DEVKIT_COVER_ONLY）：位置 0＝全開…10000＝全關（Matter 慣例）。
     * 在 CHIP 執行緒呼叫：不可在裡面呼叫 fa_net_*（會重複拿 chip 鎖） */
    void (*cover_goto)(uint16_t target);
    uint16_t (*cover_stop)(void);                  /* 回傳停下的位置 */
    void (*cover_travel)(uint32_t travel_ms);      /* HA 下拉選單改了行程時間 */
    void (*cover_reverse)(bool reversed);          /* Window Covering Mode.MotorDirectionReversed（馬達方向反轉：K1／K2 對調） */
} fa_net_cb_t;

void fa_net_start(const fa_net_cb_t *cb, const fa_io_cfg_t *cfg);
bool fa_net_joined(void);           /* 已配對且網路已連線 */
bool fa_net_commissioned(void);     /* 至少加入一個 Matter fabric（不管目前連線與否） */
/* 本地（按鍵／遙控器／DI／互鎖）改變繼電器後同步 4 路 On/Off 狀態 */
void fa_net_sync(const bool on[4]);
/* DI 狀態改變（bit k＝DI(k+1) 接通） */
void fa_net_report_di(uint8_t mask);
/* 進階設定改變（NFC、恢復出廠）後同步到下拉選單 */
void fa_net_report_cfg(const fa_io_cfg_t *cfg);
/* 窗簾：fa_net_start 前給開機時的位置與行程；之後位置／目標變了就回報（目標＝位置表示停著） */
void fa_net_cover_init(uint16_t pos, uint32_t travel_ms);
void fa_net_report_cover(uint16_t pos, uint16_t target);
/* 離開網路並清除網路資料，完成後重開機、重新開放配對 */
void fa_net_factory_reset(void);

#ifdef __cplusplus
}
#endif

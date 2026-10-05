/* V4 Matter Mode Select（HA 下拉選單）↔ fa_io_cfg_t 的對應。純 C，不碰 Matter，主機可測。
 * 每路 3 個選單：DI 模式、DO 模式、點動時間。Mode Select 的 mode 值就是本檔的選項索引（0 起算）。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fa_io.h"

typedef enum { FA_SEL_DI = 0, FA_SEL_DO = 1, FA_SEL_JOG = 2, FA_SEL_KINDS = 3 } fa_sel_kind_t;

#define FA_JOG_PRESETS 11
extern const uint32_t FA_JOG_PRESET_MS[FA_JOG_PRESETS];

uint8_t     fa_sel_count(fa_sel_kind_t kind);                    /* 選項數 */
const char *fa_sel_label(fa_sel_kind_t kind, uint8_t mode);      /* 選項名稱（UTF-8，Mode Select 的 Label 欄位） */
/* 讀：目前設定在選單上要顯示哪一項。點動時間不在級距內時取最接近的級距 */
uint8_t     fa_sel_get(const fa_io_cfg_t *c, uint8_t ch, fa_sel_kind_t kind);
/* 寫：controller 選了某項 → 改設定。mode 超出範圍或 ch 錯誤回傳 false（設定不變） */
bool        fa_sel_set(fa_io_cfg_t *c, uint8_t ch, fa_sel_kind_t kind, uint8_t mode);

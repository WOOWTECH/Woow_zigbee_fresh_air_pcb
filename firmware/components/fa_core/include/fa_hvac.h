/* 新風溫控（冷暖＋三段風速）：決定加熱、製冷、風速輸出。純 C，不碰硬體，主機可測。
 *
 * 輸入用 Matter 的數值慣例（直接對應 Room Air Conditioner 的屬性）：
 *   溫度、設定點：0.01 °C（int16）；溫度未知＝FA_HVAC_TEMP_UNKNOWN
 *   模式：SystemMode 0＝關、3＝製冷、4＝加熱（其他值視為關）
 *   風速：FanMode 0＝關、1＝低、2＝中、3＝高、4＝開（視為高）、5＝自動
 * 規則：
 *   - 溫控緩衝 ±hyst：加熱在 T ≤ 設定－hyst 開、T ≥ 設定＋hyst 關；製冷相反
 *   - 加熱、製冷各自「最少開／關 min_cycle_ms」，避免頻繁開停（壓縮機保護）；
 *     使用者關機或切模式時立刻關（不等最少開），但下一次開仍要等滿最少關
 *   - 加熱關掉後風扇至少低速再吹 purge_ms 散熱（關機也一樣）
 *   - 風速：開機時依 FanMode；自動＝有需求時依溫差（≥2 °C 高、≥1 °C 中、其餘低），沒需求時低速換氣；
 *     自動模式的風速至少維持 FA_HVAC_AUTO_FAN_HOLD_MS 才換（溫度感測器跳動不跟著變）；
 *     FanMode 關但正在加熱／製冷 → 強制低速（電熱與蒸發器一定要有風） */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FA_HVAC_TEMP_UNKNOWN INT16_MIN
#define FA_HVAC_AUTO_FAN_HOLD_MS 30000

enum { FA_HVAC_MODE_OFF = 0, FA_HVAC_MODE_COOL = 3, FA_HVAC_MODE_HEAT = 4 };
enum { FA_FAN_OFF = 0, FA_FAN_LOW = 1, FA_FAN_MED = 2, FA_FAN_HIGH = 3, FA_FAN_ON = 4, FA_FAN_AUTO = 5 };
/* Matter ThermostatRunningState 位元 */
enum { FA_RUN_HEAT = 0x01, FA_RUN_COOL = 0x02, FA_RUN_FAN = 0x04, FA_RUN_FAN2 = 0x20, FA_RUN_FAN3 = 0x40 };

typedef struct {
    bool    power;            /* OnOff（Room AC 的總開關） */
    uint8_t mode;             /* SystemMode */
    int16_t heat_sp, cool_sp; /* OccupiedHeating／CoolingSetpoint */
    uint8_t fan_mode;         /* FanMode */
} fa_hvac_cmd_t;

typedef struct {
    int16_t  hyst;            /* 0.01 °C，例如 50＝0.5 °C */
    uint32_t min_cycle_ms;
    uint32_t purge_ms;
} fa_hvac_cfg_t;

typedef struct {
    bool    heat, cool;
    uint8_t fan;              /* 0＝停、1–3＝低中高 */
    uint8_t running;          /* FA_RUN_* */
} fa_hvac_out_t;

typedef struct {
    fa_hvac_cfg_t cfg;
    bool     heat, cool;
    bool     heat_ever, cool_ever;     /* 開機後還沒切過：第一次開不用等最少關 */
    uint32_t heat_ms, cool_ms;         /* 上次切換時間 */
    bool     purging;
    uint32_t purge_until;
    uint8_t  auto_fan;                 /* 自動模式目前的速度（0＝上一刻不是自動） */
    uint32_t auto_fan_ms;              /* 上次換速時間 */
} fa_hvac_t;

void          fa_hvac_init(fa_hvac_t *h, const fa_hvac_cfg_t *cfg);
fa_hvac_out_t fa_hvac_tick(fa_hvac_t *h, const fa_hvac_cmd_t *cmd, int16_t temp, uint32_t now_ms);

/* 開發板 RGB：加熱紅、製冷藍、只送風綠；亮度＝風速（低 20、中 50、高＝max）；全停暗掉 */
#include "fa_led.h"
fa_rgb_t fa_hvac_rgb(const fa_hvac_out_t *o, uint8_t max);

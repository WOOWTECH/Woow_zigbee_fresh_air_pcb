/* 繼電器線圈「全壓吸合 → PWM 降壓保持」的時序與 12V 電流預算。純 C，不碰硬體。
 *
 * Omron G5Q-1 規格書（J155-E1-16）p.6：降壓保持要先加額定電壓至少 100ms，保持電壓不得低於額定的 30%。
 * 線圈 L/R 時間常數遠大於 20kHz 週期，加上 D1–D4 續流，線圈看到的是平均電壓：
 *   V_coil = D·V − (1−D)·Vf，I_coil = V_coil / R，12V 電源電流 = D·I_coil
 * 60% duty、Schottky Vf 0.3V：V_coil 7.08V（額定 59%），電源端每顆 11.8mA（全壓 33.3mA）。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t pullin_ms;   /* 全壓吸合時間（≥100，Omron 規定） */
    uint8_t  hold_pct;    /* 保持 duty %（100 = 不降壓） */
    uint16_t supply_mv;   /* 線圈電源，12000 */
    uint16_t coil_ohm;    /* G5Q-1 DC12：360 */
    uint16_t diode_mv;    /* 續流二極體 Vf（B5819W 在 20mA 約 300） */
} fa_coil_cfg_t;

/* 通電 ms_since_on 毫秒後該用的 duty %；off 一律 0 */
uint8_t  fa_coil_duty_pct(const fa_coil_cfg_t *c, bool on, uint32_t ms_since_on);
/* 該 duty 下線圈平均電壓（mV）、12V 電源端平均電流（µA） */
uint32_t fa_coil_voltage_mv(const fa_coil_cfg_t *c, uint8_t duty_pct);
uint32_t fa_coil_supply_ua(const fa_coil_cfg_t *c, uint8_t duty_pct);
/* 12V 側最壞情況（µA）：base_ua（Zigbee 收訊等常態）＋ burst_ua（發射峰值）
 * ＋ pulling 顆在吸合（全壓）＋ held 顆在保持 */
uint32_t fa_coil_budget_ua(const fa_coil_cfg_t *c, uint8_t held, uint8_t pulling,
                           uint32_t base_ua, uint32_t burst_ua);

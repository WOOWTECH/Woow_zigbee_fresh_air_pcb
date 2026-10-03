/* WO_30109 V3.0 腳位（hardware/…/WO30109_FreshAir.kicad_sch，scripts/design.py） */
#pragma once
#define PIN_RELAY_1   6    /* U1 pin6  → R5 → Q1 閘極（10k 下拉，開機不吸合） */
#define PIN_RELAY_2   7
#define PIN_RELAY_3   0
#define PIN_RELAY_4   1
#define PIN_MODE_BIT0 10   /* 指撥 S1，ON = 接地 = 1 */
#define PIN_MODE_BIT1 11
#define PIN_BUTTON    9    /* B1，按下 = 低；也是 BOOT strapping 腳 */
#define PIN_LED       2    /* L1 藍燈，高電位亮 */
#define PIN_RF_DATA   23   /* SYN480R DO */

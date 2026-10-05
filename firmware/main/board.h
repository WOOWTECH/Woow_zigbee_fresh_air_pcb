/* WO_30109 V3.2 腳位（hardware/…/WO30109_FreshAir.kicad_sch，scripts/design.py） */
#pragma once
#define PIN_RELAY_1   6    /* U1 pin6  → R5 → Q1 閘極（10k 下拉，開機不吸合） */
#define PIN_RELAY_2   7
#define PIN_RELAY_3   0
#define PIN_RELAY_4   1
#define PIN_DI_1      22   /* V3.2 J5 IN1–IN4 經 U5 TLP290-4：接通＝低電位（內建上拉）。V3.1 以前這裡是指撥 IO10/IO11 */
#define PIN_DI_2      21
#define PIN_DI_3      20
#define PIN_DI_4      19
#define PIN_BUTTON    9    /* B1，按下 = 低；也是 BOOT strapping 腳 */
#define PIN_LED       2    /* L1 藍燈，高電位亮 */
#define PIN_RF_DATA   23   /* SYN480R DO */

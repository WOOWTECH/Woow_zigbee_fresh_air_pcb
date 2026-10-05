/* WO_30109 腳位（hardware/…/WO30109_FreshAir.kicad_sch，scripts/design.py）。板子版本在 menuconfig 選 */
#pragma once
#include "sdkconfig.h"
#define PIN_RELAY_1   6    /* U1 pin6  → R5 → Q1 閘極（10k 下拉，開機不吸合） */
#define PIN_RELAY_2   7
#define PIN_RELAY_3   0
#define PIN_RELAY_4   1
/* J5 IN1–IN4 經 U5 TLP290-4：接通＝低電位（內建上拉）。V3.1 以前沒有 DI（那時 IO10/IO11 是指撥） */
#if CONFIG_FA_BOARD_V32
#define PIN_DI_1      22
#define PIN_DI_2      21
#define PIN_DI_3      20
#define PIN_DI_4      19
#else                      /* V3.3：DI 與 NFC I2C 照左右順序進 U1 上排（佈線不交叉） */
#define PIN_DI_1      3
#define PIN_DI_2      15   /* strapping 腳，但只在燒了 JTAG eFuse 時讀，出廠不受影響 */
#define PIN_DI_3      22
#define PIN_DI_4      21
#define PIN_NFC_SDA   20   /* U6 ST25DV04KC，R24 10k 上拉 */
#define PIN_NFC_SCL   19   /* R25 10k 上拉 */
#endif
#define PIN_BUTTON    9    /* B1，按下 = 低；也是 BOOT strapping 腳 */
#define PIN_LED       2    /* L1 藍燈，高電位亮 */
#define PIN_RF_DATA   23   /* SYN480R DO */

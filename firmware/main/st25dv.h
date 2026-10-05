/* ST25DV04KC（NFC＋I2C 雙介面 EEPROM）I2C 驅動。規格書 DS13519 Rev 4。
 *   裝置位址：0x53（E2=0：使用者記憶體、動態暫存器、mailbox）／0x57（E2=1：系統設定、I2C 密碼）
 *   EEPROM 寫入：一次 1–16 bytes（同一 16-byte 列），寫完約 5ms 不回 ACK → 用 ACK polling 等
 *   mailbox：RAM 0x2008–0x2107，不經 EEPROM；MB_LEN_Dyn＝長度−1 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define ST25DV_REG_IT_STS_DYN  0x2005
#define ST25DV_REG_MB_CTRL_DYN 0x2006
#define ST25DV_REG_MB_LEN_DYN  0x2007
#define ST25DV_MAILBOX         0x2008
#define ST25DV_SYS_FTM         0x000D
#define ST25DV_SYS_UID         0x0018

#define ST25DV_MB_EN           0x01   /* MB_CTRL_Dyn bits */
#define ST25DV_MB_HOST_PUT_MSG 0x02
#define ST25DV_MB_RF_PUT_MSG   0x04

esp_err_t st25dv_init(int sda, int scl);
bool      st25dv_present(void);
esp_err_t st25dv_read(uint16_t addr, void *buf, size_t n);                  /* 使用者記憶體／動態暫存器／mailbox */
esp_err_t st25dv_write_eeprom(uint16_t addr, const void *buf, size_t n);    /* 使用者 EEPROM（自動切 16-byte 列＋等寫完） */
esp_err_t st25dv_write_dyn(uint16_t reg, uint8_t v);
esp_err_t st25dv_mailbox_write(const void *buf, size_t n);                  /* 1–256 bytes */
/* 開啟 mailbox（FTM.MB_MODE=1 存在 EEPROM，要先用 I2C 密碼開 security session；MB_CTRL_Dyn.MB_EN 每次上電都要設） */
esp_err_t st25dv_enable_mailbox(const uint8_t i2c_pwd[8]);
esp_err_t st25dv_read_uid(uint8_t uid[8]);

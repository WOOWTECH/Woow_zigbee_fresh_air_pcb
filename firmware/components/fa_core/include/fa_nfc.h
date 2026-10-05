/* NFC 設定協定（V3.3 起，ST25DV04KC）：App 與 ESP32 在 NFC 晶片 EEPROM／mailbox 裡交換的資料格式。純 C、主機可測。
 * 完整規格（給 App 開發）：docs/nfc-protocol.md；Python 參考實作：tools/nfc_ref.py（兩邊用同一組測試向量）。
 *
 * EEPROM（512B，手機端以 4-byte block 讀寫，I2C 以 byte 位址）：
 *   0x000–0x03F  保留（之後放 NDEF：貼手機就開 App 下載頁）
 *   0x040–0x0DF  STATE   ESP32 寫、App 讀：目前設定＋代數 gen＋韌體版本，CRC16 保護
 *   0x0E0–0x17F  REQUEST App 寫、ESP32 讀：要套用的設定＋所根據的 gen＋HMAC 簽章；處理完 ESP32 在原位寫 ACK
 * 驗證（協定 v2）：每台 128-bit 隨機金鑰 secret（第一次開機產生，印在標籤的 App QR），
 *   key = SHA-256("WO30109-NFC-v2:" + secret)；REQUEST 與需要授權的 mailbox 指令附 HMAC-SHA256 前 16 bytes。
 *   App QR：WONFC:2:<UID 16 位大寫十六進位，MSB 在前>:<secret 32 位大寫十六進位>（全部是 QR 英數模式字元）
 * ST25DV 的 RF 密碼 I2C 端無法設定（規格書 Table 60），所以不用晶片的 RF 寫保護，改由 ESP32 驗簽：
 * 誰都能寫 EEPROM，但只有帶正確簽章、根據最新 gen 的 REQUEST 會被套用（重送舊的 REQUEST 會因 gen 不符被拒）。
 * 所有多位元組欄位都是 little-endian。 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fa_io.h"

#define FA_NFC_ADDR_STATE   0x040
#define FA_NFC_ADDR_REQUEST 0x0E0
#define FA_NFC_AREA_LEN     160
#define FA_NFC_HDR_LEN      12
#define FA_NFC_PAYLOAD_LEN  84
#define FA_NFC_STATE_EXTRA  8          /* fw_version u32 + hw_rev[4] */
#define FA_NFC_TAG_LEN      16
#define FA_NFC_NAME_LEN     24
#define FA_NFC_REMOTES_MAX  8
#define FA_NFC_STATE_LEN    (FA_NFC_HDR_LEN + FA_NFC_PAYLOAD_LEN + FA_NFC_STATE_EXTRA + 2)   /* 106 */
#define FA_NFC_REQUEST_LEN  (FA_NFC_HDR_LEN + FA_NFC_PAYLOAD_LEN + FA_NFC_TAG_LEN)           /* 112 */
#define FA_NFC_ACK_LEN      (FA_NFC_HDR_LEN + 4)                                             /* 16 */

enum { FA_NFC_T_STATE = 1, FA_NFC_T_REQUEST = 2, FA_NFC_T_ACK = 3 };
enum {
    FA_NFC_OK = 0,
    FA_NFC_ERR_AUTH = 1,      /* 簽章不對（PIN 錯或資料被改） */
    FA_NFC_ERR_STALE = 2,     /* 根據的 gen 不是目前的（設定在這之間被別處改過，或是重送舊的請求） */
    FA_NFC_ERR_INVALID = 3,   /* 欄位值不合法（模式超出範圍、點動時間、名稱沒有結尾 0、要加入沒學過的遙控器） */
    FA_NFC_ERR_FORMAT = 4,    /* magic／type／版本／長度不對，或 CRC 錯 */
    FA_NFC_ERR_CMD = 5,       /* mailbox：未知指令 */
};

typedef struct {
    fa_io_cfg_t io;
    char        name[FA_NFC_NAME_LEN];         /* UTF-8，以 0 結尾（最多 23 bytes） */
    uint8_t     n_remotes;
    uint32_t    remotes[FA_NFC_REMOTES_MAX];   /* EV1527 20 位元位址 */
} fa_nfc_cfg_t;

#define FA_NFC_VERSION     2
#define FA_NFC_SECRET_LEN  16
#define FA_NFC_APP_QR_LEN  (8 + 16 + 1 + 32)                  /* "WONFC:2:" + UID + ":" + secret = 57 */

void   fa_nfc_key_from_secret(const uint8_t secret[FA_NFC_SECRET_LEN], uint8_t key[32]);
/* 產生標籤上的 App QR 字串（以 0 結尾），回傳長度。uid 照 ST25DV 讀出的順序（LSB 在前，uid[7]＝0xE0） */
size_t fa_nfc_app_qr(const uint8_t uid[8], const uint8_t secret[FA_NFC_SECRET_LEN], char out[FA_NFC_APP_QR_LEN + 1]);
uint16_t fa_nfc_crc16(const uint8_t *p, size_t n);          /* CRC-16/CCITT-FALSE（0x1021，初值 0xFFFF） */

/* ESP32 端 */
size_t fa_nfc_encode_state(uint8_t out[FA_NFC_AREA_LEN], const fa_nfc_cfg_t *c, uint32_t gen, uint32_t fw_version,
                           const char hw_rev[4]);
bool   fa_nfc_is_request(const uint8_t *hdr, size_t n);    /* 前 12 bytes 是不是 REQUEST（決定要不要讀完整筆） */
/* 驗證 REQUEST；成功時把新設定寫到 *out（cur 是目前設定，用來檢查遙控器只能刪不能加） */
int    fa_nfc_check_request(const uint8_t *in, size_t n, const uint8_t key[32], uint32_t cur_gen,
                            const fa_nfc_cfg_t *cur, fa_nfc_cfg_t *out);
size_t fa_nfc_encode_ack(uint8_t out[FA_NFC_ACK_LEN], uint32_t based_on_gen, uint8_t status);

/* App 端（參考實作，主機測試與 tools/nfc_ref.py 對照用） */
int    fa_nfc_decode_state(const uint8_t *in, size_t n, fa_nfc_cfg_t *c, uint32_t *gen, uint32_t *fw_version);
size_t fa_nfc_encode_request(uint8_t out[FA_NFC_AREA_LEN], const fa_nfc_cfg_t *c, uint32_t based_on_gen,
                             const uint8_t key[32]);

/* ---------------- mailbox（板子有電時的即時指令）----------------
 * App → 板子：'M' cmd seq len payload[len] [tag16]   需授權的指令 tag = HMAC(key, 'M'..payload ‖ challenge u32)
 * 板子 → App：'R' cmd seq status len payload[len]
 * challenge 由 GET_STATUS 回傳（每次 GET_STATUS 換新），授權指令用過一次就作廢 → 錄下來重送無效 */
enum {
    FA_NFC_MB_GET_STATUS = 0x01,     /* 不需授權 */
    FA_NFC_MB_IDENTIFY = 0x02,       /* 不需授權：LED 快閃 5 秒，找是哪一台 */
    FA_NFC_MB_LEARN_REMOTE = 0x10,   /* 需授權：進入遙控器學習 20 秒 */
    FA_NFC_MB_FACTORY_RESET = 0x7F,  /* 需授權 */
};
#define FA_NFC_MB_MAX 256

typedef struct {
    uint8_t cmd, seq, len;
    const uint8_t *payload;
} fa_nfc_mb_req_t;

typedef struct {                 /* GET_STATUS 回應內容（20 bytes） */
    uint32_t gen;
    uint8_t  relays;             /* bit k＝K(k+1) 吸合 */
    uint8_t  di;                 /* bit k＝DI(k+1) 接通（接線或遙控器） */
    uint8_t  net;                /* 0 未入網、1 已入網 */
    uint8_t  flags;              /* bit0 遙控器學習中 */
    uint32_t fw_version;
    uint32_t uptime_s;
    uint32_t challenge;
} fa_nfc_status_t;
#define FA_NFC_STATUS_LEN 20

bool   fa_nfc_mb_needs_auth(uint8_t cmd);
/* 解析並驗證 App 的 mailbox 訊息；challenge＝0 表示目前沒有有效 challenge（授權指令一律 AUTH 失敗） */
int    fa_nfc_mb_parse(const uint8_t *in, size_t n, const uint8_t key[32], uint32_t challenge, fa_nfc_mb_req_t *req);
size_t fa_nfc_mb_resp(uint8_t *out, size_t cap, uint8_t cmd, uint8_t seq, uint8_t status, const uint8_t *payload,
                      uint8_t len);
size_t fa_nfc_encode_status(uint8_t out[FA_NFC_STATUS_LEN], const fa_nfc_status_t *s);
/* App 端參考：組 mailbox 請求 */
size_t fa_nfc_mb_encode_request(uint8_t *out, size_t cap, uint8_t cmd, uint8_t seq, const uint8_t *payload,
                                uint8_t len, const uint8_t key[32], uint32_t challenge);

/* NFC 設定介面的 ESP32 端（V3.3）：輪詢 ST25DV，驗證並套用 App 寫入的 REQUEST、回答 mailbox 指令、
 * 把目前設定寫到 STATE。協定格式在 fa_core/fa_nfc（純 C），晶片存取在 st25dv.c。 */
#pragma once
#include "fa_nfc.h"

typedef struct {
    void (*get_cfg)(fa_nfc_cfg_t *c);              /* 目前設定（DI／DO、名稱、遙控器） */
    void (*apply_cfg)(const fa_nfc_cfg_t *c);      /* 套用並存 NVS（已驗證過） */
    void (*get_status)(fa_nfc_status_t *s);        /* 填 relays、di、net、flags（其餘欄位由這裡填） */
    void (*learn_remote)(void);
    void (*identify)(void);
    void (*factory_reset)(void);
} fa_nfc_port_cb_t;

/* 偵測不到晶片（V3.2 以前的板子）就什麼都不做 */
void fa_nfc_port_start(int sda, int scl, const fa_nfc_port_cb_t *cb);
/* 設定在別處被改了（按鍵、遙控器學習、之後的 Matter／HA）：gen＋1、重寫 STATE，App 手上的舊 gen 請求就會被拒 */
void fa_nfc_port_publish(void);

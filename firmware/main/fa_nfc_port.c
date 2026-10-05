#include "fa_nfc_port.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "bootloader_random.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "st25dv.h"

static const char *TAG = "nfc";
#define FW_VERSION 0x00040000                      /* 4.0.0 */
static const char HW_REV[4] = "3.4";

static fa_nfc_port_cb_t s_cb;
static uint8_t           s_key[32];
static uint32_t          s_gen;
static uint32_t          s_challenge;              /* 0＝沒有有效 challenge */
static SemaphoreHandle_t s_mx;
static bool              s_running;

static void nvs_u32(const char *k, uint32_t *v, bool save)
{
    nvs_handle_t h;
    if (nvs_open("fa", save ? NVS_READWRITE : NVS_READONLY, &h) != ESP_OK) return;
    if (save) { nvs_set_u32(h, k, *v); nvs_commit(h); }
    else nvs_get_u32(h, k, v);
    nvs_close(h);
}

/* 協定 v2：每台一組 128-bit 隨機金鑰，第一次開機產生、存 NVS "nfc_secret"（恢復出廠也保留，標籤上的 App QR 才會一直有效）。
 * 產生時另開 bootloader 熵源：第一次開機時射頻（Thread／Zigbee）還沒啟動，esp_random 只有在射頻或熵源開著時才是真亂數。
 * 開機 log 印出 App QR 字串，產線燒錄後讀出來印在標籤上（docs/nfc-protocol.md §5）。 */
static void load_secret(uint8_t secret[FA_NFC_SECRET_LEN])
{
    nvs_handle_t h;
    size_t len = FA_NFC_SECRET_LEN;
    if (nvs_open("fa", NVS_READWRITE, &h) != ESP_OK) { memset(secret, 0, FA_NFC_SECRET_LEN); return; }
    if (nvs_get_blob(h, "nfc_secret", secret, &len) != ESP_OK || len != FA_NFC_SECRET_LEN) {
        bootloader_random_enable();
        esp_fill_random(secret, FA_NFC_SECRET_LEN);
        bootloader_random_disable();
        nvs_set_blob(h, "nfc_secret", secret, FA_NFC_SECRET_LEN);
        nvs_commit(h);
        ESP_LOGW(TAG, "第一次開機：已產生 NFC 金鑰");
    }
    nvs_close(h);
}

static void write_state(void)
{
    fa_nfc_cfg_t c;
    uint8_t buf[FA_NFC_AREA_LEN];
    s_cb.get_cfg(&c);
    size_t n = fa_nfc_encode_state(buf, &c, s_gen, FW_VERSION, HW_REV);
    if (st25dv_write_eeprom(FA_NFC_ADDR_STATE, buf, n) != ESP_OK) ESP_LOGW(TAG, "寫 STATE 失敗");
}

void fa_nfc_port_publish(void)
{
    if (!s_running) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    s_gen++;
    nvs_u32("nfc_gen", &s_gen, true);
    write_state();
    xSemaphoreGive(s_mx);
}

static void handle_request(void)
{
    uint8_t buf[FA_NFC_REQUEST_LEN];
    if (st25dv_read(FA_NFC_ADDR_REQUEST, buf, FA_NFC_HDR_LEN) != ESP_OK || !fa_nfc_is_request(buf, FA_NFC_HDR_LEN)) return;
    /* App 依規格最後才寫第 0 個 block（magic／type）：看到 REQUEST 表示整筆已寫完 */
    if (st25dv_read(FA_NFC_ADDR_REQUEST, buf, sizeof buf) != ESP_OK) return;
    fa_nfc_cfg_t cur, want;
    uint32_t based_on = (uint32_t)buf[4] | (uint32_t)buf[5] << 8 | (uint32_t)buf[6] << 16 | (uint32_t)buf[7] << 24;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    s_cb.get_cfg(&cur);
    int st = fa_nfc_check_request(buf, sizeof buf, s_key, s_gen, &cur, &want);
    if (st == FA_NFC_OK) {
        s_cb.apply_cfg(&want);
        s_gen++;
        nvs_u32("nfc_gen", &s_gen, true);
        write_state();
    }
    uint8_t ack[FA_NFC_ACK_LEN];
    fa_nfc_encode_ack(ack, based_on, (uint8_t)st);
    st25dv_write_eeprom(FA_NFC_ADDR_REQUEST, ack, sizeof ack);    /* 覆蓋 header：同一筆不會再處理第二次 */
    xSemaphoreGive(s_mx);
    static const char *ST[] = {"OK", "簽章錯（金鑰不對）", "gen 不符（舊請求）", "欄位不合法", "格式錯"};
    ESP_LOGI(TAG, "REQUEST（根據 gen %lu）：%s", (unsigned long)based_on, st < 5 ? ST[st] : "?");
}

static void handle_mailbox(void)
{
    uint8_t ctrl = 0;
    if (st25dv_read(ST25DV_REG_MB_CTRL_DYN, &ctrl, 1) != ESP_OK || !(ctrl & ST25DV_MB_RF_PUT_MSG)) return;
    uint8_t len = 0, in[FA_NFC_MB_MAX], out[64], pl[FA_NFC_STATUS_LEN];
    if (st25dv_read(ST25DV_REG_MB_LEN_DYN, &len, 1) != ESP_OK) return;
    size_t n = (size_t)len + 1;
    if (st25dv_read(ST25DV_MAILBOX, in, n) != ESP_OK) return;     /* 讀完整則：mailbox 變空，可以回覆 */
    fa_nfc_mb_req_t r = {0};
    xSemaphoreTake(s_mx, portMAX_DELAY);
    int st = fa_nfc_mb_parse(in, n, s_key, s_challenge, &r);
    uint8_t plen = 0;
    if (fa_nfc_mb_needs_auth(r.cmd)) s_challenge = 0;            /* 授權指令不論成敗都讓 challenge 作廢 */
    if (st == FA_NFC_OK) {
        switch (r.cmd) {
        case FA_NFC_MB_GET_STATUS: {
            fa_nfc_status_t s = {0};
            s_cb.get_status(&s);
            do { s_challenge = esp_random(); } while (s_challenge == 0);
            s.gen = s_gen; s.fw_version = FW_VERSION; s.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
            s.challenge = s_challenge;
            plen = (uint8_t)fa_nfc_encode_status(pl, &s);
            break;
        }
        case FA_NFC_MB_IDENTIFY: s_cb.identify(); break;
        case FA_NFC_MB_LEARN_REMOTE: s_cb.learn_remote(); break;
        case FA_NFC_MB_FACTORY_RESET: break;                       /* 先回覆，再重置（重置會重開機） */
        }
    }
    xSemaphoreGive(s_mx);
    size_t m = fa_nfc_mb_resp(out, sizeof out, r.cmd, r.seq, (uint8_t)st, pl, plen);
    if (m) st25dv_mailbox_write(out, m);
    ESP_LOGI(TAG, "mailbox 指令 0x%02x：%d", r.cmd, st);
    if (st == FA_NFC_OK && r.cmd == FA_NFC_MB_FACTORY_RESET) {
        vTaskDelay(pdMS_TO_TICKS(500));                            /* 讓手機有時間讀回覆 */
        s_cb.factory_reset();
    }
}

static void nfc_task(void *arg)
{
    for (;;) {
        handle_request();
        handle_mailbox();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void fa_nfc_port_start(int sda, int scl, const fa_nfc_port_cb_t *cb)
{
    if (st25dv_init(sda, scl) != ESP_OK) return;
    s_cb = *cb;
    s_mx = xSemaphoreCreateMutex();
    uint8_t secret[FA_NFC_SECRET_LEN];
    load_secret(secret);
    fa_nfc_key_from_secret(secret, s_key);
    nvs_u32("nfc_gen", &s_gen, false);
    static const uint8_t factory_i2c_pwd[8] = {0};                 /* 出廠 I2C 密碼；只用來開 mailbox，不靠它保護資料 */
    if (st25dv_enable_mailbox(factory_i2c_pwd) != ESP_OK) ESP_LOGW(TAG, "mailbox 開不起來，只能用 EEPROM 設定");
    uint8_t uid[8] = {0};
    st25dv_read_uid(uid);
    char qr[FA_NFC_APP_QR_LEN + 1];
    fa_nfc_app_qr(uid, secret, qr);
    memset(secret, 0, sizeof secret);
    ESP_LOGI(TAG, "NFC 就緒（協定 v%d），gen %lu；App QR：%s", FA_NFC_VERSION, (unsigned long)s_gen, qr);
    s_running = true;
    write_state();                                                 /* 開機時 STATE 以 NVS 為準（不管 App 寫過什麼） */
    xTaskCreate(nfc_task, "nfc", 4096, NULL, 2, NULL);
}

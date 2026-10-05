#include "st25dv.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st25dv";
#define ADDR_USER 0x53
#define ADDR_SYS  0x57
#define I2C_HZ    400000          /* 10k 上拉、走線約 5cm：上升時間遠小於 Fast-mode 的 300ns 上限 */
#define TIMEOUT   50

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_user, s_sys;
static bool s_ok;

esp_err_t st25dv_init(int sda, int scl)
{
    i2c_master_bus_config_t bc = {.i2c_port = -1, .sda_io_num = sda, .scl_io_num = scl, .clk_source = I2C_CLK_SRC_DEFAULT,
                                  .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = false};   /* 板上 R24/R25 10k */
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bc, &s_bus), TAG, "bus");
    i2c_device_config_t u = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADDR_USER, .scl_speed_hz = I2C_HZ};
    i2c_device_config_t y = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADDR_SYS, .scl_speed_hz = I2C_HZ};
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &u, &s_user), TAG, "user dev");
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &y, &s_sys), TAG, "sys dev");
    s_ok = i2c_master_probe(s_bus, ADDR_USER, TIMEOUT) == ESP_OK;
    if (!s_ok) ESP_LOGW(TAG, "沒有回應（V3.2 以前的板子沒有 NFC）");
    return s_ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool st25dv_present(void) { return s_ok; }

static esp_err_t rd(i2c_master_dev_handle_t d, uint16_t addr, void *buf, size_t n)
{
    uint8_t a[2] = {addr >> 8, addr & 0xFF};
    return i2c_master_transmit_receive(d, a, 2, buf, n, TIMEOUT);
}

/* 寫入後晶片在 tW（約 5ms）內不回 ACK：探測到 ACK 才算寫完 */
static esp_err_t wait_ready(uint16_t dev_addr)
{
    for (int i = 0; i < 30; i++) {
        if (i2c_master_probe(s_bus, dev_addr, 5) == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t wr(i2c_master_dev_handle_t d, uint16_t dev_addr, uint16_t addr, const void *buf, size_t n, bool eeprom)
{
    uint8_t tx[2 + 256];
    if (n > 256) return ESP_ERR_INVALID_SIZE;
    tx[0] = addr >> 8; tx[1] = addr & 0xFF;
    memcpy(tx + 2, buf, n);
    esp_err_t e = i2c_master_transmit(d, tx, 2 + n, TIMEOUT);
    if (e == ESP_OK && eeprom) e = wait_ready(dev_addr);
    return e;
}

esp_err_t st25dv_read(uint16_t addr, void *buf, size_t n) { return rd(s_user, addr, buf, n); }

esp_err_t st25dv_write_eeprom(uint16_t addr, const void *buf, size_t n)
{
    const uint8_t *p = buf;
    while (n) {
        size_t chunk = 16 - (addr & 15);                 /* 一次最多一列 16 bytes，不跨列 */
        if (chunk > n) chunk = n;
        ESP_RETURN_ON_ERROR(wr(s_user, ADDR_USER, addr, p, chunk, true), TAG, "write 0x%03x", addr);
        addr += chunk; p += chunk; n -= chunk;
    }
    return ESP_OK;
}

esp_err_t st25dv_write_dyn(uint16_t reg, uint8_t v) { return wr(s_user, ADDR_USER, reg, &v, 1, false); }

esp_err_t st25dv_mailbox_write(const void *buf, size_t n)
{
    if (n == 0 || n > 256) return ESP_ERR_INVALID_SIZE;
    return wr(s_user, ADDR_USER, ST25DV_MAILBOX, buf, n, false);
}

static esp_err_t present_i2c_password(const uint8_t pwd[8])
{
    uint8_t tx[2 + 17] = {0x09, 0x00};                   /* 位址 0900h：密碼 8 bytes＋驗證碼 09h＋再送一次密碼 */
    memcpy(tx + 2, pwd, 8);
    tx[10] = 0x09;
    memcpy(tx + 11, pwd, 8);
    return i2c_master_transmit(s_sys, tx, sizeof tx, TIMEOUT);
}

esp_err_t st25dv_enable_mailbox(const uint8_t i2c_pwd[8])
{
    uint8_t ftm = 0;
    ESP_RETURN_ON_ERROR(rd(s_sys, ST25DV_SYS_FTM, &ftm, 1), TAG, "read FTM");
    if (!(ftm & 0x01)) {                                 /* MB_MODE 存在 EEPROM：只有第一次上電要寫 */
        ESP_RETURN_ON_ERROR(present_i2c_password(i2c_pwd), TAG, "present pwd");
        uint8_t sso = 0;
        rd(s_user, 0x2004, &sso, 1);                     /* I2C_SSO_Dyn */
        if (!(sso & 1)) { ESP_LOGE(TAG, "I2C 密碼不對，無法開啟 mailbox"); return ESP_ERR_INVALID_STATE; }
        uint8_t v = 0x01;                                /* MB_MODE=1、MB_WDG=0（不逾時） */
        ESP_RETURN_ON_ERROR(wr(s_sys, ADDR_SYS, ST25DV_SYS_FTM, &v, 1, true), TAG, "write FTM");
        uint8_t wrong[8] = {0xFF};                       /* 送錯的密碼＝關閉 security session */
        present_i2c_password(wrong);
        ESP_LOGI(TAG, "已開啟 FTM（mailbox）");
    }
    return st25dv_write_dyn(ST25DV_REG_MB_CTRL_DYN, ST25DV_MB_EN);
}

esp_err_t st25dv_read_uid(uint8_t uid[8]) { return rd(s_sys, ST25DV_SYS_UID, uid, 8); }

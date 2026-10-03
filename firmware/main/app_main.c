/* WO_30109 新風控制器 V3.0 主程式：硬體（GPIO／中斷／NVS）接到 fa_core 純邏輯與 Zigbee。
 *
 * 執行緒
 *   relay_task   依動作清單切繼電器（先斷後通、吸合錯開），完成後同步 Zigbee 屬性
 *   rf_task      SYN480R DO 腳邊緣中斷 → 脈寬 → EV1527 解碼 → 學習／控制
 *   ui_task      每 10ms 掃按鍵、更新燈號
 *   zigbee       見 fa_zigbee.c
 */
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board.h"
#include "fa_button.h"
#include "fa_ev1527.h"
#include "fa_relays.h"
#include "fa_remotes.h"
#include "fa_zigbee.h"

static const char *TAG = "fa";

static const gpio_num_t RELAY_PIN[FA_CH] = {PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4};

static fa_relays_t       s_relays;
static fa_remotes_t      s_remotes;
static SemaphoreHandle_t s_lock;           /* 保護 s_relays / s_remotes */
static QueueHandle_t     s_relay_q;        /* fa_action_t；ch = 0xFF 表示「這批結束，請同步」 */
static QueueHandle_t     s_rf_q;           /* rf_edge_t */
static volatile int64_t  s_learn_until_us; /* 學習模式到期時間；0 = 關 */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---------------- 繼電器 ---------------- */
#define SYNC_MARK 0xFF

/* n > 0：執行動作後同步；n < 0（被拒絕）：只同步，把 Zigbee 屬性打回實際狀態；
 * n == 0（狀態沒變）：什麼都不送——否則本地同步屬性若又觸發寫入回呼，會無限循環 */
static void post_actions(const fa_action_t *a, int n)
{
    if (n == 0) return;
    for (int i = 0; i < n; i++) xQueueSend(s_relay_q, &a[i], portMAX_DELAY);
    fa_action_t mark = {.ch = SYNC_MARK};
    xQueueSend(s_relay_q, &mark, portMAX_DELAY);
}

/* 對 s_relays 做一次無參數操作（循環、全關）並送出動作 */
static int apply(int (*op)(fa_relays_t *r, fa_action_t *out, int max))
{
    fa_action_t a[8];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = op(&s_relays, a, 8);
    xSemaphoreGive(s_lock);
    post_actions(a, n);
    return n;
}

static int relay_set(uint8_t ch, bool on)
{
    fa_action_t a[8];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = fa_relays_set(&s_relays, ch, on, a, 8);
    xSemaphoreGive(s_lock);
    post_actions(a, n);
    if (n == FA_ERR_LIMIT) ESP_LOGW(TAG, "繼電器 %d：已達同時吸合上限 %d", ch + 1, CONFIG_FA_MAX_RELAYS_ON);
    return n;
}

static int relay_toggle(uint8_t ch)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool on = !s_relays.on[ch];
    xSemaphoreGive(s_lock);
    return relay_set(ch, on);
}

static void relay_task(void *arg)
{
    int64_t last_on_us = 0;
    fa_action_t a;
    for (;;) {
        xQueueReceive(s_relay_q, &a, portMAX_DELAY);
        if (a.ch == SYNC_MARK) {
            bool on[FA_CH];
            xSemaphoreTake(s_lock, portMAX_DELAY);
            memcpy(on, s_relays.on, sizeof on);
            xSemaphoreGive(s_lock);
            fa_zigbee_sync(on);
            continue;
        }
        if (a.delay_ms) vTaskDelay(pdMS_TO_TICKS(a.delay_ms));
        if (a.on) {                            /* 兩顆線圈吸合錯開，避免 12V 電源同時吃兩個湧浪 */
            int64_t wait_us = last_on_us + CONFIG_FA_RELAY_STAGGER_MS * 1000LL - esp_timer_get_time();
            if (wait_us > 0) vTaskDelay(pdMS_TO_TICKS(wait_us / 1000 + 1));
            last_on_us = esp_timer_get_time();
        }
        gpio_set_level(RELAY_PIN[a.ch], a.on);
        ESP_LOGI(TAG, "繼電器 %d %s", a.ch + 1, a.on ? "ON" : "OFF");
    }
}

static bool zb_on_set(uint8_t ch, bool on)
{
    int n = relay_set(ch, on);
    return n >= 0;
}

/* ---------------- 遙控器學習表（NVS） ---------------- */
static void remotes_load(void)
{
    nvs_handle_t h;
    size_t len = sizeof s_remotes;
    fa_remotes_init(&s_remotes);
    if (nvs_open("fa", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "remotes", &s_remotes, &len) != ESP_OK || len != sizeof s_remotes || s_remotes.n > FA_REMOTES_MAX)
            fa_remotes_init(&s_remotes);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "已學習遙控器 %d 支", s_remotes.n);
}

static void remotes_save(void)
{
    nvs_handle_t h;
    if (nvs_open("fa", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "remotes", &s_remotes, sizeof s_remotes);
    nvs_commit(h);
    nvs_close(h);
}

/* ---------------- 433MHz：邊緣中斷 → 解碼 ---------------- */
typedef struct {
    uint32_t dur_us;
    uint8_t  level;     /* 這段持續的電位（邊緣之前） */
} rf_edge_t;

static void IRAM_ATTR rf_isr(void *arg)
{
    static int64_t last;
    int64_t t = esp_timer_get_time();
    rf_edge_t e = {.dur_us = (uint32_t)(t - last), .level = !gpio_get_level(PIN_RF_DATA)};
    last = t;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_rf_q, &e, &woken);   /* 佇列滿就丟（只會是雜訊暴量） */
    if (woken) portYIELD_FROM_ISR();
}

static void rf_task(void *arg)
{
    fa_ev1527_t dec;
    fa_ev1527_init(&dec);
    uint32_t last_code = 0, last_ms = 0;
    rf_edge_t e;
    for (;;) {
        xQueueReceive(s_rf_q, &e, portMAX_DELAY);
        uint32_t code;
        if (!fa_ev1527_feed(&dec, e.level, e.dur_us, &code)) continue;
        uint32_t t = now_ms();
        if (code == last_code && t - last_ms < 600) { last_ms = t; continue; }   /* 同一次按壓的重送 */
        last_code = code;
        last_ms = t;
        uint32_t addr = fa_ev1527_addr(code);
        uint8_t  key = fa_ev1527_key(code);
        ESP_LOGI(TAG, "433：位址 0x%05lx 鍵 0x%x", (unsigned long)addr, key);

        if (s_learn_until_us && esp_timer_get_time() < s_learn_until_us) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            fa_remotes_add(&s_remotes, addr);
            xSemaphoreGive(s_lock);
            remotes_save();
            s_learn_until_us = 0;
            ESP_LOGI(TAG, "學習完成：0x%05lx（共 %d 支）", (unsigned long)addr, s_remotes.n);
            continue;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool known = fa_remotes_has(&s_remotes, addr);
        xSemaphoreGive(s_lock);
        int ch = fa_remote_key_to_channel(key);
        if (known && ch >= 0) relay_toggle((uint8_t)ch);
    }
}

/* ---------------- 按鍵、燈號 ---------------- */
static void factory_reset(void)
{
    ESP_LOGW(TAG, "恢復出廠：全部繼電器關、清除遙控器、Zigbee 離網");
    apply(fa_relays_all_off);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    fa_remotes_init(&s_remotes);
    xSemaphoreGive(s_lock);
    remotes_save();
    vTaskDelay(pdMS_TO_TICKS(300));
    fa_zigbee_factory_reset();
}

static bool led_pattern(uint32_t t, uint32_t held)
{
    if (held >= FA_BTN_VLONG_MIN) return true;                             /* 按住 ≥10s：恆亮＝放開就重置 */
    if (held >= FA_BTN_LONG_MIN) return (t / 100) % 2;                     /* 3–8s：快閃＝放開進學習 */
    if (s_learn_until_us && esp_timer_get_time() < s_learn_until_us) return (t / 100) % 2;
    if (!fa_zigbee_joined()) return (t / 500) % 2;                         /* 尚未入網：慢閃 */
    return true;                                                           /* 已入網：恆亮 */
}

static void ui_task(void *arg)
{
    fa_button_t btn;
    fa_button_init(&btn);
    for (;;) {
        uint32_t t = now_ms();
        switch (fa_button_update(&btn, gpio_get_level(PIN_BUTTON) == 0, t)) {
        case FA_BTN_SHORT:
            apply(fa_relays_cycle);
            break;
        case FA_BTN_LONG:
            s_learn_until_us = esp_timer_get_time() + 20 * 1000000LL;
            ESP_LOGI(TAG, "遙控器學習模式 20 秒：請按遙控器任一鍵");
            break;
        case FA_BTN_VLONG:
            factory_reset();
            break;
        default:
            break;
        }
        gpio_set_level(PIN_LED, led_pattern(t, fa_button_held_ms(&btn, t)));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ---------------- 啟動 ---------------- */
static void gpio_setup(void)
{
    gpio_config_t out = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = (1ULL << PIN_LED)};
    for (int i = 0; i < FA_CH; i++) out.pin_bit_mask |= 1ULL << RELAY_PIN[i];
    for (int i = 0; i < FA_CH; i++) gpio_set_level(RELAY_PIN[i], 0);
    gpio_config(&out);
    gpio_config_t in = {.mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
                        .pin_bit_mask = (1ULL << PIN_BUTTON) | (1ULL << PIN_MODE_BIT0) | (1ULL << PIN_MODE_BIT1)};
    gpio_config(&in);
    gpio_config_t rf = {.mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_ANYEDGE, .pin_bit_mask = 1ULL << PIN_RF_DATA};
    gpio_config(&rf);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    gpio_setup();
    s_lock = xSemaphoreCreateMutex();
    s_relay_q = xQueueCreate(32, sizeof(fa_action_t));
    s_rf_q = xQueueCreate(256, sizeof(rf_edge_t));

    fa_mode_t mode = fa_mode_from_dip(gpio_get_level(PIN_MODE_BIT1) == 0, gpio_get_level(PIN_MODE_BIT0) == 0);
    fa_relays_init(&s_relays, mode, CONFIG_FA_MAX_RELAYS_ON, CONFIG_FA_INTERLOCK_DEAD_MS);
    static const char *MODE_NAME[] = {"4 路獨立", "三段風速＋1 路", "兩段風速＋2 路", "4 路互斥"};
    ESP_LOGI(TAG, "模式 %d：%s；同時吸合上限 %d；Zigbee 發射 %ddBm", mode, MODE_NAME[mode],
             CONFIG_FA_MAX_RELAYS_ON, CONFIG_FA_ZB_TX_POWER);
    remotes_load();

    xTaskCreate(relay_task, "relay", 3072, NULL, 6, NULL);
    xTaskCreate(rf_task, "rf", 3072, NULL, 4, NULL);
    xTaskCreate(ui_task, "ui", 3072, NULL, 3, NULL);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_RF_DATA, rf_isr, NULL);
    fa_zigbee_start(zb_on_set);
}

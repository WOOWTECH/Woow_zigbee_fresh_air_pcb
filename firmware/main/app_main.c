/* WO_30109 新風控制器 V4 主程式：硬體（GPIO／中斷／NVS）接到 fa_core 純邏輯與網路層（Matter）。
 *
 * 執行緒
 *   relay_task   依動作清單切繼電器（先斷後通、吸合錯開），完成後同步網路層屬性
 *   rf_task      SYN480R DO 腳邊緣中斷 → 脈寬 → EV1527 解碼 → 學習／控制
 *   ui_task      每 10ms 掃按鍵、更新燈號
 *   網路層       fa_net.h → fa_matter.cpp（V3.x 的 Zigbee 版在標籤 v3.4）
 */
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
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
#include "fa_coil.h"
#include "fa_ev1527.h"
#include "fa_io.h"
#include "fa_led.h"
#include "fa_nfc_port.h"
#include "fa_relays.h"
#include "fa_remotes.h"
#include "fa_net.h"
#if CONFIG_FA_DEVKIT_RGB
#include "ws2812.h"
#endif

static const char *TAG = "fa";

static const gpio_num_t RELAY_PIN[FA_CH] = {PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4};
static const gpio_num_t DI_PIN[FA_CH] = {PIN_DI_1, PIN_DI_2, PIN_DI_3, PIN_DI_4};

static fa_relays_t       s_relays;
static fa_remotes_t      s_remotes;
static fa_io_t           s_io;             /* DI／DO 模式與狀態（V3.2） */
static SemaphoreHandle_t s_lock;           /* 保護 s_relays / s_remotes / s_io */
static QueueHandle_t     s_relay_q;        /* fa_action_t；ch = 0xFF 表示「這批結束，請同步」 */
static QueueHandle_t     s_rf_q;           /* rf_edge_t */
static volatile int64_t  s_learn_until_us; /* 學習模式到期時間；0 = 關 */
static volatile int64_t  s_identify_until_us; /* NFC「找裝置」：LED 快閃到期時間 */
static char              s_name[FA_NFC_NAME_LEN] = "WO30109";   /* 裝置名稱（NFC App 設定，存 NVS） */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---------------- 繼電器 ---------------- */
#define SYNC_MARK 0xFF

/* n > 0：執行動作後同步；n < 0（被拒絕）：只同步，把網路層屬性打回實際狀態；
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

/* ---------------- DI／DO（V3.2）：接線 DI 與遙控器 4 鍵 ---------------- */
static void io_post(int n, uint8_t ch)
{
    if (n == FA_ERR_LIMIT) ESP_LOGW(TAG, "DI%d：已達同時吸合上限 %d", ch + 1, CONFIG_FA_MAX_RELAYS_ON);
}

static void io_cfg_default(fa_io_cfg_t *c)
{
#if CONFIG_FA_PRESET_FAN2
    fa_io_default(c, FA_MODE_FAN2);
#elif CONFIG_FA_PRESET_4CH
    fa_io_default(c, FA_MODE_4CH);
#elif CONFIG_FA_PRESET_SEL4
    fa_io_default(c, FA_MODE_SEL4);
#else
    fa_io_default(c, FA_MODE_FAN3);
#endif
    for (int k = 0; k < FA_CH; k++) {
#if CONFIG_FA_DI_DEFAULT_PRESS
        c->di_mode[k] = FA_DI_PRESS;
#elif CONFIG_FA_DI_DEFAULT_OFF
        c->di_mode[k] = FA_DI_OFF;
#endif
        c->jog_ms[k] = CONFIG_FA_JOG_MS;
    }
}

/* 設定存 NVS（"fa"/"io_cfg"）；讀不到或內容不合法就用出廠預設。V4（Matter）由 HA／維護網頁寫入 */
static void io_cfg_load(fa_io_cfg_t *c)
{
    nvs_handle_t h;
    size_t len = sizeof *c;
    bool ok = false;
    if (nvs_open("fa", NVS_READONLY, &h) == ESP_OK) {
        ok = nvs_get_blob(h, "io_cfg", c, &len) == ESP_OK && len == sizeof *c && fa_io_cfg_valid(c);
        nvs_close(h);
    }
    if (!ok) io_cfg_default(c);
}

static void io_cfg_save(const fa_io_cfg_t *c)
{
    nvs_handle_t h;
    if (nvs_open("fa", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "io_cfg", c, sizeof *c);
    nvs_commit(h);
    nvs_close(h);
}

static void name_load_save(bool save)
{
    nvs_handle_t h;
    size_t len = sizeof s_name;
    if (nvs_open("fa", save ? NVS_READWRITE : NVS_READONLY, &h) != ESP_OK) return;
    if (save) { nvs_set_str(h, "name", s_name); nvs_commit(h); }
    else if (nvs_get_str(h, "name", s_name, &len) != ESP_OK) strcpy(s_name, "WO30109");
    nvs_close(h);
}

static void io_cfg_erase(void)
{
    nvs_handle_t h;
    if (nvs_open("fa", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "io_cfg");
    nvs_commit(h);
    nvs_close(h);
}

/* ---------------- 線圈驅動：全壓吸合 → PWM 降壓保持 ---------------- */
/* 電源預算的前提（fa_coil.h、Kconfig help）。menuconfig 改過其中一項，另一項不會跟著變，所以在這裡擋。 */
#if !CONFIG_FA_RELAY_PWM_HOLD && CONFIG_FA_MAX_RELAYS_ON > 3
#error "全壓驅動 4 顆＋發射峰值約 191mA，超過 IRM-02-12 保護點：關掉 FA_RELAY_PWM_HOLD 時 FA_MAX_RELAYS_ON 要 ≤3"
#endif
#if CONFIG_FA_RELAY_PWM_HOLD && CONFIG_FA_RELAY_STAGGER_MS <= CONFIG_FA_RELAY_PULLIN_MS
#error "FA_RELAY_STAGGER_MS 要大於 FA_RELAY_PULLIN_MS，才能保證同時最多 1 顆在全壓吸合"
#endif
#if CONFIG_FA_RELAY_PWM_HOLD
#define COIL_RES  LEDC_TIMER_10_BIT
#define COIL_FULL (1u << 10)                   /* LEDC duty = 2^res → 恆高 */
static const fa_coil_cfg_t COIL_CFG = {.pullin_ms = CONFIG_FA_RELAY_PULLIN_MS, .hold_pct = CONFIG_FA_RELAY_HOLD_PCT,
                                       .supply_mv = 12000, .coil_ohm = 360, .diode_mv = 300};
static esp_timer_handle_t s_hold_timer[FA_CH];
static bool               s_coil_on[FA_CH];
static SemaphoreHandle_t  s_coil_lock;        /* 保持計時器回呼與 coil_set 互斥，避免「剛關掉又被設成保持」 */

static void coil_write(uint8_t ch, uint8_t pct)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, pct >= 100 ? COIL_FULL : COIL_FULL * pct / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
}

static void coil_hold_cb(void *arg)
{
    uint8_t ch = (uint8_t)(uintptr_t)arg;
    xSemaphoreTake(s_coil_lock, portMAX_DELAY);
    if (s_coil_on[ch]) coil_write(ch, fa_coil_duty_pct(&COIL_CFG, true, COIL_CFG.pullin_ms));
    xSemaphoreGive(s_coil_lock);
}

static void coil_init(void)
{
    s_coil_lock = xSemaphoreCreateMutex();
    ledc_timer_config_t t = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = COIL_RES,
                             .timer_num = LEDC_TIMER_0, .freq_hz = CONFIG_FA_RELAY_PWM_HZ, .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    for (int i = 0; i < FA_CH; i++) {
        ledc_channel_config_t c = {.gpio_num = RELAY_PIN[i], .speed_mode = LEDC_LOW_SPEED_MODE,
                                   .channel = (ledc_channel_t)i, .timer_sel = LEDC_TIMER_0, .duty = 0};
        ESP_ERROR_CHECK(ledc_channel_config(&c));
        esp_timer_create_args_t a = {.callback = coil_hold_cb, .arg = (void *)(uintptr_t)i, .name = "coil_hold"};
        ESP_ERROR_CHECK(esp_timer_create(&a, &s_hold_timer[i]));
    }
    ESP_LOGI(TAG, "線圈：全壓 %dms → %d%% @ %dHz；保持時 %lumV、12V 端每顆 %lu.%01lumA",
             CONFIG_FA_RELAY_PULLIN_MS, CONFIG_FA_RELAY_HOLD_PCT, CONFIG_FA_RELAY_PWM_HZ,
             (unsigned long)fa_coil_voltage_mv(&COIL_CFG, COIL_CFG.hold_pct),
             (unsigned long)(fa_coil_supply_ua(&COIL_CFG, COIL_CFG.hold_pct) / 1000),
             (unsigned long)(fa_coil_supply_ua(&COIL_CFG, COIL_CFG.hold_pct) % 1000 / 100));
}

static void coil_set(uint8_t ch, bool on)
{
    esp_timer_stop(s_hold_timer[ch]);          /* 沒在跑會回 ESP_ERR_INVALID_STATE，忽略 */
    xSemaphoreTake(s_coil_lock, portMAX_DELAY);
    s_coil_on[ch] = on;
    coil_write(ch, fa_coil_duty_pct(&COIL_CFG, on, 0));
    xSemaphoreGive(s_coil_lock);
    if (on) esp_timer_start_once(s_hold_timer[ch], COIL_CFG.pullin_ms * 1000ULL);
}
#else
static void coil_init(void) {}
static void coil_set(uint8_t ch, bool on) { gpio_set_level(RELAY_PIN[ch], on); }
#endif

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
            fa_net_sync(on);
            continue;
        }
        if (a.delay_ms) vTaskDelay(pdMS_TO_TICKS(a.delay_ms));
        if (a.on) {                            /* 兩顆線圈吸合錯開，避免 12V 電源同時吃兩個湧浪 */
            int64_t wait_us = last_on_us + CONFIG_FA_RELAY_STAGGER_MS * 1000LL - esp_timer_get_time();
            if (wait_us > 0) vTaskDelay(pdMS_TO_TICKS(wait_us / 1000 + 1));
            last_on_us = esp_timer_get_time();
        }
        coil_set(a.ch, a.on);
        ESP_LOGI(TAG, "繼電器 %d %s", a.ch + 1, a.on ? "ON" : "OFF");
    }
}

static bool net_on_set(uint8_t ch, bool on)
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
        uint32_t addr = fa_ev1527_addr(code);
        uint8_t  key = fa_ev1527_key(code);
        bool repeat = code == last_code && t - last_ms < 600;          /* 同一次按壓的重送 */
        last_code = code;
        last_ms = t;
        if (!repeat) ESP_LOGI(TAG, "433：位址 0x%05lx 鍵 0x%x", (unsigned long)addr, key);

        if (s_learn_until_us && esp_timer_get_time() < s_learn_until_us) {
            if (repeat) continue;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            fa_remotes_add(&s_remotes, addr);
            xSemaphoreGive(s_lock);
            remotes_save();
            s_learn_until_us = 0;
            fa_nfc_port_publish();
            ESP_LOGI(TAG, "學習完成：0x%05lx（共 %d 支）", (unsigned long)addr, s_remotes.n);
            continue;
        }
        /* 遙控器第 k 鍵＝DI k 的分身：每一幀都送進去（按住判定靠持續收到幀，放開由 fa_io_tick 逾時判定） */
        int ch = fa_remote_key_to_channel(key);
        fa_action_t a[8];
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool known = fa_remotes_has(&s_remotes, addr);
        int n = (known && ch >= 0) ? fa_io_rf(&s_io, &s_relays, (uint8_t)ch, t, a, 8) : 0;
        xSemaphoreGive(s_lock);
        post_actions(a, n);
        io_post(n, (uint8_t)ch);
    }
}

/* ---------------- 套用 DI／DO 設定（NFC 與網路層 HA 下拉選單共用）----------------
 * 互鎖分組變了就先全關再重新分組，避免新組合下出現不該同時吸合的狀態。呼叫端要持有 s_lock，
 * 回傳要送給 relay_task 的動作數（在 a[] 裡），由呼叫端放掉鎖之後 post_actions */
static int apply_io_cfg_locked(const fa_io_cfg_t *io, fa_action_t a[8])
{
    int n = 0;
    uint8_t old_mask = fa_io_interlock_mask(&s_io.cfg), new_mask = fa_io_interlock_mask(io);
    if (old_mask != new_mask) {
        n = fa_relays_all_off(&s_relays, a, 8);
        fa_relays_init_group(&s_relays, new_mask, CONFIG_FA_MAX_RELAYS_ON, CONFIG_FA_INTERLOCK_DEAD_MS);
    }
    fa_io_init(&s_io, io);                        /* DI 狀態重新偵測：接通中的「持續」接點會在 30ms 後重新作動 */
    return n;
}

/* controller（HA 下拉選單）改了設定 */
static bool net_on_cfg(const fa_io_cfg_t *io)
{
    if (!fa_io_cfg_valid(io)) return false;
    fa_action_t a[8];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = apply_io_cfg_locked(io, a);
    xSemaphoreGive(s_lock);
    post_actions(a, n);
    io_cfg_save(io);
#if !CONFIG_FA_BOARD_V32
    fa_nfc_port_publish();                        /* NFC 的 STATE 跟著更新（gen＋1） */
#endif
    ESP_LOGI(TAG, "網路層套用新設定：互鎖 0x%x", fa_io_interlock_mask(io));
    return true;
}

/* ---------------- NFC 設定介面（V3.3）的 callback ---------------- */
#if !CONFIG_FA_BOARD_V32
static void nfc_get_cfg(fa_nfc_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    c->io = s_io.cfg;
    memcpy(c->name, s_name, sizeof c->name);
    c->n_remotes = s_remotes.n > FA_NFC_REMOTES_MAX ? FA_NFC_REMOTES_MAX : s_remotes.n;
    for (int i = 0; i < c->n_remotes; i++) c->remotes[i] = s_remotes.addr[i];
    xSemaphoreGive(s_lock);
}

/* 套用 App 送來（已驗簽）的設定。互鎖分組變了就先全關再重新分組，避免新組合下出現不該同時吸合的狀態 */
static void nfc_apply_cfg(const fa_nfc_cfg_t *c)
{
    fa_action_t a[8];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = apply_io_cfg_locked(&c->io, a);
    fa_remotes_init(&s_remotes);
    for (int i = 0; i < c->n_remotes; i++) fa_remotes_add(&s_remotes, c->remotes[i]);
    memcpy(s_name, c->name, sizeof s_name);
    s_name[sizeof s_name - 1] = 0;
    xSemaphoreGive(s_lock);
    post_actions(a, n);
    io_cfg_save(&c->io);
    remotes_save();
    name_load_save(true);
    fa_net_report_cfg(&c->io);                    /* HA 的下拉選單跟著更新 */
    ESP_LOGI(TAG, "NFC 套用新設定：互鎖 0x%x、遙控器 %d 支、名稱「%s」", fa_io_interlock_mask(&c->io), c->n_remotes, s_name);
}

static void nfc_get_status(fa_nfc_status_t *st)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int k = 0; k < FA_CH; k++) {
        if (s_relays.on[k]) st->relays |= 1u << k;
        if (s_io.active[k]) st->di |= 1u << k;
    }
    xSemaphoreGive(s_lock);
    st->net = fa_net_joined();
    st->flags = (s_learn_until_us && esp_timer_get_time() < s_learn_until_us) ? 1 : 0;
}

static void nfc_learn(void) { s_learn_until_us = esp_timer_get_time() + 20 * 1000000LL; }
static void nfc_identify(void) { s_identify_until_us = esp_timer_get_time() + 5 * 1000000LL; }
#if CONFIG_FA_DEVKIT_RGB
static volatile uint32_t s_light_rgb;            /* bit24＝開；低 24 bit＝RGB（controller 設的開發板彩色燈） */
static void net_light(bool on, uint8_t r, uint8_t g, uint8_t b)
{
    s_light_rgb = (on ? 1u << 24 : 0) | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
    ESP_LOGI(TAG, "開發板彩色燈：%s RGB(%u,%u,%u)", on ? "開" : "關", r, g, b);
}
#endif
static void net_identify(uint16_t seconds)
{
    s_identify_until_us = seconds ? esp_timer_get_time() + seconds * 1000000LL : 0;
}
static void factory_reset(void);
static void nfc_factory_reset(void) { factory_reset(); }
#endif

/* ---------------- 按鍵、燈號 ---------------- */
static void factory_reset(void)
{
    ESP_LOGW(TAG, "恢復出廠：全部繼電器關、清除遙控器與 DI/DO 設定、清除 Matter 配對");
    apply(fa_relays_all_off);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    fa_remotes_init(&s_remotes);
    xSemaphoreGive(s_lock);
    remotes_save();
    io_cfg_erase();
    {
        fa_io_cfg_t def;
        io_cfg_default(&def);
        fa_net_report_cfg(&def);
    }
    strcpy(s_name, "WO30109"); name_load_save(true);
    fa_nfc_port_publish();                         /* gen＋1：重置前 App 手上的請求全部作廢 */
    vTaskDelay(pdMS_TO_TICKS(300));
    fa_net_factory_reset();
}

static fa_led_in_t led_input(uint32_t held)
{
    int64_t now = esp_timer_get_time();
    return (fa_led_in_t){
        .held_ms = held,
        .learning = s_learn_until_us && now < s_learn_until_us,
        .identifying = s_identify_until_us && now < s_identify_until_us,   /* Matter Identify 或 NFC 找裝置 */
        .net = !fa_net_commissioned() ? FA_LED_NET_UNPAIRED : fa_net_joined() ? FA_LED_NET_ONLINE : FA_LED_NET_OFFLINE,
    };
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
        for (uint8_t k = 0; k < FA_CH; k++) {                     /* 接線 DI：每 10ms 取樣，fa_io 內部防彈跳 */
            fa_action_t a[8];
            bool closed = gpio_get_level(DI_PIN[k]) == 0;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            int n = fa_io_wired(&s_io, &s_relays, k, closed, t, a, 8);
            xSemaphoreGive(s_lock);
            post_actions(a, n);
            io_post(n, k);
        }
        {                                                          /* 遙控器放開判定、點動到時 */
            fa_action_t a[8];
            xSemaphoreTake(s_lock, portMAX_DELAY);
            int n = fa_io_tick(&s_io, &s_relays, t, a, 8);
            xSemaphoreGive(s_lock);
            post_actions(a, n);
        }
        {                                                          /* DI 狀態（接線或遙控器）變了就回報（接點感測器） */
            static uint8_t last_di = 0xFF;
            uint8_t mask = 0;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            for (int k = 0; k < FA_CH; k++) if (s_io.active[k]) mask |= 1u << k;
            xSemaphoreGive(s_lock);
            if (mask != last_di) { last_di = mask; fa_net_report_di(mask); }
        }
        fa_led_in_t led = led_input(fa_button_held_ms(&btn, t));
        gpio_set_level(PIN_LED, fa_led_level(&led, t));
#if CONFIG_FA_DEVKIT_RGB
        {
            bool on[FA_CH];
            xSemaphoreTake(s_lock, portMAX_DELAY);
            memcpy(on, s_relays.on, sizeof on);
            xSemaphoreGive(s_lock);
            uint32_t l = s_light_rgb;
            if (l >> 24)                                           /* controller 開了彩色燈：顯示它設的顏色 */
                ws2812_set((uint8_t)(l >> 16), (uint8_t)(l >> 8), (uint8_t)l);
            else {                                                 /* 關著：K1–K4 顏色或狀態閃燈；正常且全關時全暗 */
                fa_rgb_t c = fa_led_rgb(fa_led_rgb_status_lit(&led, t, on), on);
                ws2812_set(c.r, c.g, c.b);
            }
        }
#endif
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
                        .pin_bit_mask = (1ULL << PIN_BUTTON) | (1ULL << PIN_DI_1) | (1ULL << PIN_DI_2)
                                        | (1ULL << PIN_DI_3) | (1ULL << PIN_DI_4)};   /* DI：內建上拉＋板上 10nF */
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

    gpio_setup();                              /* 繼電器腳先拉低，LEDC 接手前不會吸合 */
#if CONFIG_FA_DEVKIT_RGB
#if CONFIG_FA_DEVKIT_RGB_ORDER_RGB
    const fa_led_order_t order = FA_LED_ORDER_RGB;
#else
    const fa_led_order_t order = FA_LED_ORDER_GRB;
#endif
    if (ws2812_init(8, order) != ESP_OK) ESP_LOGE(TAG, "板載 RGB 燈（GPIO8）初始化失敗");
    else ESP_LOGI(TAG, "開發板模式：板載 RGB 燈（GPIO8）顯示狀態與 K1–K4");
#endif
    coil_init();
    s_lock = xSemaphoreCreateMutex();
    s_relay_q = xQueueCreate(32, sizeof(fa_action_t));
    s_rf_q = xQueueCreate(256, sizeof(rf_edge_t));

    fa_io_cfg_t cfg;
    io_cfg_load(&cfg);
    fa_io_init(&s_io, &cfg);
    fa_relays_init_group(&s_relays, fa_io_interlock_mask(&cfg), CONFIG_FA_MAX_RELAYS_ON, CONFIG_FA_INTERLOCK_DEAD_MS);
    static const char *DI_NAME[] = {"全關", "按一次", "持續"}, *DO_NAME[] = {"自鎖", "點動", "互鎖"};
    for (int k = 0; k < FA_CH; k++)
        ESP_LOGI(TAG, "K%d：DI %s／DO %s（點動 %lums）", k + 1, DI_NAME[cfg.di_mode[k]], DO_NAME[cfg.do_mode[k]],
                 (unsigned long)cfg.jog_ms[k]);
    ESP_LOGI(TAG, "互鎖群組 0x%x；同時吸合上限 %d；Thread 發射 %ddBm", fa_io_interlock_mask(&cfg),
             CONFIG_FA_MAX_RELAYS_ON, CONFIG_FA_TX_POWER);
    remotes_load();
    name_load_save(false);

    xTaskCreate(relay_task, "relay", 3072, NULL, 6, NULL);
    xTaskCreate(rf_task, "rf", 3072, NULL, 4, NULL);
    xTaskCreate(ui_task, "ui", 3072, NULL, 3, NULL);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_RF_DATA, rf_isr, NULL);
#if !CONFIG_FA_BOARD_V32
    static const fa_nfc_port_cb_t nfc_cb = {.get_cfg = nfc_get_cfg, .apply_cfg = nfc_apply_cfg, .get_status = nfc_get_status,
                                            .learn_remote = nfc_learn, .identify = nfc_identify,
                                            .factory_reset = nfc_factory_reset};
    fa_nfc_port_start(PIN_NFC_SDA, PIN_NFC_SCL, &nfc_cb);
#endif
    {
        static const fa_net_cb_t net_cb = {.on_set = net_on_set, .on_cfg = net_on_cfg,
                                              .identify = net_identify,
#if CONFIG_FA_DEVKIT_RGB
                                              .light = net_light,
#endif
        };
        xSemaphoreTake(s_lock, portMAX_DELAY);
        fa_io_cfg_t cfg_now = s_io.cfg;
        xSemaphoreGive(s_lock);
        fa_net_start(&net_cb, &cfg_now);
    }
}

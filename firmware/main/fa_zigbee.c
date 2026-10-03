#include "fa_zigbee.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_zigbee.h"
#include "ezbee/zha.h"

static const char *TAG = "fa_zb";

#define STORAGE_PARTITION "nvs"
#define MANUFACTURER_NAME "\x08""WOOWTECH"
#define MODEL_ID          "\x0a""WO30109_FA"

static fa_zb_set_cb_t s_on_set;
static volatile bool  s_joined;

bool fa_zigbee_joined(void) { return s_joined; }

/* ---------- 配網（BDB）重試：用 esp_timer，回呼裡取 Zigbee 鎖 ---------- */
static void commission_cb(void *arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_bdb_start_top_level_commissioning((uint8_t)(uintptr_t)arg);
    esp_zigbee_lock_release();
}

static void commission_later(uint8_t mode, uint32_t ms)
{
    const esp_timer_create_args_t a = {.callback = commission_cb, .arg = (void *)(uintptr_t)mode, .name = "zb_retry"};
    esp_timer_handle_t t;
    if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, (uint64_t)ms * 1000);
}

static bool signal_handler(const ezb_app_signal_t *sig)
{
    ezb_app_signal_type_t type = ezb_app_signal_get_type(sig);
    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t st = *(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(sig);
        if (st != EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "初始化失敗 0x%02x，1 秒後重試", st);
            commission_later(EZB_BDB_MODE_INITIALIZATION, 1000);
            break;
        }
        ezb_set_tx_power(CONFIG_FA_ZB_TX_POWER);
        if (ezb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "出廠狀態：開始尋找網路（請在閘道開啟允許加入）");
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        } else {
            s_joined = true;
            ESP_LOGI(TAG, "重開機，已在網路 PAN 0x%04hx", ezb_nwk_get_panid());
        }
    } break;
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t st = *(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(sig);
        if (st == EZB_BDB_STATUS_SUCCESS) {
            s_joined = true;
            ESP_LOGI(TAG, "已加入網路 PAN 0x%04hx 頻道 %d 短位址 0x%04hx", ezb_nwk_get_panid(),
                     ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
        } else {
            ESP_LOGI(TAG, "找不到可加入的網路，10 秒後再試");
            commission_later(EZB_BDB_MODE_NETWORK_STEERING, 10000);
        }
    } break;
    case EZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG, "已離開網路，重開機");
        s_joined = false;
        esp_restart();
        break;
    default:
        ESP_LOGD(TAG, "signal %s", ezb_app_signal_to_string(type));
        break;
    }
    return true;
}

/* ---------- ZCL：協調器寫 OnOff ---------- */
static void zcl_handler(ezb_zcl_core_action_callback_id_t id, void *message)
{
    if (id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID) return;
    ezb_zcl_set_attr_value_message_t *m = message;
    if (m->info.cluster_id != EZB_ZCL_CLUSTER_ID_ON_OFF) return;
    uint8_t ch = m->info.dst_ep - FA_ZB_EP_FIRST;
    bool    on = *(uint8_t *)m->in.attribute.data.value;
    if (ch < 4 && s_on_set && !s_on_set(ch, on)) {
        ESP_LOGW(TAG, "拒絕：繼電器 %d %s", ch + 1, on ? "ON" : "OFF");
        m->out.result = EZB_ZCL_STATUS_FAIL;
    }
}

void fa_zigbee_sync(const bool on[4])
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    for (uint8_t ch = 0; ch < 4; ch++) {
        uint8_t v = on[ch];
        ezb_zcl_set_attr_value(FA_ZB_EP_FIRST + ch, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER,
                               EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, EZB_ZCL_STD_MANUF_CODE, &v, false);
    }
    esp_zigbee_lock_release();
}

void fa_zigbee_factory_reset(void)
{
    if (!s_joined) {                       /* 不在網路上：直接重開機重新尋網 */
        esp_restart();
        return;
    }
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_bdb_reset_via_local_action();      /* 完成後會收到 EZB_ZDO_SIGNAL_LEAVE → 重開機 */
    esp_zigbee_lock_release();
}

static void create_device(void)
{
    ezb_af_device_desc_t dev = ezb_af_create_device_desc();
    for (uint8_t ch = 0; ch < 4; ch++) {
        ezb_zha_mains_power_outlet_config_t cfg = EZB_ZHA_MAINS_POWER_OUTLET_CONFIG();
        cfg.basic_cfg.power_source = EZB_ZCL_BASIC_POWER_SOURCE_SINGLE_PHASE_MAINS;
        ezb_af_ep_desc_t ep = ezb_zha_create_mains_power_outlet(FA_ZB_EP_FIRST + ch, &cfg);
        if (ch == 0) {
            ezb_zcl_cluster_desc_t basic = ezb_af_endpoint_get_cluster_desc(ep, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
            ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)MANUFACTURER_NAME);
            ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)MODEL_ID);
        }
        ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev, ep));
    }
    ESP_ERROR_CHECK(ezb_af_device_desc_register(dev));
    ezb_zcl_core_action_handler_register(zcl_handler);
}

static void zigbee_task(void *arg)
{
    esp_zigbee_config_t cfg = {
        .device_config = {
            .device_type         = EZB_NWK_DEVICE_TYPE_ROUTER,     /* 市電供電：當路由器，幫其他電池裝置中繼 */
            .install_code_policy = false,
            .zczr_config         = {.max_children = 10},
        },
        .platform_config = {
            .storage_partition_name = STORAGE_PARTITION,
            .radio_config           = {.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE},
        },
    };
    ESP_ERROR_CHECK(esp_zigbee_init(&cfg));
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(0x07FFF800));   /* 頻道 11–26 全掃 */
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(signal_handler));
    create_device();
    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();
    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

void fa_zigbee_start(fa_zb_set_cb_t on_set)
{
    s_on_set = on_set;
    xTaskCreate(zigbee_task, "zigbee", 4096, NULL, 5, NULL);
}

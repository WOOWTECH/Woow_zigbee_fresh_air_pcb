/* fa_net 介面的 Matter 實作（V4）：Matter over Thread（正式）／over Wi-Fi（開發測試，menuconfig 切換）。
 *
 * 資料模型（docs/v4-matter-spec.md §3）：
 *   EP1–4   On/Off Plug-in Unit   K1–K4
 *   EP5–8   Contact Sensor        DI1–DI4（Boolean State.StateValue：接通＝true）
 *   EP9–20  Mode Select ×12       每路 DI 模式／DO 模式／點動時間（HA 顯示成下拉選單；選項見 fa_modes）
 *   EP1–8 另帶 Fixed Label {ha_entitylabel: K1…K4／DI1…DI4}：HA 用它取代 entity 名稱裡的 endpoint 號碼
 *   （只對 HA 白名單內的 VID/PID 有效，測試 VID 0xFFF1/PID 0x8000 在內；docs/v4-matter-research.md §1）。
 * Endpoint 號碼依建立順序自動分配，實際值記在 s_ep_*（開機 log 會印）。
 *
 * 執行緒：attribute 回呼在 CHIP 執行緒；fa_net_sync／report_* 從 app 的任務呼叫，用 ScopedChipStackLock 持鎖。
 *   不可在 CHIP 執行緒裡呼叫 fa_net_*（ALREADY_TAKEN 時 RAII 鎖不會重複解鎖，但 s_local 旗標的語意會錯）。
 * 回音：app 自己呼叫 attribute::update 也會觸發 PRE_UPDATE 回呼，用 s_local（持鎖期間設定）擋掉，避免當成 controller 指令。
 */
#include "fa_net.h"

#include <esp_log.h>
#include <esp_matter.h>
#include <app/clusters/mode-select-server/supported-modes-manager.h>
#include <app/server/Server.h>
#include <esp_matter_providers.h>
#include <platform/DeviceInfoProvider.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <openthread/platform/radio.h>
#endif

extern "C" {
#include "fa_modes.h"
}

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static const char *TAG = "fa_matter";

static fa_net_cb_t s_cb;
static fa_io_cfg_t s_cfg;                        /* 下拉選單顯示用的目前設定（只在 CHIP 執行緒或持鎖時讀寫） */
static uint16_t    s_ep_plug[4], s_ep_contact[4], s_ep_sel[4][FA_SEL_KINDS];
static bool        s_local;                      /* 持鎖期間 app 自己在更新屬性 */
static volatile bool s_commissioned, s_net_up;

/* ---------------- Fixed Label（自訂 DeviceInfoProvider：標籤寫死在韌體，不必燒工廠分區） ---------------- */
/* 沒有 User Label、語系、曆法相關 cluster，那些清單一律回空。 */
class FaDeviceInfo : public chip::DeviceLayer::DeviceInfoProvider {
public:
    void setLabel(int i, chip::EndpointId ep, const char *value)
    {
        mEp[i] = ep;
        snprintf(mValue[i], sizeof mValue[i], "%s", value);
    }
    FixedLabelIterator *IterateFixedLabel(chip::EndpointId ep) override
    {
        const char *v = nullptr;
        for (int i = 0; i < kN; i++)
            if (mEp[i] == ep && mEp[i] != 0) v = mValue[i];
        return chip::Platform::New<FixedIt>(v);
    }
    UserLabelIterator *IterateUserLabel(chip::EndpointId) override { return chip::Platform::New<Empty<UserLabelType>>(); }
    SupportedLocalesIterator *IterateSupportedLocales() override { return chip::Platform::New<Empty<chip::CharSpan>>(); }
    SupportedCalendarTypesIterator *IterateSupportedCalendarTypes() override { return chip::Platform::New<Empty<CalendarType>>(); }

protected:
    CHIP_ERROR SetUserLabelLength(chip::EndpointId, size_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }
    CHIP_ERROR GetUserLabelLength(chip::EndpointId, size_t &val) override { val = 0; return CHIP_NO_ERROR; }
    CHIP_ERROR SetUserLabelAt(chip::EndpointId, size_t, const UserLabelType &) override { return CHIP_ERROR_NOT_IMPLEMENTED; }
    CHIP_ERROR DeleteUserLabelAt(chip::EndpointId, size_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

private:
    static constexpr int kN = 8;
    static constexpr const char *kKey = "ha_entitylabel";   /* HA 比對時不分大小寫；key／value 都要少於 16 字元 */
    chip::EndpointId mEp[kN] = {};
    char mValue[kN][8] = {};

    template <typename T> class Empty : public Iterator<T> {
    public:
        size_t Count() override { return 0; }
        bool Next(T &) override { return false; }
        void Release() override { chip::Platform::Delete(this); }
    };
    class FixedIt : public FixedLabelIterator {
    public:
        explicit FixedIt(const char *value) : mV(value) {}
        size_t Count() override { return mV ? 1 : 0; }
        bool Next(FixedLabelType &out) override
        {
            if (!mV || mDone) return false;
            out.label = chip::CharSpan::fromCharString(kKey);
            out.value = chip::CharSpan::fromCharString(mV);
            mDone = true;
            return true;
        }
        void Release() override { chip::Platform::Delete(this); }

    private:
        const char *mV;
        bool mDone = false;
    };
};
static FaDeviceInfo s_info;

/* ---------------- Mode Select 選項清單（全部 12 個選單共用一個 manager，依 endpoint 回傳） ---------------- */
using ModeOption = ModeSelect::Structs::ModeOptionStruct::Type;

class FaModesManager : public ModeSelect::SupportedModesManager {
public:
    void init()
    {
        for (int k = 0; k < FA_SEL_KINDS; k++) {
            uint8_t n = fa_sel_count((fa_sel_kind_t)k);
            for (uint8_t m = 0; m < n; m++) {
                mOpt[k][m].label = chip::CharSpan::fromCharString(fa_sel_label((fa_sel_kind_t)k, m));
                mOpt[k][m].mode = m;
                mOpt[k][m].semanticTags = chip::app::DataModel::List<const ModeSelect::Structs::SemanticTagStruct::Type>();
            }
        }
    }
    ModeOptionsProvider getModeOptionsProvider(chip::EndpointId ep) const override
    {
        int k = kindOf(ep);
        if (k < 0) return ModeOptionsProvider();
        return ModeOptionsProvider(mOpt[k], mOpt[k] + fa_sel_count((fa_sel_kind_t)k));
    }
    chip::Protocols::InteractionModel::Status getModeOptionByMode(chip::EndpointId ep, uint8_t mode,
                                                                  const ModeOption **data) const override
    {
        int k = kindOf(ep);
        if (k < 0) return chip::Protocols::InteractionModel::Status::UnsupportedCluster;
        if (mode >= fa_sel_count((fa_sel_kind_t)k)) return chip::Protocols::InteractionModel::Status::InvalidCommand;
        *data = &mOpt[k][mode];
        return chip::Protocols::InteractionModel::Status::Success;
    }

private:
    static int kindOf(chip::EndpointId ep)
    {
        for (int ch = 0; ch < 4; ch++)
            for (int k = 0; k < FA_SEL_KINDS; k++)
                if (s_ep_sel[ch][k] == ep) return k;
        return -1;
    }
    ModeOption mOpt[FA_SEL_KINDS][FA_JOG_PRESETS];
};
static FaModesManager s_modes;

static bool find_sel(uint16_t ep, int *ch, fa_sel_kind_t *kind)
{
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < FA_SEL_KINDS; k++)
            if (s_ep_sel[c][k] == ep) { *ch = c; *kind = (fa_sel_kind_t)k; return true; }
    return false;
}

static int find_plug(uint16_t ep)
{
    for (int c = 0; c < 4; c++)
        if (s_ep_plug[c] == ep) return c;
    return -1;
}

/* ---------------- controller → app ---------------- */
static esp_err_t on_attr(attribute::callback_type_t type, uint16_t ep, uint32_t cluster, uint32_t attr,
                         esp_matter_attr_val_t *val, void *priv)
{
    if (type != attribute::PRE_UPDATE || s_local) return ESP_OK;
    if (cluster == OnOff::Id && attr == OnOff::Attributes::OnOff::Id) {
        int ch = find_plug(ep);
        if (ch < 0 || !s_cb.on_set) return ESP_OK;
        /* 拒絕（同時吸合上限）就回錯誤：這次寫入不生效，controller 會看到指令失敗、屬性維持原值 */
        return s_cb.on_set((uint8_t)ch, val->val.b) ? ESP_OK : ESP_FAIL;
    }
    if (cluster == ModeSelect::Id && attr == ModeSelect::Attributes::CurrentMode::Id) {
        int ch; fa_sel_kind_t kind;
        if (!find_sel(ep, &ch, &kind) || !s_cb.on_cfg) return ESP_OK;
        fa_io_cfg_t next = s_cfg;
        if (!fa_sel_set(&next, (uint8_t)ch, kind, val->val.u8)) return ESP_FAIL;
        if (!s_cb.on_cfg(&next)) return ESP_FAIL;
        s_cfg = next;
    }
    return ESP_OK;
}

static esp_err_t on_identify(identification::callback_type_t type, uint16_t ep, uint8_t effect, uint8_t variant, void *priv)
{
    /* START：IdentifyTime 開始倒數（esp-matter 不給秒數，給上限 120s，STOP 時會停）；EFFECT：TriggerEffect 閃一下（3s） */
    ESP_LOGI(TAG, "Identify：endpoint %u，type %d，effect %u", ep, (int)type, effect);
    if (!s_cb.identify) return ESP_OK;
    switch (type) {
    case identification::START:  s_cb.identify(120); break;
    case identification::STOP:   s_cb.identify(0); break;
    case identification::EFFECT: s_cb.identify(effect == chip::to_underlying(Identify::EffectIdentifierEnum::kStopEffect) ? 0 : 3); break;
    default: break;
    }
    return ESP_OK;
}

static void refresh_commissioned()
{
    s_commissioned = chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

static void on_event(const ChipDeviceEvent *e, intptr_t arg)
{
    using namespace chip::DeviceLayer;
    switch (e->Type) {
    case DeviceEventType::kServerReady:
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
        /* 802.15.4 驅動預設最大功率（+20dBm），超過 12V 電源預算：限制在 CONFIG_FA_TX_POWER（Kconfig help） */
        esp_openthread_lock_acquire(portMAX_DELAY);
        otPlatRadioSetTransmitPower(esp_openthread_get_instance(), CONFIG_FA_TX_POWER);
        esp_openthread_lock_release();
        ESP_LOGI(TAG, "Thread 發射功率 %d dBm", CONFIG_FA_TX_POWER);
#endif
        [[fallthrough]];
    case DeviceEventType::kCommissioningComplete:
    case DeviceEventType::kFabricRemoved:
        refresh_commissioned();
        ESP_LOGI(TAG, "配對狀態：%s", s_commissioned ? "已配對" : "未配對（可配對）");
        break;
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    case DeviceEventType::kThreadConnectivityChange:
        s_net_up = e->ThreadConnectivityChange.Result == kConnectivity_Established;
        ESP_LOGI(TAG, "Thread %s", s_net_up ? "已連線" : "斷線");
        break;
#endif
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    case DeviceEventType::kWiFiConnectivityChange:
        s_net_up = e->WiFiConnectivityChange.Result == kConnectivity_Established;
        ESP_LOGI(TAG, "Wi-Fi %s", s_net_up ? "已連線" : "斷線");
        break;
#endif
    default:
        break;
    }
}

/* ---------------- app → controller ---------------- */
static void update_locked(uint16_t ep, uint32_t cluster, uint32_t attr, esp_matter_attr_val_t v)
{
    if (!ep) return;
    s_local = true;
    attribute::update(ep, cluster, attr, &v);
    s_local = false;
}

static void push_cfg_locked()
{
    for (int ch = 0; ch < 4; ch++)
        for (int k = 0; k < FA_SEL_KINDS; k++)
            update_locked(s_ep_sel[ch][k], ModeSelect::Id, ModeSelect::Attributes::CurrentMode::Id,
                          esp_matter_uint8(fa_sel_get(&s_cfg, (uint8_t)ch, (fa_sel_kind_t)k)));
}

extern "C" void fa_net_sync(const bool on[4])
{
    lock::ScopedChipStackLock lk(portMAX_DELAY);
    for (int ch = 0; ch < 4; ch++)
        update_locked(s_ep_plug[ch], OnOff::Id, OnOff::Attributes::OnOff::Id, esp_matter_bool(on[ch]));
}

extern "C" void fa_net_report_di(uint8_t mask)
{
    lock::ScopedChipStackLock lk(portMAX_DELAY);
    for (int ch = 0; ch < 4; ch++)
        update_locked(s_ep_contact[ch], BooleanState::Id, BooleanState::Attributes::StateValue::Id,
                      esp_matter_bool((mask >> ch) & 1));
}

extern "C" void fa_net_report_cfg(const fa_io_cfg_t *cfg)
{
    lock::ScopedChipStackLock lk(portMAX_DELAY);
    s_cfg = *cfg;
    push_cfg_locked();
}

extern "C" bool fa_net_joined(void) { return s_commissioned && s_net_up; }
extern "C" bool fa_net_commissioned(void) { return s_commissioned; }

extern "C" void fa_net_factory_reset(void)
{
    /* 只清 Matter 自己的 NVS（esp_matter、chip-config、chip-counters），不動 app 的 "fa"（NFC 金鑰等）；完成後重開機 */
    esp_matter::factory_reset();
}

/* ---------------- 建立節點 ---------------- */
static const char *const SEL_NAME[FA_SEL_KINDS] = {"DI%d 模式", "K%d 模式", "K%d 點動時間"};

static void add_label(endpoint_t *ep, int slot, const char *fmt, int n)
{
    if (!ep) return;
    char v[8];
    snprintf(v, sizeof v, fmt, n);
    cluster::fixed_label::config_t fl;
    if (!cluster::fixed_label::create(ep, &fl, CLUSTER_FLAG_SERVER)) ESP_LOGE(TAG, "EP%u 建 Fixed Label 失敗", endpoint::get_id(ep));
    s_info.setLabel(slot, endpoint::get_id(ep), v);
}

extern "C" void fa_net_start(const fa_net_cb_t *cb, const fa_io_cfg_t *cfg)
{
    s_cb = *cb;
    s_cfg = *cfg;
    s_modes.init();

    node::config_t node_cfg;
    node_t *node = node::create(&node_cfg, on_attr, on_identify);
    if (!node) { ESP_LOGE(TAG, "建立 Matter node 失敗"); return; }

    for (int ch = 0; ch < 4; ch++) {
        on_off_plug_in_unit::config_t c;
        c.on_off.on_off = false;
        endpoint_t *ep = on_off_plug_in_unit::create(node, &c, ENDPOINT_FLAG_NONE, nullptr);
        s_ep_plug[ch] = ep ? endpoint::get_id(ep) : 0;
        add_label(ep, ch, "K%d", ch + 1);
    }
    for (int ch = 0; ch < 4; ch++) {
        contact_sensor::config_t c;
        c.boolean_state.state_value = false;
        endpoint_t *ep = contact_sensor::create(node, &c, ENDPOINT_FLAG_NONE, nullptr);
        s_ep_contact[ch] = ep ? endpoint::get_id(ep) : 0;
        add_label(ep, 4 + ch, "DI%d", ch + 1);
    }
    for (int ch = 0; ch < 4; ch++) {
        for (int k = 0; k < FA_SEL_KINDS; k++) {
            mode_select::config_t c;
            snprintf(c.mode_select.description, sizeof c.mode_select.description, SEL_NAME[k], ch + 1);
            /* standard_namespace：legacy config 宣告成 const、預設就是 null（不屬於任何標準命名空間），不用設 */
            c.mode_select.current_mode = fa_sel_get(&s_cfg, (uint8_t)ch, (fa_sel_kind_t)k);
            c.mode_select.delegate = &s_modes;
            endpoint_t *ep = mode_select::create(node, &c, ENDPOINT_FLAG_NONE, nullptr);
            s_ep_sel[ch][k] = ep ? endpoint::get_id(ep) : 0;
        }
    }
    ESP_LOGI(TAG, "Endpoint：插座 %u–%u、接點感測器 %u–%u、下拉選單 %u–%u", s_ep_plug[0], s_ep_plug[3],
             s_ep_contact[0], s_ep_contact[3], s_ep_sel[0][0], s_ep_sel[3][FA_SEL_KINDS - 1]);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* ESP32-C6 內建 802.15.4（native radio），沒有外接 RCP；OpenThread 資料存在 "nvs" 分區 */
    esp_openthread_platform_config_t ot = {
        .radio_config = {.radio_mode = RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = HOST_CONNECTION_MODE_NONE},
        .port_config = {.storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10},
    };
    set_openthread_platform_config(&ot);
#endif
    esp_matter::set_custom_device_info_provider(&s_info);   /* 一定要在 start 前：Fixed Label cluster 沒有 provider 會 VerifyOrDie */
    if (esp_matter::start(on_event) != ESP_OK) ESP_LOGE(TAG, "Matter 啟動失敗");
}

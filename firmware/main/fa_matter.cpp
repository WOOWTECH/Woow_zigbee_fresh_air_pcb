/* fa_net 介面的 Matter 實作（V4）：Matter over Thread（正式）／over Wi-Fi（開發測試，menuconfig 切換）。
 *
 * 資料模型（docs/v4-matter-spec.md §3）：
 *   EP1–4   On/Off Plug-in Unit   K1–K4
 *   EP5–8   Contact Sensor        DI1–DI4（Boolean State.StateValue：接通＝true）
 *   EP9–20  Mode Select ×12       每路 DI 模式／DO 模式／點動時間（HA 顯示成下拉選單；選項見 fa_modes）
 *   EP21    Extended Color Light  只在 CONFIG_FA_DEVKIT_LIGHT（預設關）：板載 RGB 燈，讓 controller 能測燈
 *   實驗 CONFIG_FA_DEVKIT_COVER_ONLY：只建一個 Window Covering endpoint（Drapery，Lift＋位置感知），
 *   「窗簾行程時間」下拉選單是同一個 endpoint 上的 Mode Select cluster
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
#include <app/clusters/window-covering-server/window-covering-server.h>
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
#include "fa_color.h"
#include "fa_cover.h"
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
#if CONFIG_FA_DEVKIT_LIGHT
static uint16_t    s_ep_light;                   /* 開發板彩色燈（建立失敗＝0） */
#endif
static uint16_t    s_ep_cover, s_ep_travel;      /* 窗簾與行程時間選單（沒建＝0） */
static uint16_t    s_cover_pos = FA_COVER_FULL;  /* fa_net_cover_init 給的開機位置 */
static uint32_t    s_cover_travel_ms = FA_COVER_TRAVEL_DEFAULT_MS;

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
    void initTravel()
    {
        for (uint8_t i = 0; i < FA_COVER_TRAVEL_PRESETS; i++) {
            fa_cover_travel_label(i, mTravelLabel[i]);
            mTravel[i].label = chip::CharSpan::fromCharString(mTravelLabel[i]);
            mTravel[i].mode = i;
            mTravel[i].semanticTags = chip::app::DataModel::List<const ModeSelect::Structs::SemanticTagStruct::Type>();
        }
    }
    ModeOptionsProvider getModeOptionsProvider(chip::EndpointId ep) const override
    {
        if (ep && ep == s_ep_travel) return ModeOptionsProvider(mTravel, mTravel + FA_COVER_TRAVEL_PRESETS);
        int k = kindOf(ep);
        if (k < 0) return ModeOptionsProvider();
        return ModeOptionsProvider(mOpt[k], mOpt[k] + fa_sel_count((fa_sel_kind_t)k));
    }
    chip::Protocols::InteractionModel::Status getModeOptionByMode(chip::EndpointId ep, uint8_t mode,
                                                                  const ModeOption **data) const override
    {
        if (ep && ep == s_ep_travel) {
            if (mode >= FA_COVER_TRAVEL_PRESETS) return chip::Protocols::InteractionModel::Status::InvalidCommand;
            *data = &mTravel[mode];
            return chip::Protocols::InteractionModel::Status::Success;
        }
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
    ModeOption mTravel[FA_COVER_TRAVEL_PRESETS];
    char mTravelLabel[FA_COVER_TRAVEL_PRESETS][12];
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

/* ---------------- 窗簾：Window Covering server 寫好 Target 後呼叫這裡 ---------------- */
class FaCoverDelegate : public WindowCovering::Delegate {
public:
    CHIP_ERROR HandleMovement(WindowCovering::WindowCoveringType type) override
    {
        if (type != WindowCovering::WindowCoveringType::Lift || !s_cb.cover_goto) return CHIP_NO_ERROR;
        esp_matter_attr_val_t v = esp_matter_invalid(nullptr);
        attribute::get_val(attribute::get(s_ep_cover, WindowCovering::Id,
                                          WindowCovering::Attributes::TargetPositionLiftPercent100ths::Id), &v);
        if (v.type == ESP_MATTER_VAL_TYPE_INVALID || v.val.u16 > FA_COVER_FULL) return CHIP_ERROR_INVALID_ARGUMENT;   /* null */
        ESP_LOGI(TAG, "窗簾：目標 %u.%02u%%", v.val.u16 / 100, v.val.u16 % 100);
        s_cb.cover_goto(v.val.u16);
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR HandleStopMotion() override
    {
        if (!s_cb.cover_stop) return CHIP_NO_ERROR;
        uint16_t p = s_cb.cover_stop();
        /* 先把停下的位置寫進 Current：server 回傳後會把 Target 設成 Current，狀態就變成停止 */
        esp_matter_attr_val_t v = esp_matter_nullable_uint16(p);
        s_local = true;
        attribute::update(s_ep_cover, WindowCovering::Id, WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id, &v);
        s_local = false;
        ESP_LOGI(TAG, "窗簾：停在 %u.%02u%%", p / 100, p % 100);
        return CHIP_NO_ERROR;
    }
};
static FaCoverDelegate s_cover_delegate;

/* ---------------- controller → app ---------------- */
#if CONFIG_FA_DEVKIT_LIGHT
/* ---------------- 開發板彩色燈：讀目前屬性 → RGB ---------------- */
static constexpr uint8_t kLightMax = 96;         /* WS2812 全亮太刺眼：最亮壓在 96/255 */
static constexpr uint16_t kMiredsMin = 167;      /* 色溫範圍 2000–6000K（使用者指定）：10^6/6000≈167 */
static constexpr uint16_t kMiredsMax = 500;      /* 10^6/2000＝500 */

static esp_matter_attr_val_t light_attr(uint32_t cluster, uint32_t attr)
{
    esp_matter_attr_val_t v = esp_matter_invalid(nullptr);
    attribute::get_val(attribute::get(s_ep_light, cluster, attr), &v);
    return v;
}

static void refresh_light()
{
    if (!s_ep_light || !s_cb.light) return;
    bool on = light_attr(OnOff::Id, OnOff::Attributes::OnOff::Id).val.b;
    esp_matter_attr_val_t lv = light_attr(LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    uint8_t level = (lv.type == ESP_MATTER_VAL_TYPE_INVALID || lv.val.u8 == 0xFF) ? 254 : lv.val.u8;   /* null＝最亮 */
    uint8_t mode = light_attr(ColorControl::Id, ColorControl::Attributes::ColorMode::Id).val.u8;
    uint8_t emode = light_attr(ColorControl::Id, ColorControl::Attributes::EnhancedColorMode::Id).val.u8;
    fa_rgb_t c;
    if (mode == chip::to_underlying(ColorControl::ColorModeEnum::kColorTemperatureMireds))
        c = fa_color_from_mireds(light_attr(ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id).val.u16,
                                 level, kLightMax);
    else if (mode == chip::to_underlying(ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation)) {
        /* 色盤（塗鴉等）：8-bit 色相，或 Enhanced 16-bit 色相 */
        uint16_t hue16 = emode == chip::to_underlying(ColorControl::EnhancedColorModeEnum::kEnhancedCurrentHueAndCurrentSaturation)
            ? light_attr(ColorControl::Id, ColorControl::Attributes::EnhancedCurrentHue::Id).val.u16
            : fa_color_hue8_to16(light_attr(ColorControl::Id, ColorControl::Attributes::CurrentHue::Id).val.u8);
        c = fa_color_from_hs(hue16, light_attr(ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id).val.u8,
                             level, kLightMax);
    } else
        c = fa_color_from_xy(light_attr(ColorControl::Id, ColorControl::Attributes::CurrentX::Id).val.u16,
                             light_attr(ColorControl::Id, ColorControl::Attributes::CurrentY::Id).val.u16, level, kLightMax);
    s_cb.light(on, c.r, c.g, c.b);
}
#endif

static esp_err_t on_attr(attribute::callback_type_t type, uint16_t ep, uint32_t cluster, uint32_t attr,
                         esp_matter_attr_val_t *val, void *priv)
{
#if CONFIG_FA_DEVKIT_LIGHT
    if (type == attribute::POST_UPDATE && ep == s_ep_light && s_ep_light) { refresh_light(); return ESP_OK; }
#endif
    if (type == attribute::POST_UPDATE && ep && ep == s_ep_cover && cluster == WindowCovering::Id &&
        attr == WindowCovering::Attributes::Mode::Id) {
        bool rev = val->val.u8 & chip::to_underlying(WindowCovering::Mode::kMotorDirectionReversed);
        ESP_LOGI(TAG, "窗簾：馬達方向%s", rev ? "反轉（K1＝關、K2＝開）" : "正常（K1＝開、K2＝關）");
        if (s_cb.cover_reverse) s_cb.cover_reverse(rev);
        return ESP_OK;
    }
    if (type != attribute::PRE_UPDATE || s_local) return ESP_OK;
    if (cluster == OnOff::Id && attr == OnOff::Attributes::OnOff::Id) {
        int ch = find_plug(ep);
        if (ch < 0 || !s_cb.on_set) return ESP_OK;
        /* 拒絕（同時吸合上限）就回錯誤：這次寫入不生效，controller 會看到指令失敗、屬性維持原值 */
        return s_cb.on_set((uint8_t)ch, val->val.b) ? ESP_OK : ESP_FAIL;
    }
    if (cluster == ModeSelect::Id && attr == ModeSelect::Attributes::CurrentMode::Id && ep && ep == s_ep_travel) {
        uint32_t ms = fa_cover_travel_preset_ms(val->val.u8);
        if (!ms) return ESP_FAIL;
        if (s_cb.cover_travel) s_cb.cover_travel(ms);
        return ESP_OK;
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
#if CONFIG_FA_DEVKIT_LIGHT
        refresh_light();                         /* 開機時套用上次存的燈狀態（屬性有存 NVS） */
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

extern "C" void fa_net_cover_init(uint16_t pos, uint32_t travel_ms)
{
    s_cover_pos = pos > FA_COVER_FULL ? FA_COVER_FULL : pos;
    s_cover_travel_ms = travel_ms;
}

extern "C" void fa_net_report_cover(uint16_t pos, uint16_t target)
{
    if (!s_ep_cover) return;
    lock::ScopedChipStackLock lk(portMAX_DELAY);
    /* 先寫 Target 再寫 Current：server 依兩者比較算 OperationalStatus（相等＝停止） */
    update_locked(s_ep_cover, WindowCovering::Id, WindowCovering::Attributes::TargetPositionLiftPercent100ths::Id,
                  esp_matter_nullable_uint16(target));
    update_locked(s_ep_cover, WindowCovering::Id, WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id,
                  esp_matter_nullable_uint16(pos));
}

extern "C" bool fa_net_joined(void) { return s_commissioned && s_net_up; }
extern "C" bool fa_net_commissioned(void) { return s_commissioned; }

extern "C" void fa_net_factory_reset(void)
{
    /* 只清 Matter 自己的 NVS（esp_matter、chip-config、chip-counters），不動 app 的 "fa"（NFC 金鑰等）；完成後重開機 */
    esp_matter::factory_reset();
}

/* ---------------- 建立節點 ---------------- */
/* EP0 root＋4 插座＋4 接點感測器＋12 下拉選單；esp-matter 預設上限 16，超過的 endpoint 會在執行時建立失敗 */
#if CONFIG_FA_DEVKIT_LIGHT
static constexpr int kDevkitEndpoints = 1;       /* EP21 開發板彩色燈（CONFIG_FA_DEVKIT_LIGHT） */
#else
static constexpr int kDevkitEndpoints = 0;
#endif
static constexpr int kEndpointCount = 1 + 4 + 4 + 4 * FA_SEL_KINDS + kDevkitEndpoints;
static_assert(CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT >= kEndpointCount,
              "CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT 太小（sdkconfig.defaults）");
static const char *const SEL_NAME[FA_SEL_KINDS] = {"DI%d 模式", "K%d 模式", "K%d 點動時間"};

[[maybe_unused]] static void add_label(endpoint_t *ep, int slot, const char *fmt, int n)   /* 實驗模式（只有燈）不用 */
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

#if !CONFIG_FA_DEVKIT_LIGHT_ONLY && !CONFIG_FA_DEVKIT_COVER_ONLY
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
#endif
#if CONFIG_FA_DEVKIT_LIGHT
    {
        extended_color_light::config_t c;
        c.on_off.on_off = false;
        c.on_off_lighting.start_up_on_off = nullptr;
        c.level_control.current_level = 254;
        /* on_level 保持 null：開燈時回到上次的亮度（設成 254 會每次開燈都全亮） */
        c.level_control_lighting.start_up_current_level = 254;
        c.color_control.color_mode = chip::to_underlying(ColorControl::ColorModeEnum::kCurrentXAndCurrentY);
        c.color_control.enhanced_color_mode = chip::to_underlying(ColorControl::EnhancedColorModeEnum::kCurrentXAndCurrentY);
        c.color_control_color_temperature.start_up_color_temperature_mireds = nullptr;
        c.color_control_color_temperature.color_temp_physical_min_mireds = kMiredsMin;   /* controller 的色溫滑桿依這兩個值 */
        c.color_control_color_temperature.color_temp_physical_max_mireds = kMiredsMax;
        c.color_control_color_temperature.couple_color_temp_to_level_min_mireds = kMiredsMin;
        c.color_control_color_temperature.color_temperature_mireds = 250;                  /* 4000K */
        endpoint_t *ep = extended_color_light::create(node, &c, ENDPOINT_FLAG_NONE, nullptr);
        if (!ep) ESP_LOGE(TAG, "開發板彩色燈 endpoint 建立失敗");
        else {
            /* esp-matter 的 Extended Color Light 只有 xy＋色溫；塗鴉等的色盤送色相／飽和度指令，要另外加 HS（含 Enhanced Hue） */
            cluster_t *cc = cluster::get(ep, ColorControl::Id);
            cluster::color_control::feature::hue_saturation::config_t hs;
            cluster::color_control::feature::enhanced_hue::config_t eh;
            if (cluster::color_control::feature::hue_saturation::add(cc, &hs) != ESP_OK ||
                cluster::color_control::feature::enhanced_hue::add(cc, &eh) != ESP_OK)
                ESP_LOGE(TAG, "彩色燈加色相／飽和度功能失敗");
        }
        s_ep_light = ep ? endpoint::get_id(ep) : 0;
        ESP_LOGI(TAG, "開發板彩色燈：endpoint %u", s_ep_light);
    }
#endif
#if CONFIG_FA_DEVKIT_COVER_ONLY
    {
        /* 窗簾：Drapery（HA 的 device class＝curtain）、只有升降（Lift）＋位置感知（可設百分比） */
        window_covering::config_t c(chip::to_underlying(WindowCovering::EndProductType::kCentralCurtain));
        c.window_covering.type = chip::to_underlying(WindowCovering::Type::kDrapery);
        c.window_covering.feature_flags = cluster::window_covering::feature::lift::get_id() |
                                          cluster::window_covering::feature::position_aware_lift::get_id();
        c.window_covering.features.position_aware_lift.current_position_lift_percent_100ths = s_cover_pos;
        c.window_covering.features.position_aware_lift.target_position_lift_percent_100ths = s_cover_pos;
        c.window_covering.config_status = chip::to_underlying(WindowCovering::ConfigStatus::kOperational);   /* 沒有這個 HA 顯示「設定狀態：問題」 */
        c.window_covering.delegate = &s_cover_delegate;
        endpoint_t *ep = window_covering::create(node, &c, ENDPOINT_FLAG_NONE, nullptr);
        s_ep_cover = ep ? endpoint::get_id(ep) : 0;
        if (ep) {
            s_cover_delegate.SetEndpoint(s_ep_cover);
            /* ConfigStatus／Mode 都存 NVS：開機時以存著的 Mode 為準重算 ConfigStatus（舊韌體存的值缺 Operational），
             * 並把馬達方向交給 app */
            cluster_t *wc = cluster::get(ep, WindowCovering::Id);
            esp_matter_attr_val_t mv = esp_matter_invalid(nullptr);
            attribute::get_val(attribute::get(wc, WindowCovering::Attributes::Mode::Id), &mv);
            bool rev = mv.val.u8 & chip::to_underlying(WindowCovering::Mode::kMotorDirectionReversed);
            uint8_t cs = chip::to_underlying(WindowCovering::ConfigStatus::kOperational) |
                         chip::to_underlying(WindowCovering::ConfigStatus::kLiftPositionAware) |
                         (rev ? chip::to_underlying(WindowCovering::ConfigStatus::kLiftMovementReversed) : 0);
            esp_matter_attr_val_t cv = esp_matter_bitmap8(cs);
            attribute::set_val(attribute::get(wc, WindowCovering::Attributes::ConfigStatus::Id), &cv);
            if (s_cb.cover_reverse) s_cb.cover_reverse(rev);
            /* 行程時間選單掛在同一個 endpoint 的 Mode Select cluster，不另開 endpoint：
             * 塗鴉看到的是「單一窗簾」（多一個 endpoint 會被當多功能裝置、只給通用面板）；HA 照樣建下拉選單 */
            s_modes.initTravel();
            cluster::mode_select::config_t ms;
            snprintf(ms.description, sizeof ms.description, "窗簾行程時間");
            ms.current_mode = fa_cover_travel_nearest(s_cover_travel_ms);
            ms.delegate = &s_modes;
            if (cluster::mode_select::create(ep, &ms, CLUSTER_FLAG_SERVER)) s_ep_travel = s_ep_cover;
        }
    }
    ESP_LOGW(TAG, "實驗模式：只有窗簾（endpoint %u，行程時間選單同一個 endpoint：%s）；開機位置 %u.%02u%%、行程 %lu ms",
             s_ep_cover, s_ep_travel ? "有" : "建立失敗", s_cover_pos / 100, s_cover_pos % 100, (unsigned long)s_cover_travel_ms);
#endif
    int failed = 0;
#if CONFIG_FA_DEVKIT_COVER_ONLY
    failed = !s_ep_cover + !s_ep_travel;
#elif CONFIG_FA_DEVKIT_LIGHT_ONLY
    ESP_LOGW(TAG, "實驗模式：只有彩色燈，插座／感測器／下拉選單不建立");
    failed = !s_ep_light;
#else
    for (int ch = 0; ch < 4; ch++) {
        failed += !s_ep_plug[ch] + !s_ep_contact[ch];
        for (int k = 0; k < FA_SEL_KINDS; k++) failed += !s_ep_sel[ch][k];
    }
#endif
    if (failed) ESP_LOGE(TAG, "%d 個 endpoint 建立失敗（見上方 esp-matter 錯誤），controller 會看不到這些功能", failed);
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

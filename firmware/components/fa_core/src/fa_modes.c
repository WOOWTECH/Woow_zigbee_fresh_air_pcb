#include "fa_modes.h"

const uint32_t FA_JOG_PRESET_MS[FA_JOG_PRESETS] = {
    500, 1000, 2000, 5000, 10000, 30000, 60000, 300000, 600000, 1800000, 3600000};

static const char *const DI_LABEL[] = {"全關", "按一次", "持續"};
static const char *const DO_LABEL[] = {"自鎖", "點動", "互鎖"};
static const char *const JOG_LABEL[FA_JOG_PRESETS] = {
    "0.5 秒", "1 秒", "2 秒", "5 秒", "10 秒", "30 秒", "1 分", "5 分", "10 分", "30 分", "60 分"};

uint8_t fa_sel_count(fa_sel_kind_t kind)
{
    switch (kind) {
    case FA_SEL_DI: return 3;
    case FA_SEL_DO: return 3;
    case FA_SEL_JOG: return FA_JOG_PRESETS;
    default: return 0;
    }
}

const char *fa_sel_label(fa_sel_kind_t kind, uint8_t mode)
{
    if (mode >= fa_sel_count(kind)) return "";
    switch (kind) {
    case FA_SEL_DI: return DI_LABEL[mode];
    case FA_SEL_DO: return DO_LABEL[mode];
    case FA_SEL_JOG: return JOG_LABEL[mode];
    default: return "";
    }
}

static uint8_t nearest_jog(uint32_t ms)
{
    uint8_t best = 0;
    uint32_t best_d = UINT32_MAX;
    for (uint8_t i = 0; i < FA_JOG_PRESETS; i++) {
        uint32_t p = FA_JOG_PRESET_MS[i], d = p > ms ? p - ms : ms - p;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

uint8_t fa_sel_get(const fa_io_cfg_t *c, uint8_t ch, fa_sel_kind_t kind)
{
    if (ch >= FA_CH) return 0;
    switch (kind) {
    case FA_SEL_DI: return c->di_mode[ch];
    case FA_SEL_DO: return c->do_mode[ch];
    case FA_SEL_JOG: return nearest_jog(c->jog_ms[ch]);
    default: return 0;
    }
}

bool fa_sel_set(fa_io_cfg_t *c, uint8_t ch, fa_sel_kind_t kind, uint8_t mode)
{
    if (ch >= FA_CH || mode >= fa_sel_count(kind)) return false;
    switch (kind) {
    case FA_SEL_DI: c->di_mode[ch] = mode; break;
    case FA_SEL_DO: c->do_mode[ch] = mode; break;
    case FA_SEL_JOG: c->jog_ms[ch] = FA_JOG_PRESET_MS[mode]; break;
    default: return false;
    }
    return true;
}

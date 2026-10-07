#include "fa_hvac.h"

void fa_hvac_init(fa_hvac_t *h, const fa_hvac_cfg_t *cfg)
{
    *h = (fa_hvac_t){0};
    h->cfg = *cfg;
}

static bool elapsed(bool ever, uint32_t since, uint32_t now, uint32_t ms)
{
    return !ever || (uint32_t)(now - since) >= ms;
}

static void set_heat(fa_hvac_t *h, bool on, uint32_t now)
{
    if (h->heat == on) return;
    h->heat = on;
    h->heat_ever = true;
    h->heat_ms = now;
    if (!on) { h->purging = true; h->purge_until = now + h->cfg.purge_ms; }   /* 電熱關掉後要吹風散熱 */
}

static void set_cool(fa_hvac_t *h, bool on, uint32_t now)
{
    if (h->cool == on) return;
    h->cool = on;
    h->cool_ever = true;
    h->cool_ms = now;
}

fa_hvac_out_t fa_hvac_tick(fa_hvac_t *h, const fa_hvac_cmd_t *c, int16_t t, uint32_t now)
{
    const int16_t hy = h->cfg.hyst;
    const uint32_t mc = h->cfg.min_cycle_ms;
    bool known = t != FA_HVAC_TEMP_UNKNOWN;
    bool want_heat = c->power && c->mode == FA_HVAC_MODE_HEAT && known;
    bool want_cool = c->power && c->mode == FA_HVAC_MODE_COOL && known;

    /* 使用者關機、切模式、溫度未知：立刻關（最少開時間只約束溫控本身的開停） */
    if (h->heat && !want_heat) set_heat(h, false, now);
    if (h->cool && !want_cool) set_cool(h, false, now);

    if (want_heat) {
        if (!h->heat && t <= c->heat_sp - hy && elapsed(h->heat_ever, h->heat_ms, now, mc)) set_heat(h, true, now);
        else if (h->heat && t >= c->heat_sp + hy && elapsed(h->heat_ever, h->heat_ms, now, mc)) set_heat(h, false, now);
    }
    if (want_cool) {
        if (!h->cool && t >= c->cool_sp + hy && elapsed(h->cool_ever, h->cool_ms, now, mc)) set_cool(h, true, now);
        else if (h->cool && t <= c->cool_sp - hy && elapsed(h->cool_ever, h->cool_ms, now, mc)) set_cool(h, false, now);
    }
    if (h->purging && (int32_t)(now - h->purge_until) >= 0) h->purging = false;

    /* 風速 */
    bool demand = h->heat || h->cool;
    uint8_t fan = 0;
    if (c->power) {
        switch (c->fan_mode) {
        case FA_FAN_LOW:  fan = 1; break;
        case FA_FAN_MED:  fan = 2; break;
        case FA_FAN_HIGH:
        case FA_FAN_ON:   fan = 3; break;
        case FA_FAN_AUTO: {
            uint8_t want = 1;                          /* 沒需求：低速換氣 */
            if (demand) {
                int32_t sp = h->heat ? c->heat_sp : c->cool_sp;
                int32_t d = t > sp ? t - sp : sp - t;
                want = d >= 200 ? 3 : d >= 100 ? 2 : 1;
            }
            if (!h->auto_fan || (want != h->auto_fan && (uint32_t)(now - h->auto_fan_ms) >= FA_HVAC_AUTO_FAN_HOLD_MS)) {
                h->auto_fan = want;
                h->auto_fan_ms = now;
            }
            fan = h->auto_fan;
            break;
        }
        default: fan = 0; break;
        }
    }
    if (!c->power || c->fan_mode != FA_FAN_AUTO) h->auto_fan = 0;   /* 離開自動：下次回自動立刻取當下值 */
    if ((demand || h->purging) && fan == 0) fan = 1;  /* 加熱／製冷／散熱中一定要有風 */

    fa_hvac_out_t o = {.heat = h->heat, .cool = h->cool, .fan = fan, .running = 0};
    if (h->heat) o.running |= FA_RUN_HEAT;
    if (h->cool) o.running |= FA_RUN_COOL;
    if (fan) o.running |= FA_RUN_FAN;
    if (fan == 2) o.running |= FA_RUN_FAN2;
    if (fan == 3) o.running |= FA_RUN_FAN3;
    return o;
}

fa_rgb_t fa_hvac_rgb(const fa_hvac_out_t *o, uint8_t max)
{
    uint8_t v = o->fan >= 3 ? max : o->fan == 2 ? 50 : o->fan == 1 ? 20 : 0;
    if (!v && (o->heat || o->cool)) v = 20;
    if (o->heat) return (fa_rgb_t){v, 0, 0};
    if (o->cool) return (fa_rgb_t){0, 0, v};
    return (fa_rgb_t){0, v, 0};
}

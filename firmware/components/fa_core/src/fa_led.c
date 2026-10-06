#include "fa_led.h"
#include "fa_button.h"

/* 兩短一長：亮 150、滅 200、亮 150、滅 200、亮 600、滅 700（共 2000ms） */
static bool beacon(uint32_t t)
{
    uint32_t p = t % FA_LED_BEACON_PERIOD_MS;
    return p < 150 || (p >= 350 && p < 500) || (p >= 700 && p < 1300);
}

bool fa_led_level(const fa_led_in_t *in, uint32_t t)
{
    if (in->held_ms >= FA_BTN_VLONG_MIN) return true;
    if (in->held_ms >= FA_BTN_LONG_MIN) return (t / 100) % 2;
    if (in->learning) return (t / 100) % 2;
    if (in->identifying) return (t / 50) % 2;
    switch (in->net) {
    case FA_LED_NET_UNPAIRED: return (t / 500) % 2;
    case FA_LED_NET_OFFLINE:  return beacon(t);
    default:                  return true;
    }
}

fa_rgb_t fa_led_rgb(bool level, const bool on[4])
{
    fa_rgb_t c = {0, 0, 0};
    if (!level) return c;
    if (!on[0] && !on[1] && !on[2] && !on[3]) return (fa_rgb_t){6, 6, 6};
    if (on[0]) c.r += 40;
    if (on[1]) c.g += 40;
    if (on[2]) c.b += 40;
    if (on[3]) { c.r += 15; c.g += 15; c.b += 15; }
    return c;
}

void fa_led_wire_bytes(fa_rgb_t c, fa_led_order_t order, uint8_t out[3])
{
    out[0] = order == FA_LED_ORDER_RGB ? c.r : c.g;
    out[1] = order == FA_LED_ORDER_RGB ? c.g : c.r;
    out[2] = c.b;
}

bool fa_led_rgb_status_lit(const fa_led_in_t *in, uint32_t t, const bool on[4])
{
    bool any = on[0] || on[1] || on[2] || on[3];
    bool special = in->held_ms >= FA_BTN_LONG_MIN || in->learning || in->identifying || in->net != FA_LED_NET_ONLINE;
    return (any || special) && fa_led_level(in, t);
}

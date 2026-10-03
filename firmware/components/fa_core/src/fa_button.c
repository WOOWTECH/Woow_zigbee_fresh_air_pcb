#include "fa_button.h"

void fa_button_init(fa_button_t *b)
{
    *b = (fa_button_t){0};
}

fa_btn_evt_t fa_button_update(fa_button_t *b, bool pressed, uint32_t now_ms)
{
    if (pressed == b->pressed) return FA_BTN_NONE;
    b->pressed = pressed;
    uint32_t held = now_ms - b->since_ms;
    b->since_ms = now_ms;
    if (pressed) return FA_BTN_NONE;
    if (held < FA_BTN_DEBOUNCE_MS) return FA_BTN_NONE;
    if (held <= FA_BTN_SHORT_MAX) return FA_BTN_SHORT;
    if (held >= FA_BTN_LONG_MIN && held <= FA_BTN_LONG_MAX) return FA_BTN_LONG;
    if (held >= FA_BTN_VLONG_MIN) return FA_BTN_VLONG;
    return FA_BTN_NONE;
}

uint32_t fa_button_held_ms(const fa_button_t *b, uint32_t now_ms)
{
    return b->pressed ? now_ms - b->since_ms : 0;
}

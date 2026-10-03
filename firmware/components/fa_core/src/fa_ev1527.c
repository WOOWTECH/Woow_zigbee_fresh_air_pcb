#include "fa_ev1527.h"

#define T_MIN_US   150
#define T_MAX_US   800
#define SYNC_RATIO 20     /* 同步低電位至少 20T（標準 31T；尾端閒置也視為同步） */

void fa_ev1527_init(fa_ev1527_t *d)
{
    *d = (fa_ev1527_t){0};
}

static bool near(uint32_t v, uint32_t target)        /* ±50% */
{
    return v * 2 >= target && v * 2 <= target * 3;
}

/* 一幀收完：與上一幀相同才回報 */
static bool finish(fa_ev1527_t *d, uint32_t *code)
{
    bool ok = d->have_prev && d->prev == d->bits;
    d->prev = d->bits;
    d->have_prev = true;
    if (ok) *code = d->bits;
    return ok;
}

bool fa_ev1527_feed(fa_ev1527_t *d, bool level_high, uint32_t dur_us, uint32_t *code)
{
    if (level_high) {
        d->high_us = dur_us;
        return false;
    }
    uint32_t h = d->high_us, l = dur_us;
    d->high_us = 0;
    bool ret = false;

    if (h >= T_MIN_US && h <= T_MAX_US && l >= SYNC_RATIO * h) {      /* 同步 */
        if (d->in_frame && d->nbits == 24) ret = finish(d, code);
        else if (d->in_frame) d->have_prev = false;                    /* 半截幀：重新配對 */
        d->in_frame = true;
        d->t_us = h;
        d->bits = 0;
        d->nbits = 0;
        return ret;
    }
    if (!d->in_frame) return false;

    uint32_t T = d->t_us;
    int bit = -1;
    /* 0/1 用高低比例判（標準 1:3 / 3:1，比例 ≥1.8 才算），週期要接近 4T：對整體時脈偏差與抖動都不敏感 */
    if (near(h + l, 4 * T)) {
        if (l * 10 >= h * 18) bit = 0;
        else if (h * 10 >= l * 18) bit = 1;
    }
    if (bit < 0 || d->nbits >= 24) {                                   /* 不像 EV1527：丟掉 */
        d->in_frame = false;
        d->have_prev = false;
        return false;
    }
    d->bits = (d->bits << 1) | (uint32_t)bit;
    d->nbits++;
    return false;
}

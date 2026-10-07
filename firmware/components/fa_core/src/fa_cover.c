#include "fa_cover.h"
#include <stdio.h>

static uint32_t overrun_ms(const fa_cover_t *c)
{
    uint32_t o = c->travel_ms / 10;
    return o < 1000 ? 1000 : o;
}

static bool is_end(int32_t p) { return p == 0 || p == FA_COVER_FULL; }

/* 依「這段開始的時間與位置」算目前位置（只在 motor 有在跑時有意義） */
static int32_t pos_at(const fa_cover_t *c, uint32_t now)
{
    if (c->motor == FA_COVER_STOP || c->overrun) return c->pos;
    int32_t d = (int32_t)((uint64_t)(now - c->seg_ms) * FA_COVER_FULL / c->travel_ms);
    int32_t p = c->motor == FA_COVER_OPENING ? c->seg_pos - d : c->seg_pos + d;
    return p < 0 ? 0 : p > FA_COVER_FULL ? FA_COVER_FULL : p;
}

static void halt(fa_cover_t *c, uint32_t now)
{
    if (c->motor != FA_COVER_STOP) {
        c->pos = pos_at(c, now);
        c->last_dir = c->motor;
        c->stopped_ms = now;
    }
    c->motor = FA_COVER_STOP;
    c->pending = FA_COVER_STOP;
    c->overrun = false;
}

static void start(fa_cover_t *c, fa_cover_motor_t dir, uint32_t now)
{
    c->motor = dir;
    c->pending = FA_COVER_STOP;
    c->seg_ms = now;
    c->seg_pos = c->pos;
    c->overrun = false;
    if (c->pos == c->target) {                     /* 已在端點：直接 overrun 校正 */
        c->overrun = true;
        c->overrun_until = now + overrun_ms(c);
    }
}

void fa_cover_init(fa_cover_t *c, uint32_t travel_ms, uint16_t pos)
{
    *c = (fa_cover_t){0};
    c->travel_ms = travel_ms ? travel_ms : FA_COVER_TRAVEL_DEFAULT_MS;
    c->pos = c->target = pos > FA_COVER_FULL ? FA_COVER_FULL : pos;
}

void fa_cover_set_travel(fa_cover_t *c, uint32_t travel_ms, uint32_t now)
{
    if (!travel_ms) return;
    if (c->motor != FA_COVER_STOP && !c->overrun) {     /* 跑到一半換行程：從現在的位置重新起算這段 */
        c->pos = pos_at(c, now);
        c->seg_pos = c->pos;
        c->seg_ms = now;
    }
    c->travel_ms = travel_ms;
}

void fa_cover_goto(fa_cover_t *c, uint16_t target, uint32_t now)
{
    int32_t t = target > FA_COVER_FULL ? FA_COVER_FULL : target;
    if (c->motor != FA_COVER_STOP) c->pos = pos_at(c, now);
    c->target = t;
    fa_cover_motor_t dir = t < c->pos ? FA_COVER_OPENING : t > c->pos ? FA_COVER_CLOSING
                         : !is_end(t) ? FA_COVER_STOP : t == 0 ? FA_COVER_OPENING : FA_COVER_CLOSING;
    if (dir == FA_COVER_STOP) { halt(c, now); return; }
    if (c->motor == dir) {                          /* 同方向：只改目標，不停車 */
        if (!c->overrun) { c->seg_pos = c->pos; c->seg_ms = now; }
        else if (c->pos != t) start(c, dir, now);   /* overrun 中又要往回一點：重新起算 */
        return;
    }
    fa_cover_motor_t prev = c->motor != FA_COVER_STOP ? c->motor : c->last_dir;
    uint32_t since = c->motor != FA_COVER_STOP ? 0 : now - c->stopped_ms;
    halt(c, now);
    if (prev != FA_COVER_STOP && prev != dir && since < FA_COVER_DEAD_MS) {
        c->pending = dir;                          /* 反方向：先停，等死區 */
        c->dead_until = now + (FA_COVER_DEAD_MS - since);
    } else {
        start(c, dir, now);
    }
}

void fa_cover_stop(fa_cover_t *c, uint32_t now)
{
    halt(c, now);
    c->target = c->pos;
}

fa_cover_motor_t fa_cover_tick(fa_cover_t *c, uint32_t now)
{
    if (c->pending != FA_COVER_STOP) {
        if ((int32_t)(now - c->dead_until) >= 0) start(c, c->pending, now);
        else return FA_COVER_STOP;
    }
    if (c->motor == FA_COVER_STOP) return FA_COVER_STOP;
    if (c->overrun) {
        if ((int32_t)(now - c->overrun_until) >= 0) halt(c, now);
        return c->motor;
    }
    int32_t p = pos_at(c, now);
    bool reached = c->motor == FA_COVER_OPENING ? p <= c->target : p >= c->target;
    if (!reached) { c->pos = p; return c->motor; }
    c->pos = c->target;
    if (is_end(c->target)) {                      /* 端點：多跑一段把誤差歸零 */
        c->overrun = true;
        c->overrun_until = now + overrun_ms(c);
        return c->motor;
    }
    c->overrun = true;                            /* 讓 halt 不再用時間重算位置（會差 1–2 個單位） */
    halt(c, now);
    return FA_COVER_STOP;
}

uint16_t fa_cover_pos(const fa_cover_t *c) { return (uint16_t)c->pos; }

bool fa_cover_busy(const fa_cover_t *c) { return c->motor != FA_COVER_STOP || c->pending != FA_COVER_STOP; }

void fa_cover_cycle(fa_cover_t *c, uint32_t now)
{
    if (fa_cover_busy(c)) { fa_cover_stop(c, now); return; }
    bool open_next = c->pos == FA_COVER_FULL || (c->last_dir != FA_COVER_OPENING && c->pos != 0);
    fa_cover_goto(c, open_next ? 0 : FA_COVER_FULL, now);
}

uint32_t fa_cover_travel_preset_ms(uint8_t i)
{
    if (i < 56) return (5u + i) * 1000u;              /* 5–60 秒 */
    if (i < 68) return (65u + (i - 56u) * 5u) * 1000u; /* 65–120 秒 */
    if (i == 68) return 150000;
    if (i == 69) return 180000;
    return 0;
}

uint8_t fa_cover_travel_nearest(uint32_t ms)
{
    uint8_t best = 0;
    uint32_t bd = UINT32_MAX;
    for (uint8_t i = 0; i < FA_COVER_TRAVEL_PRESETS; i++) {
        uint32_t p = fa_cover_travel_preset_ms(i), d = p > ms ? p - ms : ms - p;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void fa_cover_travel_label(uint8_t i, char out[12])
{
    snprintf(out, 12, "%u 秒", (unsigned)(fa_cover_travel_preset_ms(i) / 1000));
}

fa_rgb_t fa_cover_rgb(fa_cover_motor_t motor, uint16_t pos, uint8_t max)
{
    if (motor == FA_COVER_OPENING) return (fa_rgb_t){0, max, 0};
    if (motor == FA_COVER_CLOSING) return (fa_rgb_t){max, 0, 0};
    uint32_t lo = 4, open = FA_COVER_FULL - (pos > FA_COVER_FULL ? FA_COVER_FULL : pos);
    uint8_t v = (uint8_t)(lo + (uint32_t)(max > lo ? max - lo : 0) * open / FA_COVER_FULL);
    return (fa_rgb_t){v, v, v};
}

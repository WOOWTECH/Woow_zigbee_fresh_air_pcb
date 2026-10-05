#include "fa_io.h"

void fa_io_default(fa_io_cfg_t *c, fa_mode_t preset)
{
    uint8_t group = fa_mode_group_mask(preset);
    for (int k = 0; k < FA_CH; k++) {
        c->di_mode[k] = FA_DI_HOLD;
        c->do_mode[k] = (group & (1u << k)) ? FA_DO_INTERLOCK : FA_DO_LATCH;
        c->jog_ms[k] = 1000;
    }
}

bool fa_io_cfg_valid(const fa_io_cfg_t *c)
{
    for (int k = 0; k < FA_CH; k++) {
        if (c->di_mode[k] > FA_DI_HOLD || c->do_mode[k] > FA_DO_INTERLOCK) return false;
        if (c->jog_ms[k] < 500 || c->jog_ms[k] > 3600000u) return false;
    }
    return true;
}

uint8_t fa_io_interlock_mask(const fa_io_cfg_t *c)
{
    uint8_t m = 0;
    for (int k = 0; k < FA_CH; k++)
        if (c->do_mode[k] == FA_DO_INTERLOCK) m |= 1u << k;
    return m;
}

void fa_io_init(fa_io_t *io, const fa_io_cfg_t *c)
{
    *io = (fa_io_t){.cfg = *c};
}

static int set(fa_relays_t *r, uint8_t ch, bool on, fa_action_t *out, int max_out)
{
    return fa_relays_set(r, ch, on, out, max_out);
}

/* DI k（接線或遙控器）合併狀態改變時：依 DI／DO 模式決定繼電器 k 怎麼動 */
static int on_edge(fa_io_t *io, fa_relays_t *r, uint8_t k, bool closed, uint32_t now, fa_action_t *out, int max_out)
{
    uint8_t di = io->cfg.di_mode[k], dout = io->cfg.do_mode[k];
    if (di == FA_DI_OFF) return 0;
    if (di == FA_DI_HOLD) {                         /* 持續：三種 DO 都是 接通＝開、斷開＝關 */
        io->jog_run[k] = false;
        return set(r, k, closed, out, max_out);
    }
    if (!closed) return 0;                          /* 按一次：只看接通那一刻 */
    switch (dout) {
    case FA_DO_JOG:
        io->jog_run[k] = true;
        io->jog_until[k] = now + io->cfg.jog_ms[k];
        return set(r, k, true, out, max_out);
    case FA_DO_INTERLOCK:
    case FA_DO_LATCH:
    default:
        return set(r, k, !r->on[k], out, max_out);
    }
}

static int update(fa_io_t *io, fa_relays_t *r, uint8_t k, uint32_t now, fa_action_t *out, int max_out)
{
    bool a = io->wired[k] || io->rf[k];
    if (a == io->active[k]) return 0;
    io->active[k] = a;
    return on_edge(io, r, k, a, now, out, max_out);
}

int fa_io_wired(fa_io_t *io, fa_relays_t *r, uint8_t ch, bool closed, uint32_t now_ms, fa_action_t *out, int max_out)
{
    if (ch >= FA_CH) return FA_ERR_ARG;
    if (closed != io->deb_raw[ch]) {
        io->deb_raw[ch] = closed;
        io->deb_cnt[ch] = 1;
        return 0;
    }
    if (io->deb_cnt[ch] < FA_IO_DEBOUNCE_SAMPLES) io->deb_cnt[ch]++;
    if (io->deb_cnt[ch] < FA_IO_DEBOUNCE_SAMPLES || io->wired[ch] == closed) return 0;
    io->wired[ch] = closed;
    return update(io, r, ch, now_ms, out, max_out);
}

int fa_io_rf(fa_io_t *io, fa_relays_t *r, uint8_t ch, uint32_t now_ms, fa_action_t *out, int max_out)
{
    if (ch >= FA_CH) return FA_ERR_ARG;
    io->rf_last[ch] = now_ms;
    if (io->rf[ch]) return 0;
    io->rf[ch] = true;
    return update(io, r, ch, now_ms, out, max_out);
}

int fa_io_tick(fa_io_t *io, fa_relays_t *r, uint32_t now_ms, fa_action_t *out, int max_out)
{
    int n = 0;
    for (uint8_t k = 0; k < FA_CH; k++) {
        if (io->rf[k] && (uint32_t)(now_ms - io->rf_last[k]) > FA_IO_RF_RELEASE_MS) {
            io->rf[k] = false;
            int m = update(io, r, k, now_ms, out + n, max_out - n);
            if (m > 0) n += m;
        }
        if (io->jog_run[k] && (int32_t)(now_ms - io->jog_until[k]) >= 0) {
            io->jog_run[k] = false;
            int m = set(r, k, false, out + n, max_out - n);
            if (m > 0) n += m;
        }
    }
    return n;
}

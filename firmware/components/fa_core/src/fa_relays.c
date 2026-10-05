#include "fa_relays.h"

fa_mode_t fa_mode_from_dip(bool bit1, bool bit0)
{
    return (fa_mode_t)((bit1 ? 2 : 0) | (bit0 ? 1 : 0));
}

uint8_t fa_mode_group_mask(fa_mode_t mode)
{
    switch (mode) {
    case FA_MODE_FAN3: return 0x7;
    case FA_MODE_FAN2: return 0x3;
    case FA_MODE_SEL4: return 0xF;
    default:           return 0x0;
    }
}

void fa_relays_init(fa_relays_t *r, fa_mode_t mode, uint8_t max_on, uint16_t dead_ms)
{
    *r = (fa_relays_t){.mode = mode, .max_on = max_on, .dead_ms = dead_ms, .group = fa_mode_group_mask(mode)};
}

void fa_relays_init_group(fa_relays_t *r, uint8_t group_mask, uint8_t max_on, uint16_t dead_ms)
{
    *r = (fa_relays_t){.mode = FA_MODE_4CH, .max_on = max_on, .dead_ms = dead_ms, .group = group_mask & 0xF};
}

static int push(fa_action_t *out, int n, int max_out, uint8_t ch, bool on, uint32_t delay)
{
    if (n < max_out) out[n] = (fa_action_t){.ch = ch, .on = on, .delay_ms = delay};
    return n + 1;
}

int fa_relays_set(fa_relays_t *r, uint8_t ch, bool on, fa_action_t *out, int max_out)
{
    if (ch >= FA_CH) return FA_ERR_ARG;
    if (r->on[ch] == on) return 0;
    if (!on) {
        r->on[ch] = false;
        return push(out, 0, max_out, ch, false, 0);
    }
    uint8_t group = r->group;
    bool    in_group = group & (1u << ch);
    int     count = 0, freed = 0;
    for (int i = 0; i < FA_CH; i++) {
        if (!r->on[i]) continue;
        count++;
        if (in_group && (group & (1u << i))) freed++;
    }
    if (count - freed + 1 > r->max_on) return FA_ERR_LIMIT;
    int n = 0;
    if (in_group) {
        for (int i = 0; i < FA_CH; i++) {
            if (i != ch && r->on[i] && (group & (1u << i))) {
                r->on[i] = false;
                n = push(out, n, max_out, i, false, 0);
            }
        }
    }
    r->on[ch] = true;
    return push(out, n, max_out, ch, true, n ? r->dead_ms : 0);
}

int fa_relays_toggle(fa_relays_t *r, uint8_t ch, fa_action_t *out, int max_out)
{
    if (ch >= FA_CH) return FA_ERR_ARG;
    return fa_relays_set(r, ch, !r->on[ch], out, max_out);
}

int fa_relays_cycle(fa_relays_t *r, fa_action_t *out, int max_out)
{
    uint8_t group = r->group;
    if (!group) return fa_relays_toggle(r, 0, out, max_out);
    int members[FA_CH], m = 0, cur = -1;
    for (int i = 0; i < FA_CH; i++) {
        if (group & (1u << i)) {
            if (r->on[i]) cur = m;
            members[m++] = i;
        }
    }
    int next = cur + 1;                       /* 關 → 1 → 2 → … → 關 */
    if (next >= m) return fa_relays_set(r, members[cur], false, out, max_out);
    return fa_relays_set(r, members[next], true, out, max_out);
}

int fa_relays_all_off(fa_relays_t *r, fa_action_t *out, int max_out)
{
    int n = 0;
    for (int i = 0; i < FA_CH; i++) {
        if (r->on[i]) {
            r->on[i] = false;
            n = push(out, n, max_out, i, false, 0);
        }
    }
    return n;
}

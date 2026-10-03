/* fa_core 主機端單元測試：make -C firmware/test */
#include "unity_lite.h"
#include "fa_relays.h"
#include "fa_ev1527.h"
#include "fa_button.h"
#include "fa_remotes.h"

int ul_fail, ul_run;

/* ---------------- 繼電器：模式、互鎖、上限 ---------------- */
TEST(mode_from_dip)
{
    CHECK_EQ(fa_mode_from_dip(false, false), FA_MODE_4CH);
    CHECK_EQ(fa_mode_from_dip(false, true), FA_MODE_FAN3);   /* bit1, bit0 */
    CHECK_EQ(fa_mode_from_dip(true, false), FA_MODE_FAN2);
    CHECK_EQ(fa_mode_from_dip(true, true), FA_MODE_SEL4);
}

TEST(independent_channels_do_not_interlock)
{
    fa_relays_t r; fa_action_t a[8];
    fa_relays_init(&r, FA_MODE_4CH, 4, 100);
    CHECK_EQ(fa_relays_set(&r, 0, true, a, 8), 1);
    CHECK_EQ(a[0].ch, 0); CHECK(a[0].on); CHECK_EQ(a[0].delay_ms, 0);
    CHECK_EQ(fa_relays_set(&r, 1, true, a, 8), 1);
    CHECK(r.on[0] && r.on[1]);
}

TEST(fan3_switching_speed_turns_other_off_first_with_dead_time)
{
    fa_relays_t r; fa_action_t a[8];
    fa_relays_init(&r, FA_MODE_FAN3, 4, 100);
    fa_relays_set(&r, 0, true, a, 8);
    int n = fa_relays_set(&r, 2, true, a, 8);
    CHECK_EQ(n, 2);
    CHECK_EQ(a[0].ch, 0); CHECK(!a[0].on); CHECK_EQ(a[0].delay_ms, 0);
    CHECK_EQ(a[1].ch, 2); CHECK(a[1].on); CHECK_EQ(a[1].delay_ms, 100);
    CHECK(!r.on[0] && r.on[2]);
}

TEST(fan3_channel4_is_independent)
{
    fa_relays_t r; fa_action_t a[8];
    fa_relays_init(&r, FA_MODE_FAN3, 4, 100);
    fa_relays_set(&r, 1, true, a, 8);
    CHECK_EQ(fa_relays_set(&r, 3, true, a, 8), 1);
    CHECK(r.on[1] && r.on[3]);
}

TEST(fan2_group_is_ch1_ch2_only)
{
    fa_relays_t r; fa_action_t a[8];
    fa_relays_init(&r, FA_MODE_FAN2, 4, 100);
    fa_relays_set(&r, 0, true, a, 8);
    fa_relays_set(&r, 2, true, a, 8);
    CHECK_EQ(fa_relays_set(&r, 1, true, a, 8), 2);
    CHECK(!r.on[0] && r.on[1] && r.on[2]);
}

TEST(max_on_limit_refuses_extra_relay)
{
    fa_relays_t r; fa_action_t a[8];
    fa_relays_init(&r, FA_MODE_4CH, 3, 100);
    fa_relays_set(&r, 0, true, a, 8);
    fa_relays_set(&r, 1, true, a, 8);
    fa_relays_set(&r, 2, true, a, 8);
    CHECK_EQ(fa_relays_set(&r, 3, true, a, 8), FA_ERR_LIMIT);
    CHECK(!r.on[3]);
}

TEST(set_same_state_is_noop_and_bad_channel_rejected)
{
    fa_relays_t r; fa_action_t a[8];
    fa_relays_init(&r, FA_MODE_4CH, 4, 100);
    CHECK_EQ(fa_relays_set(&r, 0, false, a, 8), 0);
    CHECK_EQ(fa_relays_set(&r, 4, true, a, 8), FA_ERR_ARG);
}

TEST(cycle_fan3_goes_off_1_2_3_off)
{
    fa_relays_t r; fa_action_t a[8];
    fa_relays_init(&r, FA_MODE_FAN3, 4, 100);
    fa_relays_cycle(&r, a, 8); CHECK(r.on[0]);
    fa_relays_cycle(&r, a, 8); CHECK(!r.on[0] && r.on[1]);
    fa_relays_cycle(&r, a, 8); CHECK(!r.on[1] && r.on[2]);
    fa_relays_cycle(&r, a, 8); CHECK(!r.on[0] && !r.on[1] && !r.on[2]);
}

TEST(cycle_4ch_toggles_channel1)
{
    fa_relays_t r; fa_action_t a[8];
    fa_relays_init(&r, FA_MODE_4CH, 4, 100);
    fa_relays_cycle(&r, a, 8); CHECK(r.on[0]);
    fa_relays_cycle(&r, a, 8); CHECK(!r.on[0]);
}

TEST(all_off)
{
    fa_relays_t r; fa_action_t a[8];
    fa_relays_init(&r, FA_MODE_4CH, 4, 100);
    fa_relays_set(&r, 0, true, a, 8); fa_relays_set(&r, 3, true, a, 8);
    CHECK_EQ(fa_relays_all_off(&r, a, 8), 2);
    CHECK(!r.on[0] && !r.on[3]);
}

/* ---------------- EV1527 解碼 ---------------- */
static int jitter_pct;                                    /* 每個脈寬 ±jitter_pct% 交替抖動 */
static uint32_t J(uint32_t us) { static int s = 1; s = -s; return us + (int)us * jitter_pct * s / 100; }

static int feed_frame(fa_ev1527_t *d, uint32_t code, uint32_t T, int bits_lost_at_end, uint32_t *out)
{
    int got = 0;
    uint32_t c;
    if (fa_ev1527_feed(d, true, J(T), &c)) { *out = c; got++; }       /* 同步：1T 高 */
    if (fa_ev1527_feed(d, false, J(31 * T), &c)) { *out = c; got++; } /*       31T 低 */
    for (int i = 23; i >= bits_lost_at_end; i--) {
        bool b = (code >> i) & 1;
        if (fa_ev1527_feed(d, true, J(b ? 3 * T : T), &c)) { *out = c; got++; }
        if (fa_ev1527_feed(d, false, J(b ? T : 3 * T), &c)) { *out = c; got++; }
    }
    return got;
}

TEST(ev1527_decodes_after_two_identical_frames)
{
    fa_ev1527_t d; fa_ev1527_init(&d);
    uint32_t out = 0;
    feed_frame(&d, 0xA5C3E8, 350, 0, &out);
    feed_frame(&d, 0xA5C3E8, 350, 0, &out);
    int got = feed_frame(&d, 0xA5C3E8, 350, 0, &out);      /* 第 2 個同步時確認第 2 幀 */
    CHECK(got >= 1);
    CHECK_EQ(out, 0xA5C3E8);
}

TEST(ev1527_tolerates_timing_jitter_and_other_T)
{
    fa_ev1527_t d; fa_ev1527_init(&d);
    uint32_t out = 0;
    jitter_pct = 20;
    for (int k = 0; k < 3; k++) feed_frame(&d, 0x123458, 480, 0, &out);
    jitter_pct = 0;
    CHECK_EQ(out, 0x123458);
}

TEST(ev1527_rejects_single_frame_and_noise)
{
    fa_ev1527_t d; fa_ev1527_init(&d);
    uint32_t c, out = 0;
    feed_frame(&d, 0x0F0F01, 350, 0, &out);
    /* 雜訊：亂長度 */
    uint32_t noise[] = {120, 60, 900, 40, 3000, 200, 75, 1300, 33, 500};
    int got = 0;
    for (int i = 0; i < 10; i++) got += fa_ev1527_feed(&d, i & 1 ? false : true, noise[i], &c);
    feed_frame(&d, 0x0F0F02, 350, 0, &out);                /* 不同碼 */
    got += feed_frame(&d, 0x0F0F03, 350, 0, &out);
    CHECK_EQ(got, 0);
}

TEST(ev1527_split_helpers)
{
    CHECK_EQ(fa_ev1527_addr(0xA5C3E8), 0xA5C3E);
    CHECK_EQ(fa_ev1527_key(0xA5C3E8), 0x8);
}

/* ---------------- 遙控器按鍵對應、學習表 ---------------- */
TEST(key_to_channel_single_bit_msb_first)
{
    CHECK_EQ(fa_remote_key_to_channel(0x8), 0);   /* A */
    CHECK_EQ(fa_remote_key_to_channel(0x4), 1);   /* B */
    CHECK_EQ(fa_remote_key_to_channel(0x2), 2);   /* C */
    CHECK_EQ(fa_remote_key_to_channel(0x1), 3);   /* D */
    CHECK_EQ(fa_remote_key_to_channel(0x3), -1);
    CHECK_EQ(fa_remote_key_to_channel(0x0), -1);
}

TEST(remotes_learn_dedupe_and_fifo)
{
    fa_remotes_t t; fa_remotes_init(&t);
    CHECK(!fa_remotes_has(&t, 0x11111));
    fa_remotes_add(&t, 0x11111);
    fa_remotes_add(&t, 0x11111);
    CHECK_EQ(t.n, 1);
    for (uint32_t i = 0; i < FA_REMOTES_MAX; i++) fa_remotes_add(&t, 0x20000 + i);
    CHECK_EQ(t.n, FA_REMOTES_MAX);
    CHECK(!fa_remotes_has(&t, 0x11111));                   /* 最舊的被擠掉 */
    CHECK(fa_remotes_has(&t, 0x20000 + FA_REMOTES_MAX - 1));
}

/* ---------------- 按鍵 ---------------- */
static fa_btn_evt_t press(fa_button_t *b, uint32_t *t, uint32_t hold_ms)
{
    fa_btn_evt_t e = FA_BTN_NONE;
    for (uint32_t i = 0; i < hold_ms; i += 10) { *t += 10; fa_button_update(b, true, *t); }
    for (int i = 0; i < 10; i++) { *t += 10; fa_btn_evt_t x = fa_button_update(b, false, *t); if (x) e = x; }
    return e;
}

TEST(button_classifies_press_lengths)
{
    fa_button_t b; uint32_t t = 0; fa_button_init(&b);
    CHECK_EQ(press(&b, &t, 20), FA_BTN_NONE);              /* 彈跳 */
    CHECK_EQ(press(&b, &t, 200), FA_BTN_SHORT);
    CHECK_EQ(press(&b, &t, 4000), FA_BTN_LONG);
    CHECK_EQ(press(&b, &t, 11000), FA_BTN_VLONG);
    CHECK_EQ(press(&b, &t, 1800), FA_BTN_NONE);            /* 1–3 秒之間：不動作 */
}

int main(void)
{
    RUN(mode_from_dip);
    RUN(independent_channels_do_not_interlock);
    RUN(fan3_switching_speed_turns_other_off_first_with_dead_time);
    RUN(fan3_channel4_is_independent);
    RUN(fan2_group_is_ch1_ch2_only);
    RUN(max_on_limit_refuses_extra_relay);
    RUN(set_same_state_is_noop_and_bad_channel_rejected);
    RUN(cycle_fan3_goes_off_1_2_3_off);
    RUN(cycle_4ch_toggles_channel1);
    RUN(all_off);
    RUN(ev1527_decodes_after_two_identical_frames);
    RUN(ev1527_tolerates_timing_jitter_and_other_T);
    RUN(ev1527_rejects_single_frame_and_noise);
    RUN(ev1527_split_helpers);
    RUN(key_to_channel_single_bit_msb_first);
    RUN(remotes_learn_dedupe_and_fifo);
    RUN(button_classifies_press_lengths);
    printf("%d tests, %d failures\n", ul_run, ul_fail);
    return ul_fail ? 1 : 0;
}

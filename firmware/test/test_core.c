/* fa_core 主機端單元測試：make -C firmware/test */
#include "unity_lite.h"
#include "fa_relays.h"
#include "fa_ev1527.h"
#include "fa_button.h"
#include "fa_remotes.h"
#include "fa_coil.h"
#include "fa_io.h"
#include "fa_sha256.h"
#include "fa_nfc.h"
#include "fa_modes.h"
#include "fa_led.h"
#include "fa_color.h"
#include <string.h>

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

/* ---------------- 線圈 PWM 保持與 12V 電源預算 ---------------- */
static const fa_coil_cfg_t COIL = {.pullin_ms = 100, .hold_pct = 60, .supply_mv = 12000,
                                   .coil_ohm = 360, .diode_mv = 300};

TEST(coil_full_voltage_for_pullin_then_hold)
{
    CHECK_EQ(fa_coil_duty_pct(&COIL, false, 0), 0);
    CHECK_EQ(fa_coil_duty_pct(&COIL, true, 0), 100);
    CHECK_EQ(fa_coil_duty_pct(&COIL, true, 99), 100);      /* Omron：全壓至少 100ms */
    CHECK_EQ(fa_coil_duty_pct(&COIL, true, 100), 60);
    CHECK_EQ(fa_coil_duty_pct(&COIL, false, 5000), 0);
    fa_coil_cfg_t off = COIL; off.hold_pct = 100;           /* 關掉 PWM 保持＝一直全壓 */
    CHECK_EQ(fa_coil_duty_pct(&off, true, 5000), 100);
}

TEST(coil_hold_voltage_above_omron_30_percent)
{
    CHECK_EQ(fa_coil_supply_ua(&COIL, 100), 33333);         /* 360Ω、12V：33.3mA */
    uint32_t v = fa_coil_voltage_mv(&COIL, COIL.hold_pct);  /* 0.6·12 − 0.4·0.3 = 7.08V */
    CHECK_EQ(v, 7080);
    CHECK(v * 100 / COIL.supply_mv >= 30);                  /* G5Q-1 保持電壓下限 */
    fa_coil_cfg_t lo = COIL; lo.hold_pct = 30;              /* 30% duty 扣掉二極體壓降就低於 30% */
    CHECK(fa_coil_voltage_mv(&lo, 30) * 100 / lo.supply_mv < 30);
}

TEST(coil_budget_four_relays_fits_irm02)
{
    /* 12V 側：Zigbee 收訊等常態 26mA，+10dBm 發射峰值再 +32mA（docs/verification/V3.0.md §6）。
     * IRM-02-12 額定 167mA；吸合錯開 ≥ pullin_ms，同時最多 1 顆在全壓。 */
    const uint32_t base = 26000, burst = 32000, rated = 167000;
    uint32_t hold = fa_coil_supply_ua(&COIL, 60);
    CHECK(hold > 11000 && hold < 12500);                    /* 每顆保持約 11.8mA */
    uint32_t worst = fa_coil_budget_ua(&COIL, 3, 1, base, burst);   /* 3 顆保持＋第 4 顆吸合＋發射 */
    CHECK(worst < rated * 80 / 100);                        /* 約 127mA，留 20% 餘裕 */
    CHECK(fa_coil_budget_ua(&COIL, 4, 0, base, burst) < rated * 70 / 100);
    /* SPICE（sim/relay_pwm_hold.cir）：閘極 1k×Ciss 讓關斷晚約 1µs，實際 duty 約 64%，保持 14.4mA。
     * 用 66% 當最壞情況重算，仍要在額定 85% 內 */
    fa_coil_cfg_t slow = COIL; slow.hold_pct = 66;
    CHECK(fa_coil_budget_ua(&slow, 3, 1, base, burst) < rated * 85 / 100);
    fa_coil_cfg_t no_pwm = COIL; no_pwm.hold_pct = 100;     /* 沒有 PWM：4 顆全壓＋發射 191mA，超額 */
    CHECK(fa_coil_budget_ua(&no_pwm, 3, 1, base, burst) > rated * 110 / 100);
}

/* ---------------- DI／DO 模式（V3.2）---------------- */
static fa_io_t IO; static fa_relays_t RL; static fa_action_t ACT[8]; static uint32_t NOW;

static void io_setup(uint8_t di, uint8_t dout_k1, uint32_t jog)
{
    fa_io_cfg_t c; fa_io_default(&c, FA_MODE_FAN3);          /* K1–K3 互鎖、K4 自鎖 */
    for (int k = 0; k < FA_CH; k++) c.di_mode[k] = di;
    c.do_mode[0] = dout_k1; c.jog_ms[0] = jog;
    fa_io_init(&IO, &c);
    fa_relays_init_group(&RL, fa_io_interlock_mask(&c), 4, 200);
    NOW = 0;
}
/* 接線 DI 維持某電位 n 個 10ms 取樣，回傳最後一次的動作數總和 */
static int hold_di(uint8_t ch, bool closed, int samples)
{
    int total = 0;
    for (int i = 0; i < samples; i++) {
        NOW += 10;
        int n = fa_io_wired(&IO, &RL, ch, closed, NOW, ACT, 8);
        if (n > 0) total += n;
        n = fa_io_tick(&IO, &RL, NOW, ACT, 8);
        if (n > 0) total += n;
    }
    return total;
}

TEST(io_default_preset_and_validation)
{
    fa_io_cfg_t c; fa_io_default(&c, FA_MODE_FAN3);
    CHECK_EQ(fa_io_interlock_mask(&c), 0x7);
    CHECK_EQ(c.do_mode[3], FA_DO_LATCH);
    CHECK_EQ(c.di_mode[0], FA_DI_HOLD);
    CHECK(fa_io_cfg_valid(&c));
    c.jog_ms[2] = 100;  CHECK(!fa_io_cfg_valid(&c));        /* 點動 <0.5s 不收 */
    fa_io_default(&c, FA_MODE_4CH); c.di_mode[1] = 7; CHECK(!fa_io_cfg_valid(&c));
    fa_io_default(&c, FA_MODE_4CH); CHECK_EQ(fa_io_interlock_mask(&c), 0);
}

TEST(io_debounce_ignores_short_glitch)
{
    io_setup(FA_DI_HOLD, FA_DO_LATCH, 1000);
    hold_di(0, true, 2); hold_di(0, false, 5);                /* 20ms 雜訊：不動作 */
    CHECK(!RL.on[0]);
    hold_di(0, true, 3);                                      /* 30ms 穩定：作動 */
    CHECK(RL.on[0]);
}

TEST(io_hold_follows_contact_for_all_do_modes)
{
    for (uint8_t dm = FA_DO_LATCH; dm <= FA_DO_INTERLOCK; dm++) {
        io_setup(FA_DI_HOLD, dm, 1000);
        hold_di(0, true, 5);  CHECK(RL.on[0]);
        hold_di(0, true, 300); CHECK(RL.on[0]);               /* 點動也不會在持續模式下自己關 */
        hold_di(0, false, 5); CHECK(!RL.on[0]);
    }
}

TEST(io_hold_interlock_switches_speed_break_before_make)
{
    io_setup(FA_DI_HOLD, FA_DO_INTERLOCK, 1000);
    hold_di(0, true, 5); CHECK(RL.on[0]);
    NOW += 10;
    fa_io_wired(&IO, &RL, 1, true, NOW, ACT, 8); fa_io_wired(&IO, &RL, 1, true, NOW, ACT, 8);
    int n = fa_io_wired(&IO, &RL, 1, true, NOW, ACT, 8);      /* DI2 接通：K1 先斷、200ms 後 K2 吸合 */
    CHECK_EQ(n, 2);
    CHECK(ACT[0].ch == 0 && !ACT[0].on);
    CHECK(ACT[1].ch == 1 && ACT[1].on && ACT[1].delay_ms == 200);
    CHECK(!RL.on[0] && RL.on[1]);
}

TEST(io_press_latch_toggles_once_per_press)
{
    io_setup(FA_DI_PRESS, FA_DO_LATCH, 1000);
    hold_di(0, true, 50);  CHECK(RL.on[0]);                   /* 按住 0.5 秒：只反轉一次 */
    hold_di(0, false, 5);  CHECK(RL.on[0]);                   /* 放開：不動 */
    hold_di(0, true, 5);   CHECK(!RL.on[0]);                  /* 再按：關 */
}

TEST(io_press_jog_pulses_and_retrigger_restarts)
{
    io_setup(FA_DI_PRESS, FA_DO_JOG, 1000);
    hold_di(0, true, 5); hold_di(0, false, 5); CHECK(RL.on[0]);
    hold_di(0, false, 80); CHECK(RL.on[0]);                   /* 0.9 秒：還開著 */
    hold_di(0, true, 5);  hold_di(0, false, 5);               /* 期間再按：重新計時 */
    hold_di(0, false, 80); CHECK(RL.on[0]);
    hold_di(0, false, 20); CHECK(!RL.on[0]);                  /* 重新計時後滿 1 秒：關 */
}

TEST(io_press_interlock_on_then_off)
{
    io_setup(FA_DI_PRESS, FA_DO_INTERLOCK, 1000);
    hold_di(1, true, 5); hold_di(1, false, 5); CHECK(RL.on[1]);   /* DI2：K2 開 */
    hold_di(2, true, 5); hold_di(2, false, 30); CHECK(!RL.on[1] && RL.on[2]);  /* DI3：換 K3 */
    hold_di(2, true, 5); hold_di(2, false, 5); CHECK(!RL.on[2]);  /* 再按 DI3：關 */
}

TEST(io_off_mode_does_nothing)
{
    io_setup(FA_DI_OFF, FA_DO_LATCH, 1000);
    hold_di(3, true, 10); hold_di(3, false, 10); hold_di(3, true, 10);
    CHECK(!RL.on[3]);
    CHECK(IO.active[3]);                                      /* 狀態仍回報 */
}

TEST(io_rf_is_a_twin_of_wired_di)
{
    io_setup(FA_DI_PRESS, FA_DO_LATCH, 1000);
    for (int i = 0; i < 10; i++) {                            /* 按住遙控器 0.5 秒（每 50ms 一幀）：只反轉一次 */
        NOW += 50; fa_io_rf(&IO, &RL, 3, NOW, ACT, 8); fa_io_tick(&IO, &RL, NOW, ACT, 8);
    }
    CHECK(RL.on[3]);
    NOW += 150; fa_io_tick(&IO, &RL, NOW, ACT, 8); CHECK(IO.rf[3]);    /* 150ms 沒收到：還算按住 */
    NOW += 100; fa_io_tick(&IO, &RL, NOW, ACT, 8); CHECK(!IO.rf[3]);   /* 250ms：放開 */
    NOW += 50; fa_io_rf(&IO, &RL, 3, NOW, ACT, 8); CHECK(!RL.on[3]);   /* 再按：關 */
    /* 接線接通時再按遙控器：合併狀態已經是接通，不會再觸發 */
    io_setup(FA_DI_PRESS, FA_DO_LATCH, 1000);
    hold_di(3, true, 5); CHECK(RL.on[3]);
    NOW += 10; fa_io_rf(&IO, &RL, 3, NOW, ACT, 8); CHECK(RL.on[3]);
}

TEST(io_rf_hold_mode_releases_after_timeout)
{
    io_setup(FA_DI_HOLD, FA_DO_LATCH, 1000);
    NOW += 50; fa_io_rf(&IO, &RL, 3, NOW, ACT, 8); CHECK(RL.on[3]);
    NOW += 250; fa_io_tick(&IO, &RL, NOW, ACT, 8); CHECK(!RL.on[3]);
}

TEST(io_respects_max_on_limit)
{
    io_setup(FA_DI_HOLD, FA_DO_LATCH, 1000);
    fa_io_cfg_t c = IO.cfg; for (int k = 0; k < FA_CH; k++) c.do_mode[k] = FA_DO_LATCH;
    fa_io_init(&IO, &c); fa_relays_init_group(&RL, 0, 3, 200);
    hold_di(0, true, 5); hold_di(1, true, 5); hold_di(2, true, 5);
    NOW += 10; fa_io_wired(&IO, &RL, 3, true, NOW, ACT, 8); fa_io_wired(&IO, &RL, 3, true, NOW, ACT, 8);
    CHECK_EQ(fa_io_wired(&IO, &RL, 3, true, NOW, ACT, 8), FA_ERR_LIMIT);
    CHECK(!RL.on[3]);
}

/* ---------------- SHA-256／HMAC（標準向量）---------------- */
static void hex(const uint8_t *b, size_t n, char *out) { for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]); }

TEST(sha256_and_hmac_standard_vectors)
{
    uint8_t d[32]; char h[129];
    fa_sha256("abc", 3, d); hex(d, 32, h);                                   /* FIPS 180-2 B.1 */
    CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    fa_sha256("", 0, d); hex(d, 32, h);
    CHECK(strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    const char *m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   /* FIPS 180-2 B.2：兩個區塊 */
    fa_sha256(m2, strlen(m2), d); hex(d, 32, h);
    CHECK(strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    const char *msg = "what do ya want for nothing?";                       /* RFC 4231 test case 2 */
    fa_hmac_sha256((const uint8_t *)"Jefe", 4, msg, strlen(msg), d); hex(d, 32, h);
    CHECK(strcmp(h, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") == 0);
    uint8_t longkey[131]; memset(longkey, 0xaa, sizeof longkey);              /* RFC 4231 case 6：金鑰比區塊長 */
    const char *m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    fa_hmac_sha256(longkey, sizeof longkey, m6, strlen(m6), d); hex(d, 32, h);
    CHECK(strcmp(h, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54") == 0);
    CHECK_EQ(fa_nfc_crc16((const uint8_t *)"123456789", 9), 0x29B1);          /* CRC-16/CCITT-FALSE 檢查值 */
}

/* ---------------- NFC 協定（與 tools/nfc_ref.py 同一組向量）---------------- */
/* 與 tools/nfc_ref.py 的 VEC_SECRET／VEC_UID 相同。UID 照 ST25DV 讀出的順序（LSB 在前），QR 裡顯示成 MSB 在前 */
static const uint8_t VEC_SECRET[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                       0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
static const uint8_t VEC_UID_LSB[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x08, 0x02, 0xE0};

static fa_nfc_cfg_t vec_cfg(void)
{
    fa_nfc_cfg_t c; memset(&c, 0, sizeof c);
    uint8_t di[4] = {2, 1, 0, 2}, dm[4] = {2, 2, 1, 0}; uint32_t jog[4] = {1000, 1000, 5000, 1000};
    memcpy(c.io.di_mode, di, 4); memcpy(c.io.do_mode, dm, 4); memcpy(c.io.jog_ms, jog, sizeof jog);
    strcpy(c.name, "\xe5\xae\xa2\xe5\xbb\xb3\xe6\x96\xb0\xe9\xa2\xa8");   /* 「客廳新風」UTF-8 */
    c.n_remotes = 2; c.remotes[0] = 0x3A5F2; c.remotes[1] = 0x1B007;
    return c;
}

TEST(nfc_cross_language_vectors)
{
    uint8_t key[32], buf[FA_NFC_AREA_LEN]; char h[2 * FA_NFC_AREA_LEN + 1];
    fa_nfc_key_from_secret(VEC_SECRET, key); hex(key, 32, h);
    CHECK(strcmp(h, "d5c163b68da1f4fb1d51d02498707965f98ade59f5da675bccea78d184fd5fdc") == 0);
    char qr[FA_NFC_APP_QR_LEN + 1];
    CHECK_EQ(fa_nfc_app_qr(VEC_UID_LSB, VEC_SECRET, qr), FA_NFC_APP_QR_LEN);
    CHECK(strcmp(qr, "WONFC:2:E002080000000001:101112131415161718191A1B1C1D1E1F") == 0);
    fa_nfc_cfg_t c = vec_cfg();
    CHECK_EQ(fa_nfc_encode_request(buf, &c, 7, key), FA_NFC_REQUEST_LEN);
    hex(buf, FA_NFC_REQUEST_LEN, h);
    CHECK(strcmp(h, "574f020207000000540000000201000202020100e8030000e803000088130000e8030000e5aea2e5bbb3e696b0e9a2a8"
                    "00000000000000000000000002000000f2a5030007b00100000000000000000000000000000000000000000000000000"
                    "91ac26d044df1d9d0fbeaa1e0b5fac63") == 0);
    CHECK_EQ(fa_nfc_encode_state(buf, &c, 8, 0x00030301, "3.3"), FA_NFC_STATE_LEN);
    CHECK_EQ(fa_nfc_crc16(buf, FA_NFC_STATE_LEN - 2), 0x6194);
    size_t n = fa_nfc_mb_encode_request(buf, sizeof buf, FA_NFC_MB_LEARN_REMOTE, 5, NULL, 0, key, 0xDEADBEEF);
    hex(buf, n, h);
    CHECK(strcmp(h, "4d1005007b569d16854ce103a231b68817a8a725") == 0);
}

TEST(nfc_state_roundtrip_and_crc)
{
    uint8_t buf[FA_NFC_AREA_LEN]; fa_nfc_cfg_t c = vec_cfg(), d; uint32_t gen, fw;
    fa_nfc_encode_state(buf, &c, 42, 0x00030301, "3.3");
    CHECK_EQ(fa_nfc_decode_state(buf, FA_NFC_STATE_LEN, &d, &gen, &fw), FA_NFC_OK);
    CHECK_EQ(gen, 42); CHECK_EQ(fw, 0x00030301);
    CHECK(memcmp(&c.io, &d.io, sizeof c.io) == 0 && strcmp(c.name, d.name) == 0 && d.n_remotes == 2 && d.remotes[1] == 0x1B007);
    buf[40] ^= 0x01;                                                          /* 半筆／壞掉的資料：CRC 擋下 */
    CHECK_EQ(fa_nfc_decode_state(buf, FA_NFC_STATE_LEN, &d, &gen, &fw), FA_NFC_ERR_FORMAT);
}

TEST(nfc_request_accept_and_reject)
{
    uint8_t key[32], bad[32], buf[FA_NFC_AREA_LEN], other[16];
    fa_nfc_key_from_secret(VEC_SECRET, key);
    memcpy(other, VEC_SECRET, 16); other[15] ^= 1; fa_nfc_key_from_secret(other, bad);   /* 別台的金鑰 */
    fa_nfc_cfg_t cur = vec_cfg(), want = vec_cfg(), out;
    want.io.di_mode[3] = FA_DI_PRESS; want.io.jog_ms[2] = 30000; strcpy(want.name, "bath");
    fa_nfc_encode_request(buf, &want, 7, key);
    CHECK(fa_nfc_is_request(buf, FA_NFC_HDR_LEN));
    CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, key, 7, &cur, &out), FA_NFC_OK);
    CHECK(out.io.di_mode[3] == FA_DI_PRESS && out.io.jog_ms[2] == 30000 && strcmp(out.name, "bath") == 0);
    CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, key, 8, &cur, &out), FA_NFC_ERR_STALE);   /* 重送舊請求 */
    CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, bad, 7, &cur, &out), FA_NFC_ERR_AUTH);    /* 別台的金鑰 */
    buf[20] ^= 0x01;                                                                                   /* 被改過 */
    CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, key, 7, &cur, &out), FA_NFC_ERR_AUTH);
    want = vec_cfg(); want.io.do_mode[0] = 9; fa_nfc_encode_request(buf, &want, 7, key);              /* 模式超出範圍 */
    CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, key, 7, &cur, &out), FA_NFC_ERR_INVALID);
    want = vec_cfg(); want.io.jog_ms[0] = 100; fa_nfc_encode_request(buf, &want, 7, key);             /* 點動 <0.5s */
    CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, key, 7, &cur, &out), FA_NFC_ERR_INVALID);
    want = vec_cfg(); want.remotes[1] = 0x12345; fa_nfc_encode_request(buf, &want, 7, key);           /* 加沒學過的遙控器 */
    CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, key, 7, &cur, &out), FA_NFC_ERR_INVALID);
    want = vec_cfg(); want.n_remotes = 1; want.remotes[0] = 0x1B007; fa_nfc_encode_request(buf, &want, 7, key);  /* 刪一支 */
    CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, key, 7, &cur, &out), FA_NFC_OK);
    CHECK(out.n_remotes == 1 && out.remotes[0] == 0x1B007 && out.remotes[1] == 0);
    buf[0] = 0; CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, key, 7, &cur, &out), FA_NFC_ERR_FORMAT);
    uint8_t ack[FA_NFC_ACK_LEN]; fa_nfc_encode_ack(ack, 7, FA_NFC_ERR_STALE);
    CHECK(ack[0] == 'W' && ack[2] == FA_NFC_T_ACK && ack[4] == 7 && ack[FA_NFC_HDR_LEN] == FA_NFC_ERR_STALE);
    CHECK(!fa_nfc_is_request(ack, FA_NFC_ACK_LEN));                          /* 寫了 ACK 之後不會被當成新請求再處理 */
}

TEST(nfc_v1_frames_rejected)
{
    uint8_t key[32], buf[FA_NFC_AREA_LEN]; fa_nfc_key_from_secret(VEC_SECRET, key);
    fa_nfc_cfg_t cur = vec_cfg(), out, c = vec_cfg(); uint32_t gen, fw;
    fa_nfc_encode_request(buf, &c, 7, key);
    CHECK_EQ(buf[3], 2);                                                      /* 標頭版本＝2 */
    buf[3] = 1;                                                               /* 舊版 App 送來的請求 */
    CHECK(!fa_nfc_is_request(buf, FA_NFC_HDR_LEN));
    CHECK_EQ(fa_nfc_check_request(buf, FA_NFC_REQUEST_LEN, key, 7, &cur, &out), FA_NFC_ERR_FORMAT);
    fa_nfc_encode_state(buf, &c, 1, 0, "3.4");
    buf[3] = 1; CHECK_EQ(fa_nfc_decode_state(buf, FA_NFC_STATE_LEN, &out, &gen, &fw), FA_NFC_ERR_FORMAT);
}

TEST(nfc_mailbox_auth_and_replay)
{
    uint8_t key[32], buf[64]; fa_nfc_key_from_secret(VEC_SECRET, key); fa_nfc_mb_req_t r;
    size_t n = fa_nfc_mb_encode_request(buf, sizeof buf, FA_NFC_MB_GET_STATUS, 1, NULL, 0, key, 0);
    CHECK_EQ(n, 4); CHECK_EQ(fa_nfc_mb_parse(buf, n, key, 0, &r), FA_NFC_OK);   /* 讀狀態不用授權 */
    n = fa_nfc_mb_encode_request(buf, sizeof buf, FA_NFC_MB_FACTORY_RESET, 2, NULL, 0, key, 0x11223344);
    CHECK_EQ(fa_nfc_mb_parse(buf, n, key, 0x11223344, &r), FA_NFC_OK);
    CHECK(r.cmd == FA_NFC_MB_FACTORY_RESET && r.seq == 2);
    CHECK_EQ(fa_nfc_mb_parse(buf, n, key, 0x55667788, &r), FA_NFC_ERR_AUTH);    /* challenge 已換：錄下來重送無效 */
    CHECK_EQ(fa_nfc_mb_parse(buf, n, key, 0, &r), FA_NFC_ERR_AUTH);             /* 沒有有效 challenge */
    CHECK_EQ(fa_nfc_mb_parse(buf, n - 1, key, 0x11223344, &r), FA_NFC_ERR_AUTH);/* 少了簽章 */
    buf[1] = 0x55; CHECK_EQ(fa_nfc_mb_parse(buf, n, key, 0x11223344, &r), FA_NFC_ERR_CMD);
    uint8_t st[FA_NFC_STATUS_LEN], out[32];
    fa_nfc_status_t s = {.gen = 9, .relays = 0x5, .di = 0x2, .net = 1, .flags = 1, .fw_version = 0x00030301,
                         .uptime_s = 3600, .challenge = 0xCAFEBABE};
    fa_nfc_encode_status(st, &s);
    CHECK_EQ(fa_nfc_mb_resp(out, sizeof out, FA_NFC_MB_GET_STATUS, 1, FA_NFC_OK, st, FA_NFC_STATUS_LEN), 25);
    CHECK(out[0] == 'R' && out[4] == FA_NFC_STATUS_LEN && out[5 + 4] == 0x5 && out[5 + 16] == 0xBE);
    CHECK_EQ(fa_nfc_mb_resp(out, 10, FA_NFC_MB_GET_STATUS, 1, FA_NFC_OK, st, FA_NFC_STATUS_LEN), 0);   /* 放不下 */
}

/* ---------------- V4 Mode Select（HA 下拉選單）對應 ---------------- */
TEST(modes_counts_labels_and_bounds)
{
    CHECK_EQ(fa_sel_count(FA_SEL_DI), 3); CHECK_EQ(fa_sel_count(FA_SEL_DO), 3); CHECK_EQ(fa_sel_count(FA_SEL_JOG), 11);
    CHECK(strcmp(fa_sel_label(FA_SEL_DO, 2), "互鎖") == 0);
    CHECK(strcmp(fa_sel_label(FA_SEL_JOG, 0), "0.5 秒") == 0);
    CHECK(strcmp(fa_sel_label(FA_SEL_DI, 3), "") == 0);                       /* 超出範圍 */
    for (int i = 0; i < FA_JOG_PRESETS; i++) {                               /* 每個級距都是合法點動時間 */
        fa_io_cfg_t c; fa_io_default(&c, FA_MODE_FAN3); c.jog_ms[0] = FA_JOG_PRESET_MS[i];
        CHECK(fa_io_cfg_valid(&c));
    }
}

TEST(modes_roundtrip_and_reject)
{
    fa_io_cfg_t c; fa_io_default(&c, FA_MODE_FAN3);
    CHECK(fa_sel_set(&c, 2, FA_SEL_DI, FA_DI_PRESS)); CHECK_EQ(c.di_mode[2], FA_DI_PRESS);
    CHECK_EQ(fa_sel_get(&c, 2, FA_SEL_DI), FA_DI_PRESS);
    CHECK(fa_sel_set(&c, 3, FA_SEL_DO, FA_DO_INTERLOCK)); CHECK_EQ(fa_io_interlock_mask(&c), 0xF);
    CHECK(fa_sel_set(&c, 1, FA_SEL_JOG, 7)); CHECK_EQ(c.jog_ms[1], 300000); CHECK_EQ(fa_sel_get(&c, 1, FA_SEL_JOG), 7);
    fa_io_cfg_t before = c;
    CHECK(!fa_sel_set(&c, 4, FA_SEL_DI, 0));                                   /* 通道錯 */
    CHECK(!fa_sel_set(&c, 0, FA_SEL_DO, 3));                                   /* 選項超出 */
    CHECK(!fa_sel_set(&c, 0, FA_SEL_JOG, 11));
    CHECK(memcmp(&before, &c, sizeof c) == 0);                                 /* 被拒絕時設定不變 */
}

TEST(modes_jog_off_preset_shows_nearest)
{
    fa_io_cfg_t c; fa_io_default(&c, FA_MODE_4CH);
    c.jog_ms[0] = 4000;   CHECK_EQ(fa_sel_get(&c, 0, FA_SEL_JOG), 3);         /* NFC 設 4 秒 → 顯示 5 秒 */
    c.jog_ms[0] = 1400;   CHECK_EQ(fa_sel_get(&c, 0, FA_SEL_JOG), 1);         /* 1.4 秒 → 1 秒 */
    c.jog_ms[0] = 3600000; CHECK_EQ(fa_sel_get(&c, 0, FA_SEL_JOG), 10);
}

/* ---------------- 狀態燈 ---------------- */
static int led_on_ms(const fa_led_in_t *in, uint32_t from, uint32_t len)
{
    int n = 0;
    for (uint32_t t = from; t < from + len; t++) n += fa_led_level(in, t);
    return n;
}

TEST(led_net_states)
{
    fa_led_in_t in = {0};
    in.net = FA_LED_NET_ONLINE;
    CHECK_EQ(led_on_ms(&in, 0, 2000), 2000);                      /* 恆亮 */
    in.net = FA_LED_NET_UNPAIRED;
    CHECK_EQ(led_on_ms(&in, 0, 2000), 1000);                      /* 慢閃：半亮半滅 */
    CHECK(!fa_led_level(&in, 0) && fa_led_level(&in, 500));
    in.net = FA_LED_NET_OFFLINE;                                  /* 兩短一長：150+150+600 */
    CHECK_EQ(led_on_ms(&in, 0, FA_LED_BEACON_PERIOD_MS), 900);
    CHECK(fa_led_level(&in, 0) && !fa_led_level(&in, 200) && fa_led_level(&in, 400));
    CHECK(fa_led_level(&in, 1200) && !fa_led_level(&in, 1500));
    CHECK_EQ(led_on_ms(&in, 2000, 2000), 900);                    /* 週期重複 */
}

TEST(led_priority_button_learn_identify_over_net)
{
    fa_led_in_t in = {.net = FA_LED_NET_ONLINE, .identifying = true};
    CHECK_EQ(led_on_ms(&in, 0, 1000), 500);                       /* Identify 蓋過恆亮 */
    CHECK(fa_led_level(&in, 50) && !fa_led_level(&in, 100));      /* 50ms 一跳 */
    in.learning = true;
    CHECK(fa_led_level(&in, 100) && !fa_led_level(&in, 200));     /* 學習 100ms 一跳，蓋過 Identify */
    in.held_ms = 3000;
    CHECK(!fa_led_level(&in, 0) && fa_led_level(&in, 100));
    in.held_ms = 10000;
    CHECK_EQ(led_on_ms(&in, 0, 1000), 1000);                      /* 放開就重置：恆亮 */
}

TEST(led_rgb_shows_relays_when_lit)
{
    bool none[4] = {0}, k1[4] = {1, 0, 0, 0}, k2[4] = {0, 1, 0, 0}, k3[4] = {0, 0, 1, 0}, k4[4] = {0, 0, 0, 1}, k1k4[4] = {1, 0, 0, 1};
    fa_rgb_t c = fa_led_rgb(false, k1);
    CHECK(c.r == 0 && c.g == 0 && c.b == 0);                      /* 燈號「滅」的時刻：全暗 */
    c = fa_led_rgb(true, none);
    CHECK(c.r > 0 && c.r == c.g && c.g == c.b);                   /* 全關：暗白，看得出恆亮／閃 */
    fa_rgb_t dim = c;
    c = fa_led_rgb(true, k1); CHECK(c.r > 0 && c.g == 0 && c.b == 0);
    c = fa_led_rgb(true, k2); CHECK(c.g > 0 && c.r == 0 && c.b == 0);
    c = fa_led_rgb(true, k3); CHECK(c.b > 0 && c.r == 0 && c.g == 0);
    c = fa_led_rgb(true, k4); CHECK(c.r == c.g && c.g == c.b && c.r > dim.r);   /* K4 單獨：比全關亮的白 */
    c = fa_led_rgb(true, k1k4); CHECK(c.r > c.g && c.g == c.b && c.g > 0);      /* K1＋K4：淡紅 */
}

/* ---------------- 顏色轉換（開發板彩色燈） ---------------- */
#define XY(v) ((uint16_t)((v) * 65536.0 + 0.5))

TEST(color_xy_primaries_and_white)
{
    fa_rgb_t c = fa_color_from_xy(XY(0.700), XY(0.299), 254, 255);   /* 紅 */
    CHECK(c.r == 255 && c.g < 40 && c.b < 40);
    c = fa_color_from_xy(XY(0.172), XY(0.747), 254, 255);            /* 綠 */
    CHECK(c.g == 255 && c.r < 80 && c.b < 80);
    c = fa_color_from_xy(XY(0.135), XY(0.039), 254, 255);            /* 藍 */
    CHECK(c.b == 255 && c.r < 80 && c.g < 40);
    c = fa_color_from_xy(XY(0.3127), XY(0.3290), 254, 255);          /* D65 白 */
    CHECK(c.r >= 240 && c.g >= 240 && c.b >= 240);
}

TEST(color_level_scales_and_caps)
{
    fa_rgb_t full = fa_color_from_xy(XY(0.700), XY(0.299), 254, 100);
    CHECK_EQ(full.r, 100);                                           /* 壓在 max */
    fa_rgb_t half = fa_color_from_xy(XY(0.700), XY(0.299), 127, 100);
    CHECK(half.r >= 48 && half.r <= 52);                             /* 亮度一半 */
    fa_rgb_t low = fa_color_from_xy(XY(0.700), XY(0.299), 1, 100);
    CHECK(low.r >= 1);                                               /* 最暗仍看得到 */
    fa_rgb_t bad = fa_color_from_xy(0, 0, 254, 100);                 /* y＝0 不能除以零 */
    CHECK(bad.r == bad.g && bad.g == bad.b);
}

TEST(color_mireds_warm_vs_cool)
{
    fa_rgb_t warm = fa_color_from_mireds(370, 254, 255);             /* 2700K */
    CHECK(warm.r == 255 && warm.b < warm.g && warm.g < warm.r);
    fa_rgb_t cool = fa_color_from_mireds(153, 254, 255);             /* 6500K */
    CHECK(cool.r >= 240 && cool.g >= 240 && cool.b >= 230);
    fa_rgb_t extreme = fa_color_from_mireds(0, 254, 255);            /* 0 不能除以零 */
    CHECK(extreme.b > 0);
}

TEST(led_wire_byte_order)
{
    uint8_t b[3];
    fa_rgb_t red = {200, 10, 3};
    fa_led_wire_bytes(red, FA_LED_ORDER_GRB, b);
    CHECK(b[0] == 10 && b[1] == 200 && b[2] == 3);                /* 標準 WS2812：G、R、B */
    fa_led_wire_bytes(red, FA_LED_ORDER_RGB, b);
    CHECK(b[0] == 200 && b[1] == 10 && b[2] == 3);                /* RGB 燈珠：R、G、B */
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
    RUN(coil_full_voltage_for_pullin_then_hold);
    RUN(coil_hold_voltage_above_omron_30_percent);
    RUN(coil_budget_four_relays_fits_irm02);
    RUN(io_default_preset_and_validation);
    RUN(io_debounce_ignores_short_glitch);
    RUN(io_hold_follows_contact_for_all_do_modes);
    RUN(io_hold_interlock_switches_speed_break_before_make);
    RUN(io_press_latch_toggles_once_per_press);
    RUN(io_press_jog_pulses_and_retrigger_restarts);
    RUN(io_press_interlock_on_then_off);
    RUN(io_off_mode_does_nothing);
    RUN(io_rf_is_a_twin_of_wired_di);
    RUN(io_rf_hold_mode_releases_after_timeout);
    RUN(io_respects_max_on_limit);
    RUN(sha256_and_hmac_standard_vectors);
    RUN(nfc_cross_language_vectors);
    RUN(nfc_state_roundtrip_and_crc);
    RUN(nfc_request_accept_and_reject);
    RUN(nfc_v1_frames_rejected);
    RUN(nfc_mailbox_auth_and_replay);
    RUN(modes_counts_labels_and_bounds);
    RUN(modes_roundtrip_and_reject);
    RUN(modes_jog_off_preset_shows_nearest);
    RUN(led_net_states);
    RUN(led_priority_button_learn_identify_over_net);
    RUN(led_rgb_shows_relays_when_lit);
    RUN(color_xy_primaries_and_white);
    RUN(color_level_scales_and_caps);
    RUN(color_mireds_warm_vs_cool);
    RUN(led_wire_byte_order);
    printf("%d tests, %d failures\n", ul_run, ul_fail);
    return ul_fail ? 1 : 0;
}

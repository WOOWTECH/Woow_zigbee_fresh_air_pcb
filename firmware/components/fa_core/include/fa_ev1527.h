/* EV1527（學習碼 433MHz 遙控器）解碼：餵入 SYN480R DO 腳每段電位的持續時間。
 * 幀：同步 1T 高 + 31T 低；24 位元 MSB 先，0 = 1T 高 3T 低，1 = 3T 高 1T 低；T 約 250–600µs。
 * 連續兩幀相同才回報（遙控器每按一次會重送多幀），雜訊幾乎不可能通過。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t high_us;     /* 上一段高電位長度 */
    uint32_t t_us;        /* 由同步脈衝估的 T */
    uint32_t bits;
    uint8_t  nbits;
    bool     in_frame;
    bool     have_prev;
    uint32_t prev;
} fa_ev1527_t;

void fa_ev1527_init(fa_ev1527_t *d);
bool fa_ev1527_feed(fa_ev1527_t *d, bool level_high, uint32_t dur_us, uint32_t *code);
static inline uint32_t fa_ev1527_addr(uint32_t code) { return code >> 4; }
static inline uint8_t  fa_ev1527_key(uint32_t code) { return code & 0xF; }

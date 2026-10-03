/* 已學習遙控器（20 位元位址）清單與按鍵對應。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FA_REMOTES_MAX 8

typedef struct {
    uint32_t addr[FA_REMOTES_MAX];
    uint8_t  n;
} fa_remotes_t;

void fa_remotes_init(fa_remotes_t *t);
bool fa_remotes_has(const fa_remotes_t *t, uint32_t addr);
void fa_remotes_add(fa_remotes_t *t, uint32_t addr);     /* 已存在不重複；滿了擠掉最舊 */
/* 4 鍵遙控器 A/B/C/D 常見送 0x8/0x4/0x2/0x1 → 通道 0–3；其他值 -1 */
int  fa_remote_key_to_channel(uint8_t key);

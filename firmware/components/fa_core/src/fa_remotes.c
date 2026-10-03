#include "fa_remotes.h"

void fa_remotes_init(fa_remotes_t *t)
{
    *t = (fa_remotes_t){0};
}

bool fa_remotes_has(const fa_remotes_t *t, uint32_t addr)
{
    for (int i = 0; i < t->n; i++)
        if (t->addr[i] == addr) return true;
    return false;
}

void fa_remotes_add(fa_remotes_t *t, uint32_t addr)
{
    if (fa_remotes_has(t, addr)) return;
    if (t->n == FA_REMOTES_MAX) {
        for (int i = 1; i < FA_REMOTES_MAX; i++) t->addr[i - 1] = t->addr[i];
        t->n--;
    }
    t->addr[t->n++] = addr;
}

int fa_remote_key_to_channel(uint8_t key)
{
    switch (key) {
    case 0x8: return 0;
    case 0x4: return 1;
    case 0x2: return 2;
    case 0x1: return 3;
    default:  return -1;
    }
}

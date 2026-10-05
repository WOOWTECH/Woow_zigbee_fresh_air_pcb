/* SHA-256 與 HMAC-SHA256（FIPS 180-4、RFC 2104）。純 C、不碰硬體，主機端可測；NFC 設定的簽章驗證用。 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define FA_SHA256_LEN 32

typedef struct {
    uint32_t h[8];
    uint8_t  buf[64];
    uint64_t total;
    size_t   used;
} fa_sha256_t;

void fa_sha256_init(fa_sha256_t *c);
void fa_sha256_update(fa_sha256_t *c, const void *data, size_t len);
void fa_sha256_final(fa_sha256_t *c, uint8_t out[FA_SHA256_LEN]);
void fa_sha256(const void *data, size_t len, uint8_t out[FA_SHA256_LEN]);
void fa_hmac_sha256(const uint8_t *key, size_t key_len, const void *msg, size_t msg_len, uint8_t out[FA_SHA256_LEN]);
/* 固定時間比較（避免以比對時間推測簽章） */
int  fa_ct_equal(const uint8_t *a, const uint8_t *b, size_t len);

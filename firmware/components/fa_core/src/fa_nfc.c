#include "fa_nfc.h"
#include <string.h>
#include "fa_sha256.h"

static const char KEY_PREFIX[] = "WO30109-NFC-v2:";

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

void fa_nfc_key_from_secret(const uint8_t secret[FA_NFC_SECRET_LEN], uint8_t key[32])
{
    fa_sha256_t c;
    fa_sha256_init(&c);
    fa_sha256_update(&c, KEY_PREFIX, sizeof KEY_PREFIX - 1);
    fa_sha256_update(&c, secret, FA_NFC_SECRET_LEN);
    fa_sha256_final(&c, key);
}

size_t fa_nfc_app_qr(const uint8_t uid[8], const uint8_t secret[FA_NFC_SECRET_LEN], char out[FA_NFC_APP_QR_LEN + 1])
{
    static const char H[] = "0123456789ABCDEF";
    char *p = out;
    memcpy(p, "WONFC:2:", 8); p += 8;
    for (int i = 7; i >= 0; i--) { *p++ = H[uid[i] >> 4]; *p++ = H[uid[i] & 15]; }
    *p++ = ':';
    for (int i = 0; i < FA_NFC_SECRET_LEN; i++) { *p++ = H[secret[i] >> 4]; *p++ = H[secret[i] & 15]; }
    *p = 0;
    return (size_t)(p - out);
}

uint16_t fa_nfc_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*p++) << 8;
        for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)(crc << 1 ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static void put_hdr(uint8_t *o, uint8_t type, uint32_t gen, uint16_t len)
{
    o[0] = 'W'; o[1] = 'O'; o[2] = type; o[3] = FA_NFC_VERSION;
    put32(o + 4, gen); put16(o + 8, len); put16(o + 10, 0);
}

static void put_payload(uint8_t *p, const fa_nfc_cfg_t *c)
{
    memset(p, 0, FA_NFC_PAYLOAD_LEN);
    for (int k = 0; k < FA_CH; k++) {
        p[k] = c->io.di_mode[k];
        p[4 + k] = c->io.do_mode[k];
        put32(p + 8 + 4 * k, c->io.jog_ms[k]);
    }
    memcpy(p + 24, c->name, FA_NFC_NAME_LEN);
    p[24 + FA_NFC_NAME_LEN - 1] = 0;
    uint8_t n = c->n_remotes > FA_NFC_REMOTES_MAX ? FA_NFC_REMOTES_MAX : c->n_remotes;
    p[48] = n;
    for (int i = 0; i < n; i++) put32(p + 52 + 4 * i, c->remotes[i]);
}

static void get_payload(const uint8_t *p, fa_nfc_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    for (int k = 0; k < FA_CH; k++) {
        c->io.di_mode[k] = p[k];
        c->io.do_mode[k] = p[4 + k];
        c->io.jog_ms[k] = get32(p + 8 + 4 * k);
    }
    memcpy(c->name, p + 24, FA_NFC_NAME_LEN);
    c->n_remotes = p[48];
    for (int i = 0; i < FA_NFC_REMOTES_MAX; i++) c->remotes[i] = get32(p + 52 + 4 * i);
}

static bool hdr_ok(const uint8_t *in, size_t n, uint8_t type, uint16_t len)
{
    return n >= FA_NFC_HDR_LEN && in[0] == 'W' && in[1] == 'O' && in[2] == type && in[3] == FA_NFC_VERSION && get16(in + 8) == len;
}

size_t fa_nfc_encode_state(uint8_t out[FA_NFC_AREA_LEN], const fa_nfc_cfg_t *c, uint32_t gen, uint32_t fw_version,
                           const char hw_rev[4])
{
    memset(out, 0, FA_NFC_AREA_LEN);
    put_hdr(out, FA_NFC_T_STATE, gen, FA_NFC_PAYLOAD_LEN + FA_NFC_STATE_EXTRA);
    put_payload(out + FA_NFC_HDR_LEN, c);
    uint8_t *x = out + FA_NFC_HDR_LEN + FA_NFC_PAYLOAD_LEN;
    put32(x, fw_version);
    memcpy(x + 4, hw_rev, 4);
    put16(out + FA_NFC_STATE_LEN - 2, fa_nfc_crc16(out, FA_NFC_STATE_LEN - 2));
    return FA_NFC_STATE_LEN;
}

int fa_nfc_decode_state(const uint8_t *in, size_t n, fa_nfc_cfg_t *c, uint32_t *gen, uint32_t *fw_version)
{
    if (n < FA_NFC_STATE_LEN || !hdr_ok(in, n, FA_NFC_T_STATE, FA_NFC_PAYLOAD_LEN + FA_NFC_STATE_EXTRA))
        return FA_NFC_ERR_FORMAT;
    if (get16(in + FA_NFC_STATE_LEN - 2) != fa_nfc_crc16(in, FA_NFC_STATE_LEN - 2)) return FA_NFC_ERR_FORMAT;
    get_payload(in + FA_NFC_HDR_LEN, c);
    *gen = get32(in + 4);
    if (fw_version) *fw_version = get32(in + FA_NFC_HDR_LEN + FA_NFC_PAYLOAD_LEN);
    return FA_NFC_OK;
}

bool fa_nfc_is_request(const uint8_t *hdr, size_t n)
{
    return hdr_ok(hdr, n, FA_NFC_T_REQUEST, FA_NFC_PAYLOAD_LEN);
}

static void tag_of(const uint8_t *msg, size_t n, const uint8_t key[32], uint8_t tag[FA_NFC_TAG_LEN])
{
    uint8_t mac[FA_SHA256_LEN];
    fa_hmac_sha256(key, 32, msg, n, mac);
    memcpy(tag, mac, FA_NFC_TAG_LEN);
}

size_t fa_nfc_encode_request(uint8_t out[FA_NFC_AREA_LEN], const fa_nfc_cfg_t *c, uint32_t based_on_gen,
                             const uint8_t key[32])
{
    memset(out, 0, FA_NFC_AREA_LEN);
    put_hdr(out, FA_NFC_T_REQUEST, based_on_gen, FA_NFC_PAYLOAD_LEN);
    put_payload(out + FA_NFC_HDR_LEN, c);
    tag_of(out, FA_NFC_HDR_LEN + FA_NFC_PAYLOAD_LEN, key, out + FA_NFC_HDR_LEN + FA_NFC_PAYLOAD_LEN);
    return FA_NFC_REQUEST_LEN;
}

int fa_nfc_check_request(const uint8_t *in, size_t n, const uint8_t key[32], uint32_t cur_gen,
                         const fa_nfc_cfg_t *cur, fa_nfc_cfg_t *out)
{
    if (n < FA_NFC_REQUEST_LEN || !fa_nfc_is_request(in, n)) return FA_NFC_ERR_FORMAT;
    uint8_t tag[FA_NFC_TAG_LEN];
    tag_of(in, FA_NFC_HDR_LEN + FA_NFC_PAYLOAD_LEN, key, tag);
    if (!fa_ct_equal(tag, in + FA_NFC_HDR_LEN + FA_NFC_PAYLOAD_LEN, FA_NFC_TAG_LEN)) return FA_NFC_ERR_AUTH;
    if (get32(in + 4) != cur_gen) return FA_NFC_ERR_STALE;
    fa_nfc_cfg_t c;
    get_payload(in + FA_NFC_HDR_LEN, &c);
    if (!fa_io_cfg_valid(&c.io)) return FA_NFC_ERR_INVALID;
    if (c.name[FA_NFC_NAME_LEN - 1] != 0) return FA_NFC_ERR_INVALID;
    if (c.n_remotes > FA_NFC_REMOTES_MAX) return FA_NFC_ERR_INVALID;
    for (int i = 0; i < c.n_remotes; i++) {                 /* 遙控器只能刪、不能加（加要在現場學習） */
        bool known = false;
        for (int j = 0; j < cur->n_remotes; j++) known |= c.remotes[i] == cur->remotes[j];
        if (!known) return FA_NFC_ERR_INVALID;
    }
    for (int i = c.n_remotes; i < FA_NFC_REMOTES_MAX; i++) c.remotes[i] = 0;
    *out = c;
    return FA_NFC_OK;
}

size_t fa_nfc_encode_ack(uint8_t out[FA_NFC_ACK_LEN], uint32_t based_on_gen, uint8_t status)
{
    memset(out, 0, FA_NFC_ACK_LEN);
    put_hdr(out, FA_NFC_T_ACK, based_on_gen, 4);
    out[FA_NFC_HDR_LEN] = status;
    return FA_NFC_ACK_LEN;
}

/* ---------------- mailbox ---------------- */
bool fa_nfc_mb_needs_auth(uint8_t cmd)
{
    return cmd == FA_NFC_MB_LEARN_REMOTE || cmd == FA_NFC_MB_FACTORY_RESET;
}

static void mb_tag(const uint8_t *msg, size_t n, const uint8_t key[32], uint32_t challenge, uint8_t tag[FA_NFC_TAG_LEN])
{
    uint8_t buf[4 + 255 + 4];
    memcpy(buf, msg, n);
    put32(buf + n, challenge);
    tag_of(buf, n + 4, key, tag);
}

int fa_nfc_mb_parse(const uint8_t *in, size_t n, const uint8_t key[32], uint32_t challenge, fa_nfc_mb_req_t *req)
{
    if (n < 4 || in[0] != 'M' || (size_t)4 + in[3] > n) return FA_NFC_ERR_FORMAT;
    req->cmd = in[1]; req->seq = in[2]; req->len = in[3]; req->payload = in + 4;
    switch (req->cmd) {
    case FA_NFC_MB_GET_STATUS: case FA_NFC_MB_IDENTIFY: case FA_NFC_MB_LEARN_REMOTE: case FA_NFC_MB_FACTORY_RESET: break;
    default: return FA_NFC_ERR_CMD;
    }
    if (!fa_nfc_mb_needs_auth(req->cmd)) return FA_NFC_OK;
    size_t body = 4 + (size_t)req->len;
    if (n < body + FA_NFC_TAG_LEN || challenge == 0) return FA_NFC_ERR_AUTH;
    uint8_t tag[FA_NFC_TAG_LEN];
    mb_tag(in, body, key, challenge, tag);
    return fa_ct_equal(tag, in + body, FA_NFC_TAG_LEN) ? FA_NFC_OK : FA_NFC_ERR_AUTH;
}

size_t fa_nfc_mb_resp(uint8_t *out, size_t cap, uint8_t cmd, uint8_t seq, uint8_t status, const uint8_t *payload,
                      uint8_t len)
{
    if (cap < (size_t)5 + len) return 0;
    out[0] = 'R'; out[1] = cmd; out[2] = seq; out[3] = status; out[4] = len;
    if (len) memcpy(out + 5, payload, len);
    return (size_t)5 + len;
}

size_t fa_nfc_encode_status(uint8_t out[FA_NFC_STATUS_LEN], const fa_nfc_status_t *s)
{
    put32(out, s->gen);
    out[4] = s->relays; out[5] = s->di; out[6] = s->net; out[7] = s->flags;
    put32(out + 8, s->fw_version); put32(out + 12, s->uptime_s); put32(out + 16, s->challenge);
    return FA_NFC_STATUS_LEN;
}

size_t fa_nfc_mb_encode_request(uint8_t *out, size_t cap, uint8_t cmd, uint8_t seq, const uint8_t *payload,
                                uint8_t len, const uint8_t key[32], uint32_t challenge)
{
    size_t body = 4 + (size_t)len, total = body + (fa_nfc_mb_needs_auth(cmd) ? FA_NFC_TAG_LEN : 0);
    if (cap < total) return 0;
    out[0] = 'M'; out[1] = cmd; out[2] = seq; out[3] = len;
    if (len) memcpy(out + 4, payload, len);
    if (fa_nfc_mb_needs_auth(cmd)) mb_tag(out, body, key, challenge, out + body);
    return total;
}

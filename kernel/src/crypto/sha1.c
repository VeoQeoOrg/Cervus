#include "../../include/crypto/crypto.h"
#include <string.h>

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_block(uint32_t h[5], const uint8_t p[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1_init(sha1_ctx_t *c)
{
    c->h[0] = 0x67452301u; c->h[1] = 0xEFCDAB89u; c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u; c->h[4] = 0xC3D2E1F0u;
    c->len = 0;
    c->used = 0;
}

void sha1_update(sha1_ctx_t *c, const void *data, size_t n)
{
    const uint8_t *p = data;
    c->len += n;
    while (n) {
        size_t take = 64 - c->used;
        if (take > n) take = n;
        memcpy(c->buf + c->used, p, take);
        c->used += take; p += take; n -= take;
        if (c->used == 64) { sha1_block(c->h, c->buf); c->used = 0; }
    }
}

void sha1_final(sha1_ctx_t *c, uint8_t out[20])
{
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    uint8_t z = 0;
    while (c->used != 56) sha1_update(c, &z, 1);
    uint8_t lb[8];
    for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_update(c, lb, 8);
    for (int i = 0; i < 5; i++) {
        out[i * 4]     = (uint8_t)(c->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)c->h[i];
    }
    crypto_wipe(c, sizeof(*c));
}

void sha1(const void *data, size_t n, uint8_t out[20])
{
    sha1_ctx_t c;
    sha1_init(&c);
    sha1_update(&c, data, n);
    sha1_final(&c, out);
}

void hmac_sha1_vec(const uint8_t *key, size_t keylen, size_t count,
                   const uint8_t *const *data, const size_t *len, uint8_t out[20])
{
    uint8_t k[64], tk[20], pad[64];
    if (keylen > 64) { sha1(key, keylen, tk); key = tk; keylen = 20; }
    memset(k, 0, sizeof(k));
    memcpy(k, key, keylen);
    sha1_ctx_t c;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha1_init(&c);
    sha1_update(&c, pad, 64);
    for (size_t i = 0; i < count; i++) sha1_update(&c, data[i], len[i]);
    uint8_t inner[20];
    sha1_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha1_init(&c);
    sha1_update(&c, pad, 64);
    sha1_update(&c, inner, 20);
    sha1_final(&c, out);
    crypto_wipe(k, sizeof(k));
    crypto_wipe(pad, sizeof(pad));
    crypto_wipe(inner, sizeof(inner));
    crypto_wipe(tk, sizeof(tk));
}

void hmac_sha1(const uint8_t *key, size_t keylen, const void *data, size_t n, uint8_t out[20])
{
    const uint8_t *d[1] = { data };
    size_t l[1] = { n };
    hmac_sha1_vec(key, keylen, 1, d, l, out);
}

void sha1_prf(const uint8_t *key, size_t keylen, const char *label,
              const uint8_t *data, size_t datalen, uint8_t *out, size_t outlen)
{
    uint8_t zero = 0, counter = 0, hash[20];
    const uint8_t *d[4] = { (const uint8_t *)label, &zero, data, &counter };
    size_t l[4] = { strlen(label), 1, datalen, 1 };
    size_t pos = 0;
    while (pos < outlen) {
        hmac_sha1_vec(key, keylen, 4, d, l, hash);
        size_t take = outlen - pos < 20 ? outlen - pos : 20;
        memcpy(out + pos, hash, take);
        pos += take;
        counter++;
    }
    crypto_wipe(hash, sizeof(hash));
}

void pbkdf2_sha1(const char *pass, const uint8_t *salt, size_t saltlen,
                 unsigned iterations, uint8_t *out, size_t outlen)
{
    size_t plen = strlen(pass);
    uint32_t block = 1;
    size_t pos = 0;
    while (pos < outlen) {
        uint8_t cnt[4] = { (uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8), (uint8_t)block };
        const uint8_t *d[2] = { salt, cnt };
        size_t l[2] = { saltlen, 4 };
        uint8_t u[20], t[20];
        hmac_sha1_vec((const uint8_t *)pass, plen, 2, d, l, u);
        memcpy(t, u, 20);
        for (unsigned i = 1; i < iterations; i++) {
            hmac_sha1((const uint8_t *)pass, plen, u, 20, u);
            for (int j = 0; j < 20; j++) t[j] ^= u[j];
        }
        size_t take = outlen - pos < 20 ? outlen - pos : 20;
        memcpy(out + pos, t, take);
        pos += take;
        block++;
        crypto_wipe(u, sizeof(u));
        crypto_wipe(t, sizeof(t));
    }
}

bool crypto_memeq(const void *a, const void *b, size_t n)
{
    const volatile uint8_t *x = a, *y = b;
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= x[i] ^ y[i];
    return d == 0;
}

void crypto_wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) *v++ = 0;
}

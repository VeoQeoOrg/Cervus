#include <crypto.h>
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
        if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1_init(sha1_ctx *c)
{
    c->h[0] = 0x67452301u; c->h[1] = 0xEFCDAB89u; c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u; c->h[4] = 0xC3D2E1F0u;
    c->len = 0;
    c->fill = 0;
}

void sha1_update(sha1_ctx *c, const void *data, size_t n)
{
    const uint8_t *p = data;
    c->len += n;
    while (n) {
        size_t take = 64 - c->fill;
        if (take > n) take = n;
        memcpy(c->buf + c->fill, p, take);
        c->fill += take;
        p += take;
        n -= take;
        if (c->fill == 64) { sha1_block(c->h, c->buf); c->fill = 0; }
    }
}

void sha1_final(sha1_ctx *c, uint8_t out[20])
{
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    uint8_t zero = 0;
    while (c->fill != 56) sha1_update(c, &zero, 1);
    uint8_t tail[8];
    for (int i = 0; i < 8; i++) tail[i] = (uint8_t)(bits >> (56 - i * 8));
    sha1_update(c, tail, 8);
    for (int i = 0; i < 5; i++) {
        out[i * 4]     = (uint8_t)(c->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->h[i]);
    }
}

void sha1(const void *data, size_t n, uint8_t out[20])
{
    sha1_ctx c;
    sha1_init(&c);
    sha1_update(&c, data, n);
    sha1_final(&c, out);
}

void hmac_sha1(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t msglen, uint8_t out[20])
{
    uint8_t k[64], tk[20], pad[64], inner[20];
    if (keylen > 64) { sha1(key, keylen, tk); key = tk; keylen = 20; }
    memset(k, 0, sizeof(k));
    memcpy(k, key, keylen);
    sha1_ctx c;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha1_init(&c);
    sha1_update(&c, pad, 64);
    sha1_update(&c, msg, msglen);
    sha1_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha1_init(&c);
    sha1_update(&c, pad, 64);
    sha1_update(&c, inner, 20);
    sha1_final(&c, out);
}

void pbkdf2_hmac_sha1(const uint8_t *pass, size_t passlen,
                      const uint8_t *salt, size_t saltlen,
                      uint32_t iters, uint8_t *out, size_t outlen)
{
    uint32_t block = 1;
    size_t done = 0;
    while (done < outlen) {
        uint8_t in[512 + 4];
        size_t sl = saltlen > 512 ? 512 : saltlen;
        memcpy(in, salt, sl);
        in[sl + 0] = (uint8_t)(block >> 24);
        in[sl + 1] = (uint8_t)(block >> 16);
        in[sl + 2] = (uint8_t)(block >> 8);
        in[sl + 3] = (uint8_t)block;
        uint8_t u[20], t[20];
        hmac_sha1(pass, passlen, in, sl + 4, u);
        memcpy(t, u, 20);
        for (uint32_t i = 1; i < iters; i++) {
            hmac_sha1(pass, passlen, u, 20, u);
            for (int j = 0; j < 20; j++) t[j] ^= u[j];
        }
        size_t take = outlen - done;
        if (take > 20) take = 20;
        memcpy(out + done, t, take);
        done += take;
        block++;
    }
}

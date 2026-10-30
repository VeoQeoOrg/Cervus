#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <crypto.h>
#include "darwin.h"

#define kCCSuccess        0
#define kCCParamError     (-4300)
#define kCCBufferTooSmall (-4301)
#define kCCMemoryFailure  (-4302)
#define kCCAlignmentError (-4303)
#define kCCDecodeError    (-4304)
#define kCCUnimplemented  (-4305)

struct c256 { uint32_t h[8]; uint64_t len; uint8_t buf[64]; };
struct c1 { uint32_t h[5]; uint64_t len; uint8_t buf[64]; };
struct c512 { uint64_t h[8]; uint64_t hi, lo; uint8_t buf[128]; };
struct cmd { uint32_t h[4]; uint64_t len; uint8_t buf[64]; };

_Static_assert(sizeof(struct c256) == 104, "CC_SHA256_CTX");
_Static_assert(sizeof(struct c1) <= 96, "CC_SHA1_CTX");
_Static_assert(sizeof(struct c512) == 208, "CC_SHA512_CTX");
_Static_assert(sizeof(struct cmd) <= 92, "CC_MD5_CTX");

static void s256_load(sha256_ctx *s, const struct c256 *c)
{
    memcpy(s->h, c->h, sizeof c->h);
    s->len = c->len;
    memcpy(s->buf, c->buf, 64);
    s->n = c->len % 64;
}

static void s256_store(struct c256 *c, const sha256_ctx *s)
{
    memcpy(c->h, s->h, sizeof c->h);
    c->len = s->len;
    memcpy(c->buf, s->buf, 64);
}

static int d_CC_SHA256_Init(struct c256 *c)
{
    sha256_ctx s;
    sha256_init(&s);
    s256_store(c, &s);
    return 1;
}

static int d_CC_SHA224_Init(struct c256 *c)
{
    static const uint32_t iv[8] = { 0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939,
                                    0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4 };
    memset(c, 0, sizeof *c);
    memcpy(c->h, iv, sizeof iv);
    return 1;
}

static int d_CC_SHA256_Update(struct c256 *c, const void *d, uint32_t n)
{
    sha256_ctx s;
    s256_load(&s, c);
    sha256_update(&s, d, n);
    s256_store(c, &s);
    return 1;
}

static int d_CC_SHA256_Final(uint8_t *out, struct c256 *c)
{
    sha256_ctx s;
    s256_load(&s, c);
    sha256_final(&s, out);
    memset(c, 0, sizeof *c);
    return 1;
}

static int d_CC_SHA224_Final(uint8_t *out, struct c256 *c)
{
    uint8_t full[32];
    d_CC_SHA256_Final(full, c);
    memcpy(out, full, 28);
    return 1;
}

static void s1_load(sha1_ctx *s, const struct c1 *c)
{
    memcpy(s->h, c->h, sizeof c->h);
    s->len = c->len;
    memcpy(s->buf, c->buf, 64);
    s->fill = c->len % 64;
}

static void s1_store(struct c1 *c, const sha1_ctx *s)
{
    memcpy(c->h, s->h, sizeof c->h);
    c->len = s->len;
    memcpy(c->buf, s->buf, 64);
}

static int d_CC_SHA1_Init(struct c1 *c)
{
    sha1_ctx s;
    sha1_init(&s);
    s1_store(c, &s);
    return 1;
}

static int d_CC_SHA1_Update(struct c1 *c, const void *d, uint32_t n)
{
    sha1_ctx s;
    s1_load(&s, c);
    sha1_update(&s, d, n);
    s1_store(c, &s);
    return 1;
}

static int d_CC_SHA1_Final(uint8_t *out, struct c1 *c)
{
    sha1_ctx s;
    s1_load(&s, c);
    sha1_final(&s, out);
    memset(c, 0, sizeof *c);
    return 1;
}

static void s512_load(sha512_ctx *s, const struct c512 *c)
{
    memcpy(s->h, c->h, sizeof c->h);
    s->len_hi = c->hi;
    s->len_lo = c->lo;
    memcpy(s->buf, c->buf, 128);
    s->n = c->lo % 128;
}

static void s512_store(struct c512 *c, const sha512_ctx *s)
{
    memcpy(c->h, s->h, sizeof c->h);
    c->hi = s->len_hi;
    c->lo = s->len_lo;
    memcpy(c->buf, s->buf, 128);
}

static int d_CC_SHA512_Init(struct c512 *c)
{
    sha512_ctx s;
    sha512_init(&s);
    s512_store(c, &s);
    return 1;
}

static int d_CC_SHA384_Init(struct c512 *c)
{
    sha512_ctx s;
    sha384_init(&s);
    s512_store(c, &s);
    return 1;
}

static int d_CC_SHA512_Update(struct c512 *c, const void *d, uint32_t n)
{
    sha512_ctx s;
    s512_load(&s, c);
    sha512_update(&s, d, n);
    s512_store(c, &s);
    return 1;
}

static int d_CC_SHA512_Final(uint8_t *out, struct c512 *c)
{
    sha512_ctx s;
    s512_load(&s, c);
    sha512_final(&s, out);
    memset(c, 0, sizeof *c);
    return 1;
}

static int d_CC_SHA384_Final(uint8_t *out, struct c512 *c)
{
    uint8_t full[64];
    d_CC_SHA512_Final(full, c);
    memcpy(out, full, 48);
    return 1;
}

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void md5_block(uint32_t h[4], const uint8_t *p)
{
    static const uint32_t k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const uint8_t r[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                   5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
    uint32_t w[16], a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        uint32_t t = d;
        d = c;
        c = b;
        b = b + ROL(a + f + k[i] + w[g], r[i]);
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

static void md4_block(uint32_t h[4], const uint8_t *p)
{
    uint32_t x[16], a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 16; i++)
        x[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
    static const int s1[4] = { 3, 7, 11, 19 }, s2[4] = { 3, 5, 9, 13 }, s3[4] = { 3, 9, 11, 15 };
    static const int o2[16] = { 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 };
    static const int o3[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
    uint32_t *v[4] = { &a, &d, &c, &b };
    for (int i = 0; i < 16; i++) {
        uint32_t *A = v[i % 4], B = *v[(i + 3) % 4], C = *v[(i + 2) % 4], D = *v[(i + 1) % 4];
        *A = ROL(*A + F(B, C, D) + x[i], s1[i % 4]);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t *A = v[i % 4], B = *v[(i + 3) % 4], C = *v[(i + 2) % 4], D = *v[(i + 1) % 4];
        *A = ROL(*A + G(B, C, D) + x[o2[i]] + 0x5a827999, s2[i % 4]);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t *A = v[i % 4], B = *v[(i + 3) % 4], C = *v[(i + 2) % 4], D = *v[(i + 1) % 4];
        *A = ROL(*A + H(B, C, D) + x[o3[i]] + 0x6ed9eba1, s3[i % 4]);
    }
#undef F
#undef G
#undef H
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

static void md_init(struct cmd *c)
{
    memset(c, 0, sizeof *c);
    c->h[0] = 0x67452301;
    c->h[1] = 0xefcdab89;
    c->h[2] = 0x98badcfe;
    c->h[3] = 0x10325476;
}

static void md_update(struct cmd *c, const void *data, size_t n, void (*blk)(uint32_t *, const uint8_t *))
{
    const uint8_t *p = data;
    while (n) {
        size_t fill = c->len % 64, take = 64 - fill;
        if (take > n)
            take = n;
        memcpy(c->buf + fill, p, take);
        c->len += take;
        p += take;
        n -= take;
        if (c->len % 64 == 0)
            blk(c->h, c->buf);
    }
}

static void md_final(uint8_t *out, struct cmd *c, void (*blk)(uint32_t *, const uint8_t *))
{
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80, z = 0, l[8];
    md_update(c, &pad, 1, blk);
    while (c->len % 64 != 56)
        md_update(c, &z, 1, blk);
    for (int i = 0; i < 8; i++)
        l[i] = (uint8_t)(bits >> (8 * i));
    md_update(c, l, 8, blk);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            out[i * 4 + j] = (uint8_t)(c->h[i] >> (8 * j));
    memset(c, 0, sizeof *c);
}

static int d_CC_MD5_Init(struct cmd *c) { md_init(c); return 1; }
static int d_CC_MD5_Update(struct cmd *c, const void *d, uint32_t n) { md_update(c, d, n, md5_block); return 1; }
static int d_CC_MD5_Final(uint8_t *out, struct cmd *c) { md_final(out, c, md5_block); return 1; }
static int d_CC_MD4_Init(struct cmd *c) { md_init(c); return 1; }
static int d_CC_MD4_Update(struct cmd *c, const void *d, uint32_t n) { md_update(c, d, n, md4_block); return 1; }
static int d_CC_MD4_Final(uint8_t *out, struct cmd *c) { md_final(out, c, md4_block); return 1; }

#define ONESHOT(name, ctx, init, upd, fin)                         \
    static uint8_t *name(const void *d, uint32_t n, uint8_t *out) \
    {                                                              \
        ctx c;                                                     \
        init(&c);                                                  \
        upd(&c, d, n);                                             \
        fin(out, &c);                                              \
        return out;                                                \
    }

ONESHOT(d_CC_MD4, struct cmd, d_CC_MD4_Init, d_CC_MD4_Update, d_CC_MD4_Final)
ONESHOT(d_CC_MD5, struct cmd, d_CC_MD5_Init, d_CC_MD5_Update, d_CC_MD5_Final)
ONESHOT(d_CC_SHA1, struct c1, d_CC_SHA1_Init, d_CC_SHA1_Update, d_CC_SHA1_Final)
ONESHOT(d_CC_SHA224, struct c256, d_CC_SHA224_Init, d_CC_SHA256_Update, d_CC_SHA224_Final)
ONESHOT(d_CC_SHA256, struct c256, d_CC_SHA256_Init, d_CC_SHA256_Update, d_CC_SHA256_Final)
ONESHOT(d_CC_SHA384, struct c512, d_CC_SHA384_Init, d_CC_SHA512_Update, d_CC_SHA384_Final)
ONESHOT(d_CC_SHA512, struct c512, d_CC_SHA512_Init, d_CC_SHA512_Update, d_CC_SHA512_Final)

DSYM("_CC_MD4_Init", d_CC_MD4_Init);
DSYM("_CC_MD4_Update", d_CC_MD4_Update);
DSYM("_CC_MD4_Final", d_CC_MD4_Final);
DSYM("_CC_MD4", d_CC_MD4);
DSYM("_CC_MD5_Init", d_CC_MD5_Init);
DSYM("_CC_MD5_Update", d_CC_MD5_Update);
DSYM("_CC_MD5_Final", d_CC_MD5_Final);
DSYM("_CC_MD5", d_CC_MD5);
DSYM("_CC_SHA1_Init", d_CC_SHA1_Init);
DSYM("_CC_SHA1_Update", d_CC_SHA1_Update);
DSYM("_CC_SHA1_Final", d_CC_SHA1_Final);
DSYM("_CC_SHA1", d_CC_SHA1);
DSYM("_CC_SHA224_Init", d_CC_SHA224_Init);
DSYM("_CC_SHA224_Update", d_CC_SHA256_Update);
DSYM("_CC_SHA224_Final", d_CC_SHA224_Final);
DSYM("_CC_SHA224", d_CC_SHA224);
DSYM("_CC_SHA256_Init", d_CC_SHA256_Init);
DSYM("_CC_SHA256_Update", d_CC_SHA256_Update);
DSYM("_CC_SHA256_Final", d_CC_SHA256_Final);
DSYM("_CC_SHA256", d_CC_SHA256);
DSYM("_CC_SHA384_Init", d_CC_SHA384_Init);
DSYM("_CC_SHA384_Update", d_CC_SHA512_Update);
DSYM("_CC_SHA384_Final", d_CC_SHA384_Final);
DSYM("_CC_SHA384", d_CC_SHA384);
DSYM("_CC_SHA512_Init", d_CC_SHA512_Init);
DSYM("_CC_SHA512_Update", d_CC_SHA512_Update);
DSYM("_CC_SHA512_Final", d_CC_SHA512_Final);
DSYM("_CC_SHA512", d_CC_SHA512);

struct hashalg {
    size_t block, out, ctx;
    int (*init)(void *);
    int (*update)(void *, const void *, uint32_t);
    int (*final)(uint8_t *, void *);
};

static const struct hashalg g_hmac[6] = {
    { 64, 20, sizeof(struct c1), (int (*)(void *))d_CC_SHA1_Init, (int (*)(void *, const void *, uint32_t))d_CC_SHA1_Update, (int (*)(uint8_t *, void *))d_CC_SHA1_Final },
    { 64, 16, sizeof(struct cmd), (int (*)(void *))d_CC_MD5_Init, (int (*)(void *, const void *, uint32_t))d_CC_MD5_Update, (int (*)(uint8_t *, void *))d_CC_MD5_Final },
    { 64, 32, sizeof(struct c256), (int (*)(void *))d_CC_SHA256_Init, (int (*)(void *, const void *, uint32_t))d_CC_SHA256_Update, (int (*)(uint8_t *, void *))d_CC_SHA256_Final },
    { 128, 48, sizeof(struct c512), (int (*)(void *))d_CC_SHA384_Init, (int (*)(void *, const void *, uint32_t))d_CC_SHA512_Update, (int (*)(uint8_t *, void *))d_CC_SHA384_Final },
    { 128, 64, sizeof(struct c512), (int (*)(void *))d_CC_SHA512_Init, (int (*)(void *, const void *, uint32_t))d_CC_SHA512_Update, (int (*)(uint8_t *, void *))d_CC_SHA512_Final },
    { 64, 28, sizeof(struct c256), (int (*)(void *))d_CC_SHA224_Init, (int (*)(void *, const void *, uint32_t))d_CC_SHA256_Update, (int (*)(uint8_t *, void *))d_CC_SHA224_Final },
};

struct hmac {
    uint32_t alg;
    uint8_t opad[128];
    uint64_t inner[26];
};
_Static_assert(sizeof(struct hmac) <= 384, "CCHmacContext");

static void d_CCHmacInit(struct hmac *h, uint32_t alg, const void *key, size_t klen)
{
    memset(h, 0, sizeof *h);
    if (alg > 5)
        alg = 2;
    const struct hashalg *a = &g_hmac[alg];
    uint8_t k[128], ipad[128];
    memset(k, 0, sizeof k);
    if (klen > a->block) {
        a->init(h->inner);
        a->update(h->inner, key, (uint32_t)klen);
        a->final(k, h->inner);
    } else if (klen) {
        memcpy(k, key, klen);
    }
    for (size_t i = 0; i < a->block; i++) {
        ipad[i] = k[i] ^ 0x36;
        h->opad[i] = k[i] ^ 0x5c;
    }
    h->alg = alg;
    a->init(h->inner);
    a->update(h->inner, ipad, (uint32_t)a->block);
}

static void d_CCHmacUpdate(struct hmac *h, const void *d, size_t n)
{
    const struct hashalg *a = &g_hmac[h->alg];
    const uint8_t *p = d;
    while (n) {
        uint32_t c = n > 0x40000000 ? 0x40000000 : (uint32_t)n;
        a->update(h->inner, p, c);
        p += c;
        n -= c;
    }
}

static void d_CCHmacFinal(struct hmac *h, uint8_t *out)
{
    const struct hashalg *a = &g_hmac[h->alg];
    uint8_t ih[64];
    a->final(ih, h->inner);
    a->init(h->inner);
    a->update(h->inner, h->opad, (uint32_t)a->block);
    a->update(h->inner, ih, (uint32_t)a->out);
    a->final(out, h->inner);
}

static void d_CCHmac(uint32_t alg, const void *key, size_t klen, const void *d, size_t n, uint8_t *out)
{
    struct hmac h;
    d_CCHmacInit(&h, alg, key, klen);
    d_CCHmacUpdate(&h, d, n);
    d_CCHmacFinal(&h, out);
}

DSYM("_CCHmacInit", d_CCHmacInit);
DSYM("_CCHmacUpdate", d_CCHmacUpdate);
DSYM("_CCHmacFinal", d_CCHmacFinal);
DSYM("_CCHmac", d_CCHmac);

static const uint8_t g_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

struct aes {
    int nr;
    uint8_t rk[240];
    uint8_t isbox[256];
};

static uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }

static uint8_t gmul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    while (b) {
        if (b & 1)
            r ^= a;
        a = xt(a);
        b >>= 1;
    }
    return r;
}

static void aes_setup(struct aes *a, const uint8_t *key, size_t klen)
{
    int nk = (int)klen / 4;
    a->nr = nk + 6;
    memcpy(a->rk, key, klen);
    uint8_t rcon = 1;
    for (int i = nk; i < 4 * (a->nr + 1); i++) {
        uint8_t t[4];
        memcpy(t, a->rk + (i - 1) * 4, 4);
        if (i % nk == 0) {
            uint8_t u = t[0];
            t[0] = g_sbox[t[1]] ^ rcon;
            t[1] = g_sbox[t[2]];
            t[2] = g_sbox[t[3]];
            t[3] = g_sbox[u];
            rcon = xt(rcon);
        } else if (nk > 6 && i % nk == 4) {
            for (int j = 0; j < 4; j++)
                t[j] = g_sbox[t[j]];
        }
        for (int j = 0; j < 4; j++)
            a->rk[i * 4 + j] = a->rk[(i - nk) * 4 + j] ^ t[j];
    }
    for (int i = 0; i < 256; i++)
        a->isbox[g_sbox[i]] = (uint8_t)i;
}

static void aes_enc(const struct aes *a, uint8_t s[16])
{
    for (int i = 0; i < 16; i++)
        s[i] ^= a->rk[i];
    for (int r = 1; r <= a->nr; r++) {
        uint8_t t[16];
        for (int i = 0; i < 16; i++)
            t[i] = g_sbox[s[(i + 4 * (i % 4)) % 16]];
        if (r != a->nr)
            for (int c = 0; c < 4; c++) {
                uint8_t *p = t + 4 * c, x0 = p[0], x1 = p[1], x2 = p[2], x3 = p[3], all = x0 ^ x1 ^ x2 ^ x3;
                p[0] ^= all ^ xt(x0 ^ x1);
                p[1] ^= all ^ xt(x1 ^ x2);
                p[2] ^= all ^ xt(x2 ^ x3);
                p[3] ^= all ^ xt(x3 ^ x0);
            }
        for (int i = 0; i < 16; i++)
            s[i] = t[i] ^ a->rk[r * 16 + i];
    }
}

static void aes_dec(const struct aes *a, uint8_t s[16])
{
    for (int i = 0; i < 16; i++)
        s[i] ^= a->rk[a->nr * 16 + i];
    for (int r = a->nr - 1; r >= 0; r--) {
        uint8_t t[16];
        for (int i = 0; i < 16; i++)
            t[(i + 4 * (i % 4)) % 16] = a->isbox[s[i]];
        for (int i = 0; i < 16; i++)
            t[i] ^= a->rk[r * 16 + i];
        if (r)
            for (int c = 0; c < 4; c++) {
                uint8_t *p = t + 4 * c, x0 = p[0], x1 = p[1], x2 = p[2], x3 = p[3];
                p[0] = gmul(x0, 14) ^ gmul(x1, 11) ^ gmul(x2, 13) ^ gmul(x3, 9);
                p[1] = gmul(x0, 9) ^ gmul(x1, 14) ^ gmul(x2, 11) ^ gmul(x3, 13);
                p[2] = gmul(x0, 13) ^ gmul(x1, 9) ^ gmul(x2, 14) ^ gmul(x3, 11);
                p[3] = gmul(x0, 11) ^ gmul(x1, 13) ^ gmul(x2, 9) ^ gmul(x3, 14);
            }
        memcpy(s, t, 16);
    }
}

static const uint8_t g_pc1[56] = { 57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18, 10, 2, 59, 51, 43, 35,
                                   27, 19, 11, 3, 60, 52, 44, 36, 63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38,
                                   30, 22, 14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4 };
static const uint8_t g_pc2[48] = { 14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10, 23, 19, 12, 4,
                                   26, 8, 16, 7, 27, 20, 13, 2, 41, 52, 31, 37, 47, 55, 30, 40,
                                   51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32 };
static const uint8_t g_ip[64] = { 58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
                                  62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
                                  57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
                                  61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7 };
static const uint8_t g_fp[64] = { 40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
                                  38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
                                  36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
                                  34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25 };
static const uint8_t g_e[48] = { 32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11, 12, 13, 12, 13, 14, 15, 16, 17,
                                 16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1 };
static const uint8_t g_p[32] = { 16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
                                 2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25 };
static const uint8_t g_ds[8][64] = {
    { 14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7, 0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
      4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0, 15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13 },
    { 15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10, 3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
      0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15, 13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9 },
    { 10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8, 13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
      13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7, 1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12 },
    { 7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15, 13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
      10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4, 3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14 },
    { 2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9, 14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
      4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14, 11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3 },
    { 12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11, 10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
      9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6, 4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13 },
    { 4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1, 13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
      1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2, 6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12 },
    { 13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7, 1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
      7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8, 2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11 },
};

static uint64_t perm(uint64_t in, const uint8_t *tab, int n, int inbits)
{
    uint64_t r = 0;
    for (int i = 0; i < n; i++)
        r = (r << 1) | ((in >> (inbits - tab[i])) & 1);
    return r;
}

struct des {
    uint64_t k[3][16];
    int n;
};

static void des_keys(uint64_t sub[16], const uint8_t *key)
{
    uint64_t k = 0;
    for (int i = 0; i < 8; i++)
        k = (k << 8) | key[i];
    uint64_t cd = perm(k, g_pc1, 56, 64);
    uint32_t c = (uint32_t)(cd >> 28) & 0xfffffff, d = (uint32_t)cd & 0xfffffff;
    static const uint8_t sh[16] = { 1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1 };
    for (int i = 0; i < 16; i++) {
        c = ((c << sh[i]) | (c >> (28 - sh[i]))) & 0xfffffff;
        d = ((d << sh[i]) | (d >> (28 - sh[i]))) & 0xfffffff;
        sub[i] = perm(((uint64_t)c << 28) | d, g_pc2, 48, 56);
    }
}

static void des_block(const uint64_t sub[16], int decrypt, uint8_t b[8])
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | b[i];
    v = perm(v, g_ip, 64, 64);
    uint32_t l = (uint32_t)(v >> 32), r = (uint32_t)v;
    for (int i = 0; i < 16; i++) {
        uint64_t e = perm(r, g_e, 48, 32) ^ sub[decrypt ? 15 - i : i];
        uint32_t s = 0;
        for (int j = 0; j < 8; j++) {
            int six = (int)(e >> (42 - 6 * j)) & 63;
            int row = ((six & 32) >> 4) | (six & 1), col = (six >> 1) & 15;
            s = (s << 4) | g_ds[j][row * 16 + col];
        }
        uint32_t f = (uint32_t)perm(s, g_p, 32, 32), t = r;
        r = l ^ f;
        l = t;
    }
    v = perm(((uint64_t)r << 32) | l, g_fp, 64, 64);
    for (int i = 7; i >= 0; i--) {
        b[i] = (uint8_t)v;
        v >>= 8;
    }
}

struct cryptor {
    int op, alg, pad, ecb, bs;
    uint8_t iv[16];
    uint8_t part[16];
    size_t npart;
    union {
        struct aes aes;
        struct des des;
    } k;
};

static void block_raw(struct cryptor *c, uint8_t *b)
{
    if (c->alg == 0) {
        if (c->op)
            aes_dec(&c->k.aes, b);
        else
            aes_enc(&c->k.aes, b);
    } else if (c->k.des.n == 1) {
        des_block(c->k.des.k[0], c->op, b);
    } else if (!c->op) {
        des_block(c->k.des.k[0], 0, b);
        des_block(c->k.des.k[1], 1, b);
        des_block(c->k.des.k[2], 0, b);
    } else {
        des_block(c->k.des.k[2], 1, b);
        des_block(c->k.des.k[1], 0, b);
        des_block(c->k.des.k[0], 1, b);
    }
}

static void block_mode(struct cryptor *c, const uint8_t *in, uint8_t *out)
{
    uint8_t b[16];
    memcpy(b, in, c->bs);
    if (c->ecb) {
        block_raw(c, b);
    } else if (!c->op) {
        for (int i = 0; i < c->bs; i++)
            b[i] ^= c->iv[i];
        block_raw(c, b);
        memcpy(c->iv, b, c->bs);
    } else {
        uint8_t prev[16];
        memcpy(prev, in, c->bs);
        block_raw(c, b);
        for (int i = 0; i < c->bs; i++)
            b[i] ^= c->iv[i];
        memcpy(c->iv, prev, c->bs);
    }
    memcpy(out, b, c->bs);
}

static int d_CCCryptorCreate(int op, int alg, int opts, const void *key, size_t klen, const void *iv, struct cryptor **out)
{
    if (op < 0 || op > 1 || !out)
        return kCCParamError;
    struct cryptor *c = calloc(1, sizeof *c);
    if (!c)
        return kCCMemoryFailure;
    c->op = op;
    c->alg = alg;
    c->pad = opts & 1;
    c->ecb = (opts & 2) != 0;
    if (alg == 0) {
        if (klen != 16 && klen != 24 && klen != 32) {
            free(c);
            return kCCParamError;
        }
        c->bs = 16;
        aes_setup(&c->k.aes, key, klen);
    } else if (alg == 1 || alg == 2) {
        if ((alg == 1 && klen != 8) || (alg == 2 && klen != 24)) {
            free(c);
            return kCCParamError;
        }
        c->bs = 8;
        c->k.des.n = alg == 1 ? 1 : 3;
        for (int i = 0; i < c->k.des.n; i++)
            des_keys(c->k.des.k[i], (const uint8_t *)key + i * 8);
    } else {
        free(c);
        return kCCUnimplemented;
    }
    if (iv && !c->ecb)
        memcpy(c->iv, iv, c->bs);
    *out = c;
    return kCCSuccess;
}

static int d_CCCryptorUpdate(struct cryptor *c, const void *in, size_t n, void *outp, size_t avail, size_t *moved)
{
    const uint8_t *p = in;
    uint8_t *o = outp;
    size_t total = c->npart + n, keep = total % c->bs;
    if (c->op && c->pad && !keep && total)
        keep = c->bs;
    size_t emit = total - keep;
    if (avail < emit) {
        if (moved)
            *moved = emit;
        return kCCBufferTooSmall;
    }
    size_t done = 0;
    while (done < emit) {
        uint8_t blk[16];
        size_t from_part = c->npart < (size_t)c->bs ? c->npart : (size_t)c->bs;
        memcpy(blk, c->part, from_part);
        memcpy(blk + from_part, p, c->bs - from_part);
        p += c->bs - from_part;
        n -= c->bs - from_part;
        c->npart = 0;
        block_mode(c, blk, o + done);
        done += c->bs;
    }
    memcpy(c->part + c->npart, p, n);
    c->npart += n;
    if (moved)
        *moved = done;
    return kCCSuccess;
}

static int d_CCCryptorFinal(struct cryptor *c, void *outp, size_t avail, size_t *moved)
{
    uint8_t *o = outp;
    if (moved)
        *moved = 0;
    if (!c->pad) {
        return c->npart ? kCCAlignmentError : kCCSuccess;
    }
    uint8_t blk[16];
    if (!c->op) {
        if (avail < (size_t)c->bs)
            return kCCBufferTooSmall;
        uint8_t v = (uint8_t)(c->bs - c->npart);
        memcpy(blk, c->part, c->npart);
        memset(blk + c->npart, v, (size_t)v);
        block_mode(c, blk, o);
        if (moved)
            *moved = (size_t)c->bs;
        c->npart = 0;
        return kCCSuccess;
    }
    if (c->npart != (size_t)c->bs)
        return kCCAlignmentError;
    block_mode(c, c->part, blk);
    uint8_t v = blk[c->bs - 1];
    if (!v || v > c->bs)
        return kCCDecodeError;
    for (int i = c->bs - v; i < c->bs; i++)
        if (blk[i] != v)
            return kCCDecodeError;
    size_t keep = (size_t)(c->bs - v);
    if (avail < keep)
        return kCCBufferTooSmall;
    memcpy(o, blk, keep);
    if (moved)
        *moved = keep;
    c->npart = 0;
    return kCCSuccess;
}

static int d_CCCryptorRelease(struct cryptor *c)
{
    if (c) {
        memset(c, 0, sizeof *c);
        free(c);
    }
    return kCCSuccess;
}

static size_t d_CCCryptorGetOutputLength(struct cryptor *c, size_t n, int final)
{
    size_t t = c->npart + n;
    return final ? (t / c->bs + 1) * c->bs : t - t % c->bs;
}

static int d_CCCrypt(int op, int alg, int opts, const void *key, size_t klen, const void *iv, const void *in,
                     size_t n, void *out, size_t avail, size_t *moved)
{
    struct cryptor *c;
    int r = d_CCCryptorCreate(op, alg, opts, key, klen, iv, &c);
    if (r)
        return r;
    size_t a = 0, b = 0;
    r = d_CCCryptorUpdate(c, in, n, out, avail, &a);
    if (!r)
        r = d_CCCryptorFinal(c, (uint8_t *)out + a, avail - a, &b);
    else if (r == kCCBufferTooSmall)
        a = d_CCCryptorGetOutputLength(c, n, 1);
    if (moved)
        *moved = a + b;
    d_CCCryptorRelease(c);
    return r;
}

static int d_CCRandomGenerateBytes(void *p, size_t n)
{
    crypto_random(p, n);
    return kCCSuccess;
}

static int d_CCKeyDerivationPBKDF(int alg, const char *pw, size_t pwlen, const uint8_t *salt, size_t saltlen,
                                  uint32_t prf, unsigned rounds, uint8_t *dk, size_t dklen)
{
    if (alg != 2)
        return kCCParamError;
    if (prf == 1)
        pbkdf2_hmac_sha1((const uint8_t *)pw, pwlen, salt, saltlen, rounds, dk, dklen);
    else if (prf == 3)
        pbkdf2_hmac_sha256((const uint8_t *)pw, pwlen, salt, saltlen, rounds, dk, dklen);
    else
        return kCCUnimplemented;
    return kCCSuccess;
}

DSYM("_CCCryptorCreate", d_CCCryptorCreate);
DSYM("_CCCryptorUpdate", d_CCCryptorUpdate);
DSYM("_CCCryptorFinal", d_CCCryptorFinal);
DSYM("_CCCryptorRelease", d_CCCryptorRelease);
DSYM("_CCCryptorGetOutputLength", d_CCCryptorGetOutputLength);
DSYM("_CCCrypt", d_CCCrypt);
DSYM("_CCRandomGenerateBytes", d_CCRandomGenerateBytes);
DSYM("_CCKeyDerivationPBKDF", d_CCKeyDerivationPBKDF);

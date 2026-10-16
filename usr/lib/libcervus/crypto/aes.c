#include <crypto.h>
#include <string.h>

static const uint8_t sbox[256] = {
 0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
 0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
 0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
 0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
 0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
 0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
 0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
 0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
 0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
 0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
 0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
 0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
 0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
 0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
 0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
 0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

static const uint8_t rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

typedef struct { uint8_t rk[240]; uint32_t w[60]; int nr; } aes_ctx;

typedef long long v2di __attribute__((vector_size(16)));
typedef int v4si __attribute__((vector_size(16)));
typedef char v16qi __attribute__((vector_size(16)));

static uint32_t te0[256];
static int g_tables;
static int g_hw = -1;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b)); }

static uint32_t ror32(uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }

static void init_tables(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t s = sbox[i], s2 = xtime(s), s3 = (uint8_t)(s2 ^ s);
        te0[i] = ((uint32_t)s2 << 24) | ((uint32_t)s << 16) | ((uint32_t)s << 8) | s3;
    }
    g_tables = 1;
}

static int have_hw(void) {
    if (g_hw >= 0) return g_hw;
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    g_hw = (c & (1u << 25)) && (c & (1u << 1)) && (c & (1u << 9)) && (c & (1u << 19));
    return g_hw;
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void aes_key_expand(aes_ctx *c, const uint8_t *key, int keybytes) {
    int nk = keybytes / 4;
    c->nr = nk + 6;
    int words = 4 * (c->nr + 1);
    memcpy(c->rk, key, (size_t)keybytes);
    for (int i = nk; i < words; i++) {
        uint8_t t[4];
        memcpy(t, c->rk + (i - 1) * 4, 4);
        if (i % nk == 0) {
            uint8_t u = t[0]; t[0] = t[1]; t[1] = t[2]; t[2] = t[3]; t[3] = u;
            for (int j = 0; j < 4; j++) t[j] = sbox[t[j]];
            t[0] ^= rcon[i / nk];
        } else if (nk > 6 && i % nk == 4) {
            for (int j = 0; j < 4; j++) t[j] = sbox[t[j]];
        }
        for (int j = 0; j < 4; j++) c->rk[i*4+j] = c->rk[(i-nk)*4+j] ^ t[j];
    }
    for (int i = 0; i < words; i++) c->w[i] = be32(c->rk + i * 4);
}

static void aes_encrypt_block(const aes_ctx *c, const uint8_t in[16], uint8_t out[16]) {
    const uint32_t *rk = c->w;
    uint32_t s0 = be32(in) ^ rk[0], s1 = be32(in + 4) ^ rk[1];
    uint32_t s2 = be32(in + 8) ^ rk[2], s3 = be32(in + 12) ^ rk[3];
    for (int r = 1; r < c->nr; r++) {
        rk += 4;
        uint32_t t0 = te0[s0 >> 24] ^ ror32(te0[(s1 >> 16) & 0xff], 8) ^ ror32(te0[(s2 >> 8) & 0xff], 16) ^ ror32(te0[s3 & 0xff], 24) ^ rk[0];
        uint32_t t1 = te0[s1 >> 24] ^ ror32(te0[(s2 >> 16) & 0xff], 8) ^ ror32(te0[(s3 >> 8) & 0xff], 16) ^ ror32(te0[s0 & 0xff], 24) ^ rk[1];
        uint32_t t2 = te0[s2 >> 24] ^ ror32(te0[(s3 >> 16) & 0xff], 8) ^ ror32(te0[(s0 >> 8) & 0xff], 16) ^ ror32(te0[s1 & 0xff], 24) ^ rk[2];
        uint32_t t3 = te0[s3 >> 24] ^ ror32(te0[(s0 >> 16) & 0xff], 8) ^ ror32(te0[(s1 >> 8) & 0xff], 16) ^ ror32(te0[s2 & 0xff], 24) ^ rk[3];
        s0 = t0; s1 = t1; s2 = t2; s3 = t3;
    }
    rk += 4;
    put_be32(out,      (((uint32_t)sbox[s0 >> 24] << 24) | ((uint32_t)sbox[(s1 >> 16) & 0xff] << 16) |
                        ((uint32_t)sbox[(s2 >> 8) & 0xff] << 8) | sbox[s3 & 0xff]) ^ rk[0]);
    put_be32(out + 4,  (((uint32_t)sbox[s1 >> 24] << 24) | ((uint32_t)sbox[(s2 >> 16) & 0xff] << 16) |
                        ((uint32_t)sbox[(s3 >> 8) & 0xff] << 8) | sbox[s0 & 0xff]) ^ rk[1]);
    put_be32(out + 8,  (((uint32_t)sbox[s2 >> 24] << 24) | ((uint32_t)sbox[(s3 >> 16) & 0xff] << 16) |
                        ((uint32_t)sbox[(s0 >> 8) & 0xff] << 8) | sbox[s1 & 0xff]) ^ rk[2]);
    put_be32(out + 12, (((uint32_t)sbox[s3 >> 24] << 24) | ((uint32_t)sbox[(s0 >> 16) & 0xff] << 16) |
                        ((uint32_t)sbox[(s1 >> 8) & 0xff] << 8) | sbox[s2 & 0xff]) ^ rk[3]);
}

static const uint64_t last4[16] = {
    0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
    0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0
};

typedef struct { uint64_t hl[16], hh[16]; } ghash_tab;

static void ghash_table(ghash_tab *t, const uint8_t h[16]) {
    uint64_t vh = ((uint64_t)be32(h) << 32) | be32(h + 4);
    uint64_t vl = ((uint64_t)be32(h + 8) << 32) | be32(h + 12);
    t->hl[8] = vl; t->hh[8] = vh;
    t->hl[0] = 0; t->hh[0] = 0;
    for (int i = 4; i > 0; i >>= 1) {
        uint32_t tt = (uint32_t)(vl & 1) * 0xe1000000u;
        vl = (vh << 63) | (vl >> 1);
        vh = (vh >> 1) ^ ((uint64_t)tt << 32);
        t->hl[i] = vl; t->hh[i] = vh;
    }
    for (int i = 2; i <= 8; i *= 2) {
        uint64_t bh = t->hh[i], bl = t->hl[i];
        for (int j = 1; j < i; j++) {
            t->hh[i + j] = bh ^ t->hh[j];
            t->hl[i + j] = bl ^ t->hl[j];
        }
    }
}

static void ghash_mul_tab(const ghash_tab *t, uint8_t x[16]) {
    uint8_t lo = x[15] & 0xf, hi, rem;
    uint64_t zh = t->hh[lo], zl = t->hl[lo];
    for (int i = 15; i >= 0; i--) {
        lo = x[i] & 0xf;
        hi = (x[i] >> 4) & 0xf;
        if (i != 15) {
            rem = (uint8_t)(zl & 0xf);
            zl = (zh << 60) | (zl >> 4);
            zh = (zh >> 4) ^ (last4[rem] << 48);
            zh ^= t->hh[lo];
            zl ^= t->hl[lo];
        }
        rem = (uint8_t)(zl & 0xf);
        zl = (zh << 60) | (zl >> 4);
        zh = (zh >> 4) ^ (last4[rem] << 48);
        zh ^= t->hh[hi];
        zl ^= t->hl[hi];
    }
    put_be32(x, (uint32_t)(zh >> 32)); put_be32(x + 4, (uint32_t)zh);
    put_be32(x + 8, (uint32_t)(zl >> 32)); put_be32(x + 12, (uint32_t)zl);
}

static void ghash_sw(const ghash_tab *t, uint8_t y[16], const uint8_t *data, size_t len) {
    while (len) {
        size_t n = len < 16 ? len : 16;
        for (size_t j = 0; j < n; j++) y[j] ^= data[j];
        ghash_mul_tab(t, y);
        data += n; len -= n;
    }
}

static void inc32(uint8_t *ctr) {
    for (int i = 15; i >= 12; i--) { if (++ctr[i]) break; }
}

static void gctr_sw(const aes_ctx *c, const uint8_t *icb, const uint8_t *in, uint8_t *out, size_t len) {
    uint8_t ctr[16], ks[16];
    memcpy(ctr, icb, 16);
    while (len) {
        aes_encrypt_block(c, ctr, ks);
        size_t n = len < 16 ? len : 16;
        for (size_t i = 0; i < n; i++) out[i] = in[i] ^ ks[i];
        inc32(ctr);
        in += n; out += n; len -= n;
    }
}

#define HW __attribute__((target("aes,pclmul,ssse3,sse4.1")))

HW static inline v2di ld(const void *p) { v2di v; memcpy(&v, p, 16); return v; }
HW static inline void st(void *p, v2di v) { memcpy(p, &v, 16); }

HW static inline v2di bswap128(v2di v) {
    const v16qi m = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    return (v2di)__builtin_ia32_pshufb128((v16qi)v, m);
}

HW static v2di gfmul(v2di a, v2di b) {
    v2di t3 = __builtin_ia32_pclmulqdq128(a, b, 0x00);
    v2di t4 = __builtin_ia32_pclmulqdq128(a, b, 0x10);
    v2di t5 = __builtin_ia32_pclmulqdq128(a, b, 0x01);
    v2di t6 = __builtin_ia32_pclmulqdq128(a, b, 0x11);
    t4 ^= t5;
    t5 = __builtin_ia32_pslldqi128(t4, 8 * 8);
    t4 = __builtin_ia32_psrldqi128(t4, 8 * 8);
    t3 ^= t5;
    t6 ^= t4;
    v2di t7 = (v2di)__builtin_ia32_psrldi128((v4si)t3, 31);
    v2di t8 = (v2di)__builtin_ia32_psrldi128((v4si)t6, 31);
    t3 = (v2di)__builtin_ia32_pslldi128((v4si)t3, 1);
    t6 = (v2di)__builtin_ia32_pslldi128((v4si)t6, 1);
    v2di t9 = __builtin_ia32_psrldqi128(t7, 12 * 8);
    t8 = __builtin_ia32_pslldqi128(t8, 4 * 8);
    t7 = __builtin_ia32_pslldqi128(t7, 4 * 8);
    t3 |= t7;
    t6 |= t8;
    t6 |= t9;
    t7 = (v2di)__builtin_ia32_pslldi128((v4si)t3, 31);
    t8 = (v2di)__builtin_ia32_pslldi128((v4si)t3, 30);
    t9 = (v2di)__builtin_ia32_pslldi128((v4si)t3, 25);
    t7 ^= t8;
    t7 ^= t9;
    t8 = __builtin_ia32_psrldqi128(t7, 4 * 8);
    t7 = __builtin_ia32_pslldqi128(t7, 12 * 8);
    t3 ^= t7;
    v2di t2 = (v2di)__builtin_ia32_psrldi128((v4si)t3, 1);
    t4 = (v2di)__builtin_ia32_psrldi128((v4si)t3, 2);
    t5 = (v2di)__builtin_ia32_psrldi128((v4si)t3, 7);
    t2 ^= t4;
    t2 ^= t5;
    t2 ^= t8;
    t3 ^= t2;
    t6 ^= t3;
    return t6;
}

HW static v2di ghash_hw(v2di y, v2di h, const uint8_t *data, size_t len) {
    while (len >= 16) {
        y = gfmul(y ^ bswap128(ld(data)), h);
        data += 16; len -= 16;
    }
    if (len) {
        uint8_t b[16] = {0};
        memcpy(b, data, len);
        y = gfmul(y ^ bswap128(ld(b)), h);
    }
    return y;
}

HW static inline v2di aes_hw(const v2di *rk, int nr, v2di b) {
    b ^= rk[0];
    for (int r = 1; r < nr; r++) b = __builtin_ia32_aesenc128(b, rk[r]);
    return __builtin_ia32_aesenclast128(b, rk[nr]);
}

HW static void gctr_hw(const v2di *rk, int nr, const uint8_t *icb, const uint8_t *in, uint8_t *out, size_t len) {
    uint8_t ctr[16];
    memcpy(ctr, icb, 16);
    uint32_t base = be32(ctr + 12);
    while (len >= 64) {
        uint8_t c4[64];
        for (int k = 0; k < 4; k++) { memcpy(c4 + k * 16, ctr, 12); put_be32(c4 + k * 16 + 12, base + (uint32_t)k); }
        v2di b0 = ld(c4) ^ rk[0], b1 = ld(c4 + 16) ^ rk[0], b2 = ld(c4 + 32) ^ rk[0], b3 = ld(c4 + 48) ^ rk[0];
        for (int r = 1; r < nr; r++) {
            b0 = __builtin_ia32_aesenc128(b0, rk[r]);
            b1 = __builtin_ia32_aesenc128(b1, rk[r]);
            b2 = __builtin_ia32_aesenc128(b2, rk[r]);
            b3 = __builtin_ia32_aesenc128(b3, rk[r]);
        }
        b0 = __builtin_ia32_aesenclast128(b0, rk[nr]);
        b1 = __builtin_ia32_aesenclast128(b1, rk[nr]);
        b2 = __builtin_ia32_aesenclast128(b2, rk[nr]);
        b3 = __builtin_ia32_aesenclast128(b3, rk[nr]);
        st(out, ld(in) ^ b0);
        st(out + 16, ld(in + 16) ^ b1);
        st(out + 32, ld(in + 32) ^ b2);
        st(out + 48, ld(in + 48) ^ b3);
        base += 4;
        in += 64; out += 64; len -= 64;
    }
    while (len) {
        uint8_t cb[16], ks[16];
        memcpy(cb, ctr, 12);
        put_be32(cb + 12, base++);
        st(ks, aes_hw(rk, nr, ld(cb)));
        size_t n = len < 16 ? len : 16;
        for (size_t i = 0; i < n; i++) out[i] = in[i] ^ ks[i];
        in += n; out += n; len -= n;
    }
}

HW static void gcm_hw(const aes_ctx *c, const uint8_t j0[16], const uint8_t *aad, size_t aadlen,
                      const uint8_t *in, uint8_t *out, size_t len, int encrypt, uint8_t tag[16]) {
    v2di rk[15] = { { 0 } };
    for (int i = 0; i <= c->nr; i++) rk[i] = ld(c->rk + i * 16);
    v2di zero = { 0, 0 };
    v2di h = bswap128(aes_hw(rk, c->nr, zero));
    uint8_t icb[16];
    memcpy(icb, j0, 16);
    inc32(icb);
    if (encrypt) gctr_hw(rk, c->nr, icb, in, out, len);
    v2di y = zero;
    y = ghash_hw(y, h, aad, aadlen);
    y = ghash_hw(y, h, encrypt ? out : in, len);
    uint8_t lb[16];
    uint64_t abits = (uint64_t)aadlen * 8, cbits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(abits >> (56 - i*8));
    for (int i = 0; i < 8; i++) lb[8+i] = (uint8_t)(cbits >> (56 - i*8));
    y = gfmul(y ^ bswap128(ld(lb)), h);
    st(tag, bswap128(y) ^ aes_hw(rk, c->nr, ld(j0)));
    if (!encrypt) gctr_hw(rk, c->nr, icb, in, out, len);
}

static void gcm_core(const uint8_t *key, int keybytes, const uint8_t nonce[12],
                     const uint8_t *aad, size_t aadlen,
                     const uint8_t *in, uint8_t *out, size_t len, int encrypt,
                     uint8_t tag[16]) {
    if (!g_tables) init_tables();
    aes_ctx c;
    aes_key_expand(&c, key, keybytes);
    uint8_t j0[16];
    memcpy(j0, nonce, 12);
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    if (have_hw()) {
        gcm_hw(&c, j0, aad, aadlen, in, out, len, encrypt, tag);
        return;
    }
    uint8_t h[16] = {0};
    aes_encrypt_block(&c, h, h);
    ghash_tab t;
    ghash_table(&t, h);
    uint8_t icb[16];
    memcpy(icb, j0, 16);
    inc32(icb);
    if (encrypt) gctr_sw(&c, icb, in, out, len);
    uint8_t y[16] = {0};
    ghash_sw(&t, y, aad, aadlen);
    ghash_sw(&t, y, encrypt ? out : in, len);
    uint8_t lb[16];
    uint64_t abits = (uint64_t)aadlen * 8, cbits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(abits >> (56 - i*8));
    for (int i = 0; i < 8; i++) lb[8+i] = (uint8_t)(cbits >> (56 - i*8));
    for (int j = 0; j < 16; j++) y[j] ^= lb[j];
    ghash_mul_tab(&t, y);
    uint8_t ej0[16];
    aes_encrypt_block(&c, j0, ej0);
    for (int j = 0; j < 16; j++) tag[j] = y[j] ^ ej0[j];
    if (!encrypt) gctr_sw(&c, icb, in, out, len);
}

void aes_gcm_encrypt(const uint8_t *key, int keybytes, const uint8_t nonce[12],
                     const uint8_t *aad, size_t aadlen,
                     const uint8_t *pt, size_t ptlen, uint8_t *ct, uint8_t tag[16]) {
    gcm_core(key, keybytes, nonce, aad, aadlen, pt, ct, ptlen, 1, tag);
}

int aes_gcm_decrypt(const uint8_t *key, int keybytes, const uint8_t nonce[12],
                    const uint8_t *aad, size_t aadlen,
                    const uint8_t *ct, size_t ctlen, const uint8_t tag[16], uint8_t *pt) {
    uint8_t want[16];
    gcm_core(key, keybytes, nonce, aad, aadlen, ct, pt, ctlen, 0, want);
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= want[i] ^ tag[i];
    return diff ? -1 : 0;
}

void aes_128_gcm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                         const uint8_t *aad, size_t aadlen,
                         const uint8_t *pt, size_t ptlen, uint8_t *ct, uint8_t tag[16]) {
    aes_gcm_encrypt(key, 16, nonce, aad, aadlen, pt, ptlen, ct, tag);
}
int aes_128_gcm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                        const uint8_t *aad, size_t aadlen,
                        const uint8_t *ct, size_t ctlen, const uint8_t tag[16], uint8_t *pt) {
    return aes_gcm_decrypt(key, 16, nonce, aad, aadlen, ct, ctlen, tag, pt);
}
void aes_256_gcm_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                         const uint8_t *aad, size_t aadlen,
                         const uint8_t *pt, size_t ptlen, uint8_t *ct, uint8_t tag[16]) {
    aes_gcm_encrypt(key, 32, nonce, aad, aadlen, pt, ptlen, ct, tag);
}
int aes_256_gcm_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                        const uint8_t *aad, size_t aadlen,
                        const uint8_t *ct, size_t ctlen, const uint8_t tag[16], uint8_t *pt) {
    return aes_gcm_decrypt(key, 32, nonce, aad, aadlen, ct, ctlen, tag, pt);
}

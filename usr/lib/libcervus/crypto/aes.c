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

typedef struct { uint8_t rk[240]; int nr; } aes_ctx;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b)); }

static void aes_key_expand(aes_ctx *c, const uint8_t *key, int keybytes) {
    int nk = keybytes / 4;
    c->nr = nk + 6;
    int words = 4 * (c->nr + 1);
    memcpy(c->rk, key, keybytes);
    uint8_t t[4];
    for (int i = nk; i < words; i++) {
        memcpy(t, c->rk + (i-1)*4, 4);
        if (i % nk == 0) {
            uint8_t tmp = t[0]; t[0]=t[1]; t[1]=t[2]; t[2]=t[3]; t[3]=tmp;
            for (int j = 0; j < 4; j++) t[j] = sbox[t[j]];
            t[0] ^= rcon[i/nk];
        } else if (nk > 6 && i % nk == 4) {
            for (int j = 0; j < 4; j++) t[j] = sbox[t[j]];
        }
        for (int j = 0; j < 4; j++) c->rk[i*4+j] = c->rk[(i-nk)*4+j] ^ t[j];
    }
}

static void aes_encrypt_block(const aes_ctx *c, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);
    for (int i = 0; i < 16; i++) s[i] ^= c->rk[i];
    for (int round = 1; round < c->nr; round++) {
        for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];
        uint8_t t;
        t = s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
        t = s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;
        t = s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
        for (int col = 0; col < 4; col++) {
            uint8_t *p = s + col*4;
            uint8_t a0=p[0],a1=p[1],a2=p[2],a3=p[3];
            uint8_t h = a0 ^ a1 ^ a2 ^ a3;
            p[0] ^= h ^ xtime(a0 ^ a1);
            p[1] ^= h ^ xtime(a1 ^ a2);
            p[2] ^= h ^ xtime(a2 ^ a3);
            p[3] ^= h ^ xtime(a3 ^ a0);
        }
        for (int i = 0; i < 16; i++) s[i] ^= c->rk[round*16+i];
    }
    for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];
    uint8_t t;
    t = s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
    t = s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;
    t = s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
    for (int i = 0; i < 16; i++) out[i] = s[i] ^ c->rk[c->nr*16+i];
}

static void ghash_mul(uint8_t *x, const uint8_t *h) {
    uint8_t z[16] = {0}, v[16];
    memcpy(v, h, 16);
    for (int i = 0; i < 128; i++) {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1)
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--) v[j] = (uint8_t)((v[j] >> 1) | (v[j-1] << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    memcpy(x, z, 16);
}

static void ghash_update(uint8_t *y, const uint8_t *h, const uint8_t *data, size_t len) {
    size_t full = len / 16;
    for (size_t i = 0; i < full; i++) {
        for (int j = 0; j < 16; j++) y[j] ^= data[i*16+j];
        ghash_mul(y, h);
    }
    size_t rem = len % 16;
    if (rem) {
        for (size_t j = 0; j < rem; j++) y[j] ^= data[full*16+j];
        ghash_mul(y, h);
    }
}

static void inc32(uint8_t *ctr) {
    for (int i = 15; i >= 12; i--) { if (++ctr[i]) break; }
}

static void gctr(const aes_ctx *c, const uint8_t *icb, const uint8_t *in, uint8_t *out, size_t len) {
    uint8_t ctr[16], ks[16];
    memcpy(ctr, icb, 16);
    size_t off = 0;
    while (off < len) {
        aes_encrypt_block(c, ctr, ks);
        size_t n = len - off; if (n > 16) n = 16;
        for (size_t i = 0; i < n; i++) out[off+i] = in[off+i] ^ ks[i];
        inc32(ctr);
        off += n;
    }
}

static void gcm_core(const uint8_t *key, int keybytes, const uint8_t nonce[12],
                     const uint8_t *aad, size_t aadlen,
                     const uint8_t *in, uint8_t *out, size_t len, int encrypt,
                     uint8_t tag[16]) {
    aes_ctx c;
    aes_key_expand(&c, key, keybytes);
    uint8_t h[16] = {0};
    aes_encrypt_block(&c, h, h);
    uint8_t j0[16];
    memcpy(j0, nonce, 12);
    j0[12]=0; j0[13]=0; j0[14]=0; j0[15]=1;
    uint8_t icb[16];
    memcpy(icb, j0, 16);
    inc32(icb);
    if (encrypt) gctr(&c, icb, in, out, len);
    uint8_t y[16] = {0};
    ghash_update(y, h, aad, aadlen);
    ghash_update(y, h, encrypt ? out : in, len);
    uint8_t lb[16];
    uint64_t abits = (uint64_t)aadlen * 8, cbits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(abits >> (56 - i*8));
    for (int i = 0; i < 8; i++) lb[8+i] = (uint8_t)(cbits >> (56 - i*8));
    for (int j = 0; j < 16; j++) y[j] ^= lb[j];
    ghash_mul(y, h);
    uint8_t ej0[16];
    aes_encrypt_block(&c, j0, ej0);
    for (int j = 0; j < 16; j++) tag[j] = y[j] ^ ej0[j];
    if (!encrypt) gctr(&c, icb, in, out, len);
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

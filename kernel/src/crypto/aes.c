#include "../../include/crypto/crypto.h"
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
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

static uint8_t inv_sbox[256];
static volatile int inv_ready;

static void build_inv(void)
{
    if (inv_ready) return;
    for (int i = 0; i < 256; i++) inv_sbox[sbox[i]] = (uint8_t)i;
    __atomic_store_n(&inv_ready, 1, __ATOMIC_RELEASE);
}

static uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }

static uint8_t mul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    while (b) {
        if (b & 1) r ^= a;
        a = xt(a);
        b >>= 1;
    }
    return r;
}

int aes_set_key(aes_key_t *k, const uint8_t *key, size_t keylen)
{
    int nk;
    if (keylen == 16) { nk = 4; k->rounds = 10; }
    else if (keylen == 24) { nk = 6; k->rounds = 12; }
    else if (keylen == 32) { nk = 8; k->rounds = 14; }
    else return -1;
    build_inv();
    int total = 4 * (k->rounds + 1);
    for (int i = 0; i < nk; i++)
        k->rk[i] = ((uint32_t)key[4 * i] << 24) | ((uint32_t)key[4 * i + 1] << 16) |
                   ((uint32_t)key[4 * i + 2] << 8) | key[4 * i + 3];
    uint8_t rcon = 1;
    for (int i = nk; i < total; i++) {
        uint32_t t = k->rk[i - 1];
        if (i % nk == 0) {
            t = (t << 8) | (t >> 24);
            t = ((uint32_t)sbox[t >> 24] << 24) | ((uint32_t)sbox[(t >> 16) & 0xff] << 16) |
                ((uint32_t)sbox[(t >> 8) & 0xff] << 8) | sbox[t & 0xff];
            t ^= (uint32_t)rcon << 24;
            rcon = xt(rcon);
        } else if (nk > 6 && i % nk == 4) {
            t = ((uint32_t)sbox[t >> 24] << 24) | ((uint32_t)sbox[(t >> 16) & 0xff] << 16) |
                ((uint32_t)sbox[(t >> 8) & 0xff] << 8) | sbox[t & 0xff];
        }
        k->rk[i] = k->rk[i - nk] ^ t;
    }
    return 0;
}

static void add_round_key(uint8_t s[16], const uint32_t *rk)
{
    for (int c = 0; c < 4; c++) {
        s[4 * c]     ^= (uint8_t)(rk[c] >> 24);
        s[4 * c + 1] ^= (uint8_t)(rk[c] >> 16);
        s[4 * c + 2] ^= (uint8_t)(rk[c] >> 8);
        s[4 * c + 3] ^= (uint8_t)rk[c];
    }
}

void aes_encrypt(const aes_key_t *k, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16], t[16];
    memcpy(s, in, 16);
    add_round_key(s, k->rk);
    for (int r = 1; r <= k->rounds; r++) {
        for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++)
                t[4 * c + row] = s[4 * ((c + row) & 3) + row];
        if (r != k->rounds) {
            for (int c = 0; c < 4; c++) {
                uint8_t a0 = t[4 * c], a1 = t[4 * c + 1], a2 = t[4 * c + 2], a3 = t[4 * c + 3];
                s[4 * c]     = (uint8_t)(xt(a0) ^ (xt(a1) ^ a1) ^ a2 ^ a3);
                s[4 * c + 1] = (uint8_t)(a0 ^ xt(a1) ^ (xt(a2) ^ a2) ^ a3);
                s[4 * c + 2] = (uint8_t)(a0 ^ a1 ^ xt(a2) ^ (xt(a3) ^ a3));
                s[4 * c + 3] = (uint8_t)((xt(a0) ^ a0) ^ a1 ^ a2 ^ xt(a3));
            }
        } else {
            memcpy(s, t, 16);
        }
        add_round_key(s, k->rk + 4 * r);
    }
    memcpy(out, s, 16);
    crypto_wipe(s, sizeof(s));
    crypto_wipe(t, sizeof(t));
}

void aes_decrypt(const aes_key_t *k, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16], t[16];
    memcpy(s, in, 16);
    add_round_key(s, k->rk + 4 * k->rounds);
    for (int r = k->rounds - 1; r >= 0; r--) {
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++)
                t[4 * ((c + row) & 3) + row] = s[4 * c + row];
        for (int i = 0; i < 16; i++) t[i] = inv_sbox[t[i]];
        memcpy(s, t, 16);
        add_round_key(s, k->rk + 4 * r);
        if (r != 0) {
            for (int c = 0; c < 4; c++) {
                uint8_t a0 = s[4 * c], a1 = s[4 * c + 1], a2 = s[4 * c + 2], a3 = s[4 * c + 3];
                t[4 * c]     = (uint8_t)(mul(a0, 14) ^ mul(a1, 11) ^ mul(a2, 13) ^ mul(a3, 9));
                t[4 * c + 1] = (uint8_t)(mul(a0, 9) ^ mul(a1, 14) ^ mul(a2, 11) ^ mul(a3, 13));
                t[4 * c + 2] = (uint8_t)(mul(a0, 13) ^ mul(a1, 9) ^ mul(a2, 14) ^ mul(a3, 11));
                t[4 * c + 3] = (uint8_t)(mul(a0, 11) ^ mul(a1, 13) ^ mul(a2, 9) ^ mul(a3, 14));
            }
            memcpy(s, t, 16);
        }
    }
    memcpy(out, s, 16);
    crypto_wipe(s, sizeof(s));
    crypto_wipe(t, sizeof(t));
}

int aes_key_wrap(const uint8_t *kek, size_t keklen, const uint8_t *plain, size_t n, uint8_t *out)
{
    if (n < 16 || n % 8) return -1;
    aes_key_t k;
    if (aes_set_key(&k, kek, keklen)) return -1;
    size_t blocks = n / 8;
    uint8_t a[8], b[16];
    memset(a, 0xa6, 8);
    memmove(out + 8, plain, n);
    for (int j = 0; j <= 5; j++) {
        for (size_t i = 1; i <= blocks; i++) {
            memcpy(b, a, 8);
            memcpy(b + 8, out + 8 * i, 8);
            aes_encrypt(&k, b, b);
            uint64_t t = (uint64_t)blocks * (uint64_t)j + i;
            memcpy(a, b, 8);
            for (int q = 0; q < 8; q++) a[7 - q] ^= (uint8_t)(t >> (8 * q));
            memcpy(out + 8 * i, b + 8, 8);
        }
    }
    memcpy(out, a, 8);
    crypto_wipe(&k, sizeof(k));
    crypto_wipe(b, sizeof(b));
    return 0;
}

int aes_key_unwrap(const uint8_t *kek, size_t keklen, const uint8_t *cipher, size_t n, uint8_t *out)
{
    if (n < 24 || n % 8) return -1;
    aes_key_t k;
    if (aes_set_key(&k, kek, keklen)) return -1;
    size_t blocks = n / 8 - 1;
    uint8_t a[8], b[16];
    memcpy(a, cipher, 8);
    memmove(out, cipher + 8, n - 8);
    for (int j = 5; j >= 0; j--) {
        for (size_t i = blocks; i >= 1; i--) {
            uint64_t t = (uint64_t)blocks * (uint64_t)j + i;
            memcpy(b, a, 8);
            for (int q = 0; q < 8; q++) b[7 - q] ^= (uint8_t)(t >> (8 * q));
            memcpy(b + 8, out + 8 * (i - 1), 8);
            aes_decrypt(&k, b, b);
            memcpy(a, b, 8);
            memcpy(out + 8 * (i - 1), b + 8, 8);
        }
    }
    crypto_wipe(&k, sizeof(k));
    crypto_wipe(b, sizeof(b));
    for (int i = 0; i < 8; i++)
        if (a[i] != 0xa6) {
            crypto_wipe(out, n - 8);
            return -1;
        }
    return 0;
}

static void dbl(uint8_t b[16])
{
    uint8_t carry = b[0] >> 7;
    for (int i = 0; i < 15; i++) b[i] = (uint8_t)((b[i] << 1) | (b[i + 1] >> 7));
    b[15] = (uint8_t)((b[15] << 1) ^ (carry * 0x87));
}

void aes_cmac(const uint8_t *key, size_t keylen, const uint8_t *data, size_t n, uint8_t out[16])
{
    aes_key_t k;
    aes_set_key(&k, key, keylen);
    uint8_t l[16] = { 0 }, k1[16], k2[16], x[16] = { 0 }, blk[16];
    aes_encrypt(&k, l, l);
    memcpy(k1, l, 16); dbl(k1);
    memcpy(k2, k1, 16); dbl(k2);
    size_t nb = (n + 15) / 16;
    bool complete = n && n % 16 == 0;
    if (!nb) nb = 1;
    for (size_t i = 0; i + 1 < nb; i++) {
        for (int j = 0; j < 16; j++) x[j] ^= data[16 * i + j];
        aes_encrypt(&k, x, x);
    }
    size_t last = (nb - 1) * 16;
    if (complete) {
        for (int j = 0; j < 16; j++) blk[j] = data[last + j] ^ k1[j];
    } else {
        size_t rem = n - last;
        for (size_t j = 0; j < 16; j++) blk[j] = j < rem ? data[last + j] : (j == rem ? 0x80 : 0);
        for (int j = 0; j < 16; j++) blk[j] ^= k2[j];
    }
    for (int j = 0; j < 16; j++) x[j] ^= blk[j];
    aes_encrypt(&k, x, out);
    crypto_wipe(&k, sizeof(k));
    crypto_wipe(k1, 16);
    crypto_wipe(k2, 16);
    crypto_wipe(x, 16);
}

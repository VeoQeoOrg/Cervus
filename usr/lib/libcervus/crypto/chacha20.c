#include <crypto.h>
#include <string.h>

#define ROTL(x,n) (((x) << (n)) | ((x) >> (32-(n))))
#define QR(a,b,c,d) \
    a += b; d ^= a; d = ROTL(d,16); \
    c += d; b ^= c; b = ROTL(b,12); \
    a += b; d ^= a; d = ROTL(d,8);  \
    c += d; b ^= c; b = ROTL(b,7);

static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void chacha20_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    memcpy(x, in, sizeof(x));
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8],  x[12]);
        QR(x[1], x[5], x[9],  x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8],  x[13]);
        QR(x[3], x[4], x[9],  x[14]);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t v = x[i] + in[i];
        out[i*4]   = (uint8_t)v;
        out[i*4+1] = (uint8_t)(v >> 8);
        out[i*4+2] = (uint8_t)(v >> 16);
        out[i*4+3] = (uint8_t)(v >> 24);
    }
}

void chacha20(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
              const uint8_t *in, uint8_t *out, size_t len) {
    uint32_t state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e; state[2] = 0x79622d32; state[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) state[4+i] = rd32le(key + i*4);
    state[12] = counter;
    state[13] = rd32le(nonce);
    state[14] = rd32le(nonce+4);
    state[15] = rd32le(nonce+8);

    uint8_t ks[64];
    size_t off = 0;
    while (off < len) {
        chacha20_block(state, ks);
        state[12]++;
        size_t n = len - off;
        if (n > 64) n = 64;
        for (size_t i = 0; i < n; i++) out[off+i] = in[off+i] ^ ks[i];
        off += n;
    }
}

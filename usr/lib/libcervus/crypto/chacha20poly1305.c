#include <crypto.h>
#include <string.h>

static void poly_key(const uint8_t key[32], const uint8_t nonce[12], uint8_t out[32]) {
    uint8_t zero[64];
    memset(zero, 0, sizeof(zero));
    uint8_t ks[64];
    chacha20(key, nonce, 0, zero, ks, 64);
    memcpy(out, ks, 32);
}

static void mac_compute(const uint8_t pkey[32], const uint8_t *aad, size_t aadlen,
                        const uint8_t *ct, size_t ctlen, uint8_t tag[16]) {
    static const uint8_t zpad[16] = {0};
    poly1305_ctx st;
    poly1305_init(&st, pkey);
    poly1305_update(&st, aad, aadlen);
    poly1305_update(&st, zpad, (16 - (aadlen & 15)) & 15);
    poly1305_update(&st, ct, ctlen);
    poly1305_update(&st, zpad, (16 - (ctlen & 15)) & 15);
    uint8_t lens[16];
    uint64_t al = aadlen, cl = ctlen;
    for (int i = 0; i < 8; i++) lens[i]   = (uint8_t)(al >> (i*8));
    for (int i = 0; i < 8; i++) lens[8+i] = (uint8_t)(cl >> (i*8));
    poly1305_update(&st, lens, 16);
    poly1305_final(&st, tag);
}

void chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                               const uint8_t *aad, size_t aadlen,
                               const uint8_t *pt, size_t ptlen,
                               uint8_t *ct, uint8_t tag[16]) {
    uint8_t pkey[32];
    poly_key(key, nonce, pkey);
    chacha20(key, nonce, 1, pt, ct, ptlen);
    mac_compute(pkey, aad, aadlen, ct, ptlen, tag);
}

int chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aadlen,
                              const uint8_t *ct, size_t ctlen, const uint8_t tag[16],
                              uint8_t *pt) {
    uint8_t pkey[32];
    poly_key(key, nonce, pkey);
    uint8_t want[16];
    mac_compute(pkey, aad, aadlen, ct, ctlen, want);
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= want[i] ^ tag[i];
    if (diff) return -1;
    chacha20(key, nonce, 1, ct, pt, ctlen);
    return 0;
}

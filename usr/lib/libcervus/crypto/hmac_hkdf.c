#include <crypto.h>
#include <string.h>

void hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t msglen, uint8_t out[32]) {
    uint8_t k0[64];
    memset(k0, 0, sizeof(k0));
    if (keylen > 64) {
        sha256(key, keylen, k0);
    } else {
        memcpy(k0, key, keylen);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k0[i] ^ 0x36; opad[i] = k0[i] ^ 0x5c; }

    sha256_ctx c;
    uint8_t inner[32];
    sha256_init(&c);
    sha256_update(&c, ipad, 64);
    sha256_update(&c, msg, msglen);
    sha256_final(&c, inner);

    sha256_init(&c);
    sha256_update(&c, opad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
}

void hkdf_extract(const uint8_t *salt, size_t saltlen, const uint8_t *ikm, size_t ikmlen, uint8_t prk[32]) {
    uint8_t zero[32];
    if (!salt || saltlen == 0) { memset(zero, 0, 32); salt = zero; saltlen = 32; }
    hmac_sha256(salt, saltlen, ikm, ikmlen, prk);
}

void hkdf_expand(const uint8_t prk[32], const uint8_t *info, size_t infolen, uint8_t *out, size_t outlen) {
    uint8_t t[32];
    size_t tlen = 0;
    uint8_t ctr = 1;
    size_t done = 0;
    while (done < outlen) {
        uint8_t buf[32 + 512 + 1];
        size_t p = 0;
        if (tlen) { memcpy(buf, t, tlen); p = tlen; }
        if (infolen > 512) infolen = 512;
        memcpy(buf + p, info, infolen); p += infolen;
        buf[p++] = ctr;
        hmac_sha256(prk, 32, buf, p, t);
        tlen = 32;
        size_t take = outlen - done;
        if (take > 32) take = 32;
        memcpy(out + done, t, take);
        done += take;
        ctr++;
    }
}

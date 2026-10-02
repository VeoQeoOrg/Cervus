#ifndef KERNEL_CRYPTO_CRYPTO_H
#define KERNEL_CRYPTO_CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t  buf[64];
    size_t   used;
} sha1_ctx_t;

void sha1_init(sha1_ctx_t *c);
void sha1_update(sha1_ctx_t *c, const void *data, size_t n);
void sha1_final(sha1_ctx_t *c, uint8_t out[20]);
void sha1(const void *data, size_t n, uint8_t out[20]);

void hmac_sha1_vec(const uint8_t *key, size_t keylen, size_t count,
                   const uint8_t *const *data, const size_t *len, uint8_t out[20]);
void hmac_sha1(const uint8_t *key, size_t keylen, const void *data, size_t n, uint8_t out[20]);
void sha1_prf(const uint8_t *key, size_t keylen, const char *label,
              const uint8_t *data, size_t datalen, uint8_t *out, size_t outlen);
void pbkdf2_sha1(const char *pass, const uint8_t *salt, size_t saltlen,
                 unsigned iterations, uint8_t *out, size_t outlen);

typedef struct {
    uint32_t rk[60];
    int      rounds;
} aes_key_t;

int  aes_set_key(aes_key_t *k, const uint8_t *key, size_t keylen);
void aes_encrypt(const aes_key_t *k, const uint8_t in[16], uint8_t out[16]);
void aes_decrypt(const aes_key_t *k, const uint8_t in[16], uint8_t out[16]);
int  aes_key_wrap(const uint8_t *kek, size_t keklen, const uint8_t *plain, size_t n, uint8_t *out);
int  aes_key_unwrap(const uint8_t *kek, size_t keklen, const uint8_t *cipher, size_t n, uint8_t *out);
void aes_cmac(const uint8_t *key, size_t keylen, const uint8_t *data, size_t n, uint8_t out[16]);

bool crypto_memeq(const void *a, const void *b, size_t n);
void crypto_wipe(void *p, size_t n);

#endif

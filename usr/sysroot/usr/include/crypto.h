#ifndef _CERVUS_CRYPTO_H
#define _CERVUS_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK 64
#define SHA256_LEN   32
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } sha256_ctx;
void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256(const void *data, size_t len, uint8_t out[32]);

#define SHA512_BLOCK 128
#define SHA512_LEN   64
typedef struct { uint64_t h[8]; uint64_t len_hi, len_lo; uint8_t buf[128]; size_t n; } sha512_ctx;
void sha512_init(sha512_ctx *c);
void sha512_update(sha512_ctx *c, const void *data, size_t len);
void sha512_final(sha512_ctx *c, uint8_t out[64]);
void sha512(const void *data, size_t len, uint8_t out[64]);
void sha384_init(sha512_ctx *c);
void sha384(const void *data, size_t len, uint8_t out[48]);

void hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t msglen, uint8_t out[32]);
void hkdf_extract(const uint8_t *salt, size_t saltlen, const uint8_t *ikm, size_t ikmlen, uint8_t prk[32]);
void hkdf_expand(const uint8_t prk[32], const uint8_t *info, size_t infolen, uint8_t *out, size_t outlen);

void chacha20(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
              const uint8_t *in, uint8_t *out, size_t len);

typedef struct {
    uint32_t r[5], h[5], pad[4];
    uint8_t buffer[16];
    size_t leftover;
    int final;
} poly1305_ctx;
void poly1305_init(poly1305_ctx *c, const uint8_t key[32]);
void poly1305_update(poly1305_ctx *c, const uint8_t *m, size_t bytes);
void poly1305_final(poly1305_ctx *c, uint8_t mac[16]);
void poly1305(const uint8_t key[32], const uint8_t *msg, size_t len, uint8_t tag[16]);

void chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                               const uint8_t *aad, size_t aadlen,
                               const uint8_t *pt, size_t ptlen,
                               uint8_t *ct, uint8_t tag[16]);
int  chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                               const uint8_t *aad, size_t aadlen,
                               const uint8_t *ct, size_t ctlen, const uint8_t tag[16],
                               uint8_t *pt);

void aes_gcm_encrypt(const uint8_t *key, int keybytes, const uint8_t nonce[12],
                     const uint8_t *aad, size_t aadlen,
                     const uint8_t *pt, size_t ptlen, uint8_t *ct, uint8_t tag[16]);
int  aes_gcm_decrypt(const uint8_t *key, int keybytes, const uint8_t nonce[12],
                     const uint8_t *aad, size_t aadlen,
                     const uint8_t *ct, size_t ctlen, const uint8_t tag[16], uint8_t *pt);

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void x25519_base(uint8_t out[32], const uint8_t scalar[32]);

int  ed25519_verify(const uint8_t sig[64], const uint8_t *msg, size_t msglen, const uint8_t pub[32]);
void ed25519_keypair(uint8_t pub[32], uint8_t priv[64], const uint8_t seed[32]);
void ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t msglen, const uint8_t priv[64]);

int  rsa_pkcs1v15_verify(const uint8_t *n, size_t nlen, const uint8_t *e, size_t elen,
                         const uint8_t *sig, size_t siglen, const uint8_t *hash, size_t hlen, int hash_id);
int  rsa_pss_sha256_verify(const uint8_t *n, size_t nlen, const uint8_t *e, size_t elen,
                           const uint8_t *sig, size_t siglen, const uint8_t *mhash);
int  ecdsa_p256_verify(const uint8_t *pub, size_t publen,
                       const uint8_t *sig_der, size_t siglen, const uint8_t *hash, size_t hlen);
int  ecdsa_p384_verify(const uint8_t *pub, size_t publen,
                       const uint8_t *sig_der, size_t siglen, const uint8_t *hash, size_t hlen);

void crypto_random(void *buf, size_t len);

#endif

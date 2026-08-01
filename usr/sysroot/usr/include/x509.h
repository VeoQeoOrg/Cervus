#ifndef _CERVUS_X509_H
#define _CERVUS_X509_H

#include <stdint.h>
#include <stddef.h>

enum {
    SIG_RSA_SHA256, SIG_RSA_SHA384, SIG_RSA_SHA512, SIG_RSA_PSS_SHA256,
    SIG_ECDSA_SHA256, SIG_ECDSA_SHA384, SIG_ED25519, SIG_UNKNOWN
};
enum { KEY_RSA, KEY_EC_P256, KEY_EC_P384, KEY_ED25519, KEY_UNKNOWN };

typedef struct {
    const uint8_t *tbs; size_t tbs_len;
    int sig_alg;
    const uint8_t *sig; size_t sig_len;
    const uint8_t *issuer; size_t issuer_len;
    const uint8_t *subject; size_t subject_len;
    int64_t not_before, not_after;
    int key_type;
    const uint8_t *rsa_n; size_t rsa_n_len;
    const uint8_t *rsa_e; size_t rsa_e_len;
    const uint8_t *ec_point; size_t ec_point_len;
    const uint8_t *ed_pub;
    int is_ca;
    const uint8_t *san; size_t san_len;
} x509_cert;

int x509_parse(const uint8_t *der, size_t len, x509_cert *c);

int x509_verify_sig(const x509_cert *signer, int sig_alg,
                    const uint8_t *data, size_t dlen, const uint8_t *sig, size_t slen);

int x509_check_host(const x509_cert *leaf, const char *host);

#define X509_STORE_MAX 700
typedef struct {
    uint8_t *pem;
    x509_cert certs[X509_STORE_MAX];
    int count;
} x509_store;

int x509_store_load_pem(x509_store *st, uint8_t *pembuf, size_t pemlen);

int x509_verify_chain(const x509_cert *chain, int n, const x509_store *st,
                      const char *host, int64_t now, char *err, size_t errlen);

#endif

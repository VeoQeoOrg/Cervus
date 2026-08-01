#include <crypto.h>
#include <string.h>

#define W 256

typedef struct { uint32_t d[W]; } bn;

static void bn_zero(bn *a) { memset(a->d, 0, sizeof a->d); }

static int bn_topword(const bn *a) {
    int i = W - 1;
    while (i > 0 && a->d[i] == 0) i--;
    return i;
}

static int bn_topbit(const bn *a) {
    int i = bn_topword(a);
    uint32_t w = a->d[i];
    if (i == 0 && w == 0) return -1;
    int b = 31;
    while (b > 0 && !((w >> b) & 1)) b--;
    return i * 32 + b;
}

static int bn_bit(const bn *a, int i) { return (a->d[i >> 5] >> (i & 31)) & 1; }

static int bn_cmp(const bn *a, const bn *b) {
    for (int i = W - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

static void bn_sub(bn *a, const bn *b) {
    uint64_t borrow = 0;
    for (int i = 0; i < W; i++) {
        uint64_t t = (uint64_t)a->d[i] - b->d[i] - borrow;
        a->d[i] = (uint32_t)t;
        borrow = (t >> 63) & 1;
    }
}

static void bn_shl1(bn *a) {
    uint32_t carry = 0;
    for (int i = 0; i < W; i++) {
        uint32_t nc = a->d[i] >> 31;
        a->d[i] = (a->d[i] << 1) | carry;
        carry = nc;
    }
}

static void bn_from_be(bn *a, const uint8_t *b, size_t len) {
    bn_zero(a);
    for (size_t i = 0; i < len; i++) {
        size_t pos = len - 1 - i;
        a->d[i >> 2] |= (uint32_t)b[pos] << ((i & 3) * 8);
    }
}

static void bn_to_be(const bn *a, uint8_t *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        size_t pos = len - 1 - i;
        b[pos] = (uint8_t)(a->d[i >> 2] >> ((i & 3) * 8));
    }
}

static void bn_mul(const bn *a, const bn *b, bn *r) {
    bn_zero(r);
    int ta = bn_topword(a), tb = bn_topword(b);
    for (int i = 0; i <= ta; i++) {
        if (a->d[i] == 0) continue;
        uint64_t carry = 0;
        for (int j = 0; j <= tb; j++) {
            if (i + j >= W) break;
            uint64_t t = (uint64_t)a->d[i] * b->d[j] + r->d[i + j] + carry;
            r->d[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        if (i + tb + 1 < W) r->d[i + tb + 1] += (uint32_t)carry;
    }
}

static void bn_mod(const bn *a, const bn *m, bn *r) {
    bn_zero(r);
    int tb = bn_topbit(a);
    if (tb < 0) return;
    for (int i = tb; i >= 0; i--) {
        bn_shl1(r);
        r->d[0] |= (uint32_t)bn_bit(a, i);
        if (bn_cmp(r, m) >= 0) bn_sub(r, m);
    }
}

static void bn_mulmod(const bn *a, const bn *b, const bn *m, bn *r) {
    bn p;
    bn_mul(a, b, &p);
    bn_mod(&p, m, r);
}

static void bn_modexp(const bn *base, const uint8_t *e, size_t elen, const bn *m, bn *r) {
    bn cur, tmp;
    bn_zero(r); r->d[0] = 1;
    int started = 0;
    for (size_t bi = 0; bi < elen; bi++) {
        for (int b = 7; b >= 0; b--) {
            if (started) { bn_mulmod(r, r, m, &tmp); *r = tmp; }
            int bit = (e[bi] >> b) & 1;
            if (bit) {
                if (!started) { *r = *base; bn_mod(r, m, &cur); *r = cur; started = 1; }
                else { bn_mulmod(r, base, m, &tmp); *r = tmp; }
            }
        }
    }
}

static int rsa_public(const uint8_t *n, size_t nlen, const uint8_t *e, size_t elen,
                      const uint8_t *sig, size_t siglen, uint8_t *em) {
    if (nlen > W * 4 || siglen > nlen) return -1;
    bn N, S, R;
    bn_from_be(&N, n, nlen);
    bn_from_be(&S, sig, siglen);
    if (bn_cmp(&S, &N) >= 0) return -1;
    bn_modexp(&S, e, elen, &N, &R);
    bn_to_be(&R, em, nlen);
    return 0;
}

static const uint8_t DI_SHA256[] = {
    0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20 };
static const uint8_t DI_SHA384[] = {
    0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30 };
static const uint8_t DI_SHA512[] = {
    0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40 };

int rsa_pkcs1v15_verify(const uint8_t *n, size_t nlen, const uint8_t *e, size_t elen,
                        const uint8_t *sig, size_t siglen, const uint8_t *hash, size_t hlen, int hash_id) {
    uint8_t em[W * 4];
    if (rsa_public(n, nlen, e, elen, sig, siglen, em)) return -1;
    const uint8_t *di; size_t dilen;
    if (hash_id == 0) { di = DI_SHA256; dilen = sizeof DI_SHA256; }
    else if (hash_id == 1) { di = DI_SHA384; dilen = sizeof DI_SHA384; }
    else if (hash_id == 2) { di = DI_SHA512; dilen = sizeof DI_SHA512; }
    else return -1;
    size_t tlen = dilen + hlen;
    if (nlen < tlen + 11) return -1;
    if (em[0] != 0x00 || em[1] != 0x01) return -1;
    size_t i = 2;
    while (i < nlen - tlen - 1 && em[i] == 0xff) i++;
    if (em[i] != 0x00) return -1;
    i++;
    if (i + tlen != nlen) return -1;
    if (memcmp(em + i, di, dilen) != 0) return -1;
    if (memcmp(em + i + dilen, hash, hlen) != 0) return -1;
    return 0;
}

static void mgf1_sha256(const uint8_t *seed, size_t seedlen, uint8_t *mask, size_t masklen) {
    uint8_t cnt[4];
    size_t done = 0;
    uint32_t c = 0;
    while (done < masklen) {
        cnt[0]=(uint8_t)(c>>24); cnt[1]=(uint8_t)(c>>16); cnt[2]=(uint8_t)(c>>8); cnt[3]=(uint8_t)c;
        sha256_ctx h; sha256_init(&h);
        sha256_update(&h, seed, seedlen);
        sha256_update(&h, cnt, 4);
        uint8_t dig[32]; sha256_final(&h, dig);
        size_t take = masklen - done; if (take > 32) take = 32;
        memcpy(mask + done, dig, take);
        done += take; c++;
    }
}

int rsa_pss_sha256_verify(const uint8_t *n, size_t nlen, const uint8_t *e, size_t elen,
                          const uint8_t *sig, size_t siglen, const uint8_t *mhash) {
    uint8_t em[W * 4];
    if (rsa_public(n, nlen, e, elen, sig, siglen, em)) return -1;
    size_t emLen = nlen, hLen = 32, sLen = 32;
    if (emLen < hLen + sLen + 2) return -1;
    if (em[emLen - 1] != 0xbc) return -1;
    size_t dbLen = emLen - hLen - 1;
    const uint8_t *maskedDB = em;
    const uint8_t *H = em + dbLen;
    uint8_t db[W * 4];
    uint8_t dbmask[W * 4];
    mgf1_sha256(H, hLen, dbmask, dbLen);
    for (size_t i = 0; i < dbLen; i++) db[i] = maskedDB[i] ^ dbmask[i];
    int embits = (int)(nlen * 8 - 1);
    int topbits = embits & 7;
    if (topbits) db[0] &= (uint8_t)(0xff >> (8 - topbits));
    else db[0] &= 0x00;
    size_t i = 0;
    while (i < dbLen - sLen - 1 && db[i] == 0) i++;
    if (db[i] != 0x01) return -1;
    const uint8_t *salt = db + dbLen - sLen;
    uint8_t mp[8 + 32 + 32];
    memset(mp, 0, 8);
    memcpy(mp + 8, mhash, 32);
    memcpy(mp + 40, salt, sLen);
    uint8_t hh[32]; sha256(mp, 8 + 32 + sLen, hh);
    if (memcmp(hh, H, hLen) != 0) return -1;
    return 0;
}

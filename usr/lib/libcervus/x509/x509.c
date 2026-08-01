#include <x509.h>
#include <crypto.h>
#include <string.h>
#include <stdio.h>

static const uint8_t OID_RSA_SHA256[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0b};
static const uint8_t OID_RSA_SHA384[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0c};
static const uint8_t OID_RSA_SHA512[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0d};
static const uint8_t OID_RSA_PSS[]    = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0a};
static const uint8_t OID_ECDSA_256[]  = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x02};
static const uint8_t OID_ECDSA_384[]  = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x03};
static const uint8_t OID_ED25519[]    = {0x2b,0x65,0x70};
static const uint8_t OID_RSA_ENC[]    = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01};
static const uint8_t OID_EC_PUB[]     = {0x2a,0x86,0x48,0xce,0x3d,0x02,0x01};
static const uint8_t OID_P256[]       = {0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07};
static const uint8_t OID_P384[]       = {0x2b,0x81,0x04,0x00,0x22};
static const uint8_t OID_BASIC[]      = {0x55,0x1d,0x13};
static const uint8_t OID_SAN[]        = {0x55,0x1d,0x11};

static int oideq(const uint8_t *a, size_t al, const uint8_t *b, size_t bl) {
    return al == bl && memcmp(a, b, al) == 0;
}

static int der_len(const uint8_t **p, const uint8_t *end, size_t *out) {
    if (*p >= end) return -1;
    uint8_t b = *(*p)++;
    if (b < 0x80) { *out = b; return 0; }
    int n = b & 0x7f;
    if (n == 0 || n > 4) return -1;
    size_t v = 0;
    for (int i = 0; i < n; i++) { if (*p >= end) return -1; v = (v << 8) | *(*p)++; }
    *out = v; return 0;
}

static int der_tlv(const uint8_t **p, const uint8_t *end, uint8_t *tag, const uint8_t **vp, size_t *vlen) {
    if (*p >= end) return -1;
    *tag = *(*p)++;
    size_t l;
    if (der_len(p, end, &l)) return -1;
    if (*p + l > end) return -1;
    *vp = *p; *vlen = l; *p += l;
    return 0;
}

static void strip_zero(const uint8_t **p, size_t *l) {
    while (*l > 1 && (*p)[0] == 0) { (*p)++; (*l)--; }
}

static int64_t days_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int dig2(const uint8_t *p) { return (p[0]-'0')*10 + (p[1]-'0'); }

static int64_t parse_time(const uint8_t *v, size_t l, uint8_t tag) {
    int y, mo, d, h, mi, s;
    const uint8_t *p = v;
    if (tag == 0x17 && l >= 13) {
        int yy = dig2(p); p += 2;
        y = yy < 50 ? 2000 + yy : 1900 + yy;
    } else if (tag == 0x18 && l >= 15) {
        y = dig2(p) * 100 + dig2(p + 2); p += 4;
    } else return 0;
    mo = dig2(p); p += 2; d = dig2(p); p += 2;
    h = dig2(p); p += 2; mi = dig2(p); p += 2; s = dig2(p);
    return days_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

static int sig_alg_from_oid(const uint8_t *o, size_t l) {
    if (oideq(o,l,OID_RSA_SHA256,sizeof OID_RSA_SHA256)) return SIG_RSA_SHA256;
    if (oideq(o,l,OID_RSA_SHA384,sizeof OID_RSA_SHA384)) return SIG_RSA_SHA384;
    if (oideq(o,l,OID_RSA_SHA512,sizeof OID_RSA_SHA512)) return SIG_RSA_SHA512;
    if (oideq(o,l,OID_RSA_PSS,sizeof OID_RSA_PSS))       return SIG_RSA_PSS_SHA256;
    if (oideq(o,l,OID_ECDSA_256,sizeof OID_ECDSA_256))   return SIG_ECDSA_SHA256;
    if (oideq(o,l,OID_ECDSA_384,sizeof OID_ECDSA_384))   return SIG_ECDSA_SHA384;
    if (oideq(o,l,OID_ED25519,sizeof OID_ED25519))       return SIG_ED25519;
    return SIG_UNKNOWN;
}

static int parse_alg_oid(const uint8_t *seq, size_t seqlen, const uint8_t **oid, size_t *oidlen,
                         const uint8_t **params, size_t *paramslen) {
    const uint8_t *p = seq, *e = seq + seqlen, *vp; size_t vl; uint8_t t;
    if (der_tlv(&p, e, &t, &vp, &vl) || t != 0x06) return -1;
    *oid = vp; *oidlen = vl;
    if (params) { *params = 0; *paramslen = 0; if (p < e) { const uint8_t *q=p; if (!der_tlv(&q,e,&t,&vp,&vl)) { *params = p; *paramslen = (size_t)(e - p); } } }
    return 0;
}

static int parse_spki(const uint8_t *v, size_t l, x509_cert *c) {
    const uint8_t *p = v, *e = v + l, *vp; size_t vl; uint8_t t;
    if (der_tlv(&p, e, &t, &vp, &vl) || t != 0x30) return -1;
    const uint8_t *alg = vp; size_t alglen = vl;
    if (der_tlv(&p, e, &t, &vp, &vl) || t != 0x03) return -1;
    const uint8_t *key = vp + 1; size_t keylen = vl - 1;
    const uint8_t *oid, *params; size_t oidlen, paramslen;
    if (parse_alg_oid(alg, alglen, &oid, &oidlen, &params, &paramslen)) return -1;
    if (oideq(oid, oidlen, OID_RSA_ENC, sizeof OID_RSA_ENC)) {
        c->key_type = KEY_RSA;
        const uint8_t *kp = key, *ke = key + keylen;
        if (der_tlv(&kp, ke, &t, &vp, &vl) || t != 0x30) return -1;
        const uint8_t *ip = vp, *ie = vp + vl;
        if (der_tlv(&ip, ie, &t, &vp, &vl) || t != 0x02) return -1;
        c->rsa_n = vp; c->rsa_n_len = vl; strip_zero(&c->rsa_n, &c->rsa_n_len);
        if (der_tlv(&ip, ie, &t, &vp, &vl) || t != 0x02) return -1;
        c->rsa_e = vp; c->rsa_e_len = vl; strip_zero(&c->rsa_e, &c->rsa_e_len);
        return 0;
    }
    if (oideq(oid, oidlen, OID_EC_PUB, sizeof OID_EC_PUB)) {
        if (params && paramslen >= 2 && oideq(params + 2, paramslen - 2, OID_P256, sizeof OID_P256)) {
            c->key_type = KEY_EC_P256;
        } else if (params && paramslen >= 2 && oideq(params + 2, paramslen - 2, OID_P384, sizeof OID_P384)) {
            c->key_type = KEY_EC_P384;
        } else {
            c->key_type = KEY_UNKNOWN;
        }
        c->ec_point = key; c->ec_point_len = keylen;
        return 0;
    }
    if (oideq(oid, oidlen, OID_ED25519, sizeof OID_ED25519)) {
        if (keylen != 32) return -1;
        c->key_type = KEY_ED25519; c->ed_pub = key;
        return 0;
    }
    c->key_type = KEY_UNKNOWN;
    return 0;
}

static void parse_extensions(const uint8_t *v, size_t l, x509_cert *c) {
    const uint8_t *p = v, *e = v + l, *vp; size_t vl; uint8_t t;
    if (der_tlv(&p, e, &t, &vp, &vl) || t != 0x30) return;
    const uint8_t *sp = vp, *se = vp + vl;
    while (sp < se) {
        const uint8_t *xp; size_t xl;
        if (der_tlv(&sp, se, &t, &xp, &xl) || t != 0x30) return;
        const uint8_t *ep = xp, *ee = xp + xl, *ov; size_t ol;
        if (der_tlv(&ep, ee, &t, &ov, &ol) || t != 0x06) continue;
        const uint8_t *val; size_t vall; uint8_t vt;
        if (der_tlv(&ep, ee, &vt, &val, &vall)) continue;
        if (vt == 0x01) { if (der_tlv(&ep, ee, &vt, &val, &vall)) continue; }
        if (vt != 0x04) continue;
        if (oideq(ov, ol, OID_BASIC, sizeof OID_BASIC)) {
            const uint8_t *bp = val, *be = val + vall, *bv; size_t bl2; uint8_t bt;
            if (!der_tlv(&bp, be, &bt, &bv, &bl2) && bt == 0x30) {
                const uint8_t *ip = bv, *ie = bv + bl2, *iv; size_t il; uint8_t it;
                if (ip < ie && !der_tlv(&ip, ie, &it, &iv, &il) && it == 0x01 && il == 1 && iv[0] != 0)
                    c->is_ca = 1;
            }
        } else if (oideq(ov, ol, OID_SAN, sizeof OID_SAN)) {
            c->san = val; c->san_len = vall;
        }
    }
}

int x509_parse(const uint8_t *der, size_t len, x509_cert *c) {
    memset(c, 0, sizeof *c);
    const uint8_t *p = der, *e = der + len, *vp; size_t vl; uint8_t t;
    if (der_tlv(&p, e, &t, &vp, &vl) || t != 0x30) return -1;
    const uint8_t *cp = vp, *ce = vp + vl;

    const uint8_t *tbs_start = cp;
    const uint8_t *tp = cp;
    if (der_tlv(&tp, ce, &t, &vp, &vl) || t != 0x30) return -1;
    c->tbs = tbs_start; c->tbs_len = (size_t)(tp - tbs_start);
    const uint8_t *ip = vp, *ie = vp + vl;

    const uint8_t *svp; size_t svl; uint8_t st;
    if (der_tlv(&tp, ce, &st, &svp, &svl) || st != 0x30) return -1;
    const uint8_t *oid; size_t oidlen;
    if (parse_alg_oid(svp, svl, &oid, &oidlen, 0, 0)) return -1;
    c->sig_alg = sig_alg_from_oid(oid, oidlen);
    if (der_tlv(&tp, ce, &st, &svp, &svl) || st != 0x03) return -1;
    c->sig = svp + 1; c->sig_len = svl - 1;

    if (der_tlv(&ip, ie, &t, &vp, &vl)) return -1;
    if (t == 0xa0) { if (der_tlv(&ip, ie, &t, &vp, &vl)) return -1; }
    if (t != 0x02) return -1;
    if (der_tlv(&ip, ie, &t, &vp, &vl) || t != 0x30) return -1;
    const uint8_t *iss_start = ip;
    if (der_tlv(&ip, ie, &t, &vp, &vl) || t != 0x30) return -1;
    c->issuer = iss_start; c->issuer_len = (size_t)(ip - iss_start);
    const uint8_t *val_start = ip;
    if (der_tlv(&ip, ie, &t, &vp, &vl) || t != 0x30) return -1;
    {
        const uint8_t *vpp = vp, *vpe = vp + vl, *tv; size_t tl; uint8_t tt;
        if (der_tlv(&vpp, vpe, &tt, &tv, &tl)) return -1;
        c->not_before = parse_time(tv, tl, tt);
        if (der_tlv(&vpp, vpe, &tt, &tv, &tl)) return -1;
        c->not_after = parse_time(tv, tl, tt);
    }
    (void)val_start;
    const uint8_t *subj_start = ip;
    if (der_tlv(&ip, ie, &t, &vp, &vl) || t != 0x30) return -1;
    c->subject = subj_start; c->subject_len = (size_t)(ip - subj_start);
    if (der_tlv(&ip, ie, &t, &vp, &vl) || t != 0x30) return -1;
    if (parse_spki(vp, vl, c)) return -1;

    while (ip < ie) {
        if (der_tlv(&ip, ie, &t, &vp, &vl)) break;
        if (t == 0xa3) { parse_extensions(vp, vl, c); break; }
    }
    return 0;
}

static int hash_for(int sig_alg, const uint8_t *data, size_t dlen, uint8_t *out, size_t *outlen, int *hash_id) {
    switch (sig_alg) {
        case SIG_RSA_SHA256: case SIG_ECDSA_SHA256: case SIG_RSA_PSS_SHA256:
            sha256(data, dlen, out); *outlen = 32; *hash_id = 0; return 0;
        case SIG_RSA_SHA384: case SIG_ECDSA_SHA384:
            sha384(data, dlen, out); *outlen = 48; *hash_id = 1; return 0;
        case SIG_RSA_SHA512:
            sha512(data, dlen, out); *outlen = 64; *hash_id = 2; return 0;
        default: return -1;
    }
}

int x509_verify_sig(const x509_cert *signer, int sig_alg,
                    const uint8_t *data, size_t dlen, const uint8_t *sig, size_t slen) {
    uint8_t h[64]; size_t hlen; int hid;
    if (sig_alg == SIG_ED25519) {
        if (signer->key_type != KEY_ED25519) return -1;
        return ed25519_verify(sig, data, dlen, signer->ed_pub) == 0 ? 0 : -1;
    }
    if (hash_for(sig_alg, data, dlen, h, &hlen, &hid)) return -1;
    if (sig_alg == SIG_ECDSA_SHA256 || sig_alg == SIG_ECDSA_SHA384) {
        if (signer->key_type == KEY_EC_P256)
            return ecdsa_p256_verify(signer->ec_point, signer->ec_point_len, sig, slen, h, hlen);
        if (signer->key_type == KEY_EC_P384)
            return ecdsa_p384_verify(signer->ec_point, signer->ec_point_len, sig, slen, h, hlen);
        return -1;
    }
    if (sig_alg == SIG_RSA_PSS_SHA256) {
        if (signer->key_type != KEY_RSA) return -1;
        return rsa_pss_sha256_verify(signer->rsa_n, signer->rsa_n_len, signer->rsa_e, signer->rsa_e_len, sig, slen, h);
    }
    if (sig_alg == SIG_RSA_SHA256 || sig_alg == SIG_RSA_SHA384 || sig_alg == SIG_RSA_SHA512) {
        if (signer->key_type != KEY_RSA) return -1;
        return rsa_pkcs1v15_verify(signer->rsa_n, signer->rsa_n_len, signer->rsa_e, signer->rsa_e_len, sig, slen, h, hlen, hid);
    }
    return -1;
}

static int host_match(const char *pat, size_t patlen, const char *host) {
    size_t hlen = strlen(host);
    if (patlen >= 2 && pat[0] == '*' && pat[1] == '.') {
        const char *dot = strchr(host, '.');
        if (!dot) return 0;
        size_t suf = patlen - 1;
        size_t hsuf = hlen - (size_t)(dot - host);
        if (suf != hsuf) return 0;
        return memcmp(pat + 1, dot, suf) == 0 ? 1 : 0;
    }
    if (patlen != hlen) return 0;
    for (size_t i = 0; i < hlen; i++) {
        char a = pat[i], b = host[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

int x509_check_host(const x509_cert *leaf, const char *host) {
    if (!leaf->san) return -1;
    const uint8_t *p = leaf->san, *e = leaf->san + leaf->san_len, *vp; size_t vl; uint8_t t;
    if (der_tlv(&p, e, &t, &vp, &vl) || t != 0x30) return -1;
    const uint8_t *sp = vp, *se = vp + vl;
    while (sp < se) {
        if (der_tlv(&sp, se, &t, &vp, &vl)) break;
        if (t == 0x82) {
            if (host_match((const char *)vp, vl, host)) return 0;
        }
    }
    return -1;
}

static int b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int x509_store_load_pem(x509_store *st, uint8_t *pembuf, size_t pemlen) {
    st->pem = pembuf;
    st->count = 0;
    uint8_t *out = pembuf;
    const char *BEG = "-----BEGIN CERTIFICATE-----";
    const char *END = "-----END CERTIFICATE-----";
    char *p = (char *)pembuf;
    char *end = (char *)pembuf + pemlen;
    for (;;) {
        char *b = 0;
        for (char *q = p; q + 27 <= end; q++) { if (memcmp(q, BEG, 27) == 0) { b = q; break; } }
        if (!b) break;
        char *body = b + 27;
        char *fin = 0;
        for (char *q = body; q + 25 <= end; q++) { if (memcmp(q, END, 25) == 0) { fin = q; break; } }
        if (!fin) break;
        uint8_t *dstart = out;
        int acc = 0, nb = 0;
        for (char *q = body; q < fin; q++) {
            int v = b64val((unsigned char)*q);
            if (v < 0) continue;
            acc = (acc << 6) | v; nb += 6;
            if (nb >= 8) { nb -= 8; *out++ = (uint8_t)(acc >> nb); }
        }
        size_t dlen = (size_t)(out - dstart);
        if (st->count < X509_STORE_MAX && x509_parse(dstart, dlen, &st->certs[st->count]) == 0)
            st->count++;
        else
            out = dstart;
        p = fin + 25;
    }
    return st->count;
}

static int dn_eq(const uint8_t *a, size_t al, const uint8_t *b, size_t bl) {
    return al == bl && memcmp(a, b, al) == 0;
}

int x509_verify_chain(const x509_cert *chain, int n, const x509_store *st,
                      const char *host, int64_t now, char *err, size_t errlen) {
    if (n < 1) { if (err) snprintf(err, errlen, "empty chain"); return -1; }
    if (host && x509_check_host(&chain[0], host) != 0) {
        if (err) snprintf(err, errlen, "hostname does not match certificate");
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (now && (now < chain[i].not_before || now > chain[i].not_after)) {
            if (err) snprintf(err, errlen, "certificate %d expired or not yet valid", i);
            return -1;
        }
        for (int j = 0; j < st->count; j++) {
            const x509_cert *root = &st->certs[j];
            if (!dn_eq(root->subject, root->subject_len, chain[i].issuer, chain[i].issuer_len)) continue;
            if (now && (now < root->not_before || now > root->not_after)) continue;
            if (x509_verify_sig(root, chain[i].sig_alg, chain[i].tbs, chain[i].tbs_len, chain[i].sig, chain[i].sig_len) == 0)
                return 0;
        }
        if (i + 1 < n) {
            if (!dn_eq(chain[i].issuer, chain[i].issuer_len, chain[i+1].subject, chain[i+1].subject_len)) {
                if (err) snprintf(err, errlen, "broken chain link at %d", i);
                return -1;
            }
            if (x509_verify_sig(&chain[i+1], chain[i].sig_alg, chain[i].tbs, chain[i].tbs_len, chain[i].sig, chain[i].sig_len) != 0) {
                if (err) snprintf(err, errlen, "bad signature on certificate %d", i);
                return -1;
            }
        }
    }
    if (err) snprintf(err, errlen, "no trusted root found for chain");
    return -1;
}

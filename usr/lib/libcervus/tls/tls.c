#include <tls.h>
#include <crypto.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

#define MAXREC 16640
#define HSBUF  32768

struct tls_conn {
    int fd;
    const char *host;
    sha256_ctx th;

    uint8_t cli_priv[32], cli_pub[32], server_pub[32];
    uint8_t hs_secret[32];
    uint8_t chs[32], shs[32];

    uint8_t wkey[32], wiv[12]; uint64_t wseq; int wkeys_set;
    uint8_t rkey[32], riv[12]; uint64_t rseq; int rkeys_set;
    int established;

    uint8_t rec[MAXREC];
    uint8_t sbuf[MAXREC];
    uint8_t plain[MAXREC];
    size_t  plain_off, plain_len;

    uint8_t hsbuf[HSBUF];
    size_t  hs_len, hs_pos;
    const uint8_t *hs_last_msg;
    size_t  hs_last_total;

    char err[128];
};

static int fail(tls_conn *c, const char *m) {
    size_t i = 0;
    while (m[i] && i < sizeof(c->err)-1) { c->err[i] = m[i]; i++; }
    c->err[i] = 0;
    return -1;
}

static int io_read_full(int fd, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        long r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int io_write_full(int fd, const uint8_t *buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        long r = send(fd, buf + sent, n - sent, 0);
        if (r <= 0) return -1;
        sent += (size_t)r;
    }
    return 0;
}

static void expand_label(const uint8_t secret[32], const char *label,
                         const uint8_t *ctx, size_t ctxlen, uint8_t *out, size_t outlen) {
    uint8_t info[300];
    size_t i = 0, ll = strlen(label);
    info[i++] = (uint8_t)(outlen >> 8);
    info[i++] = (uint8_t)outlen;
    info[i++] = (uint8_t)(6 + ll);
    memcpy(info + i, "tls13 ", 6); i += 6;
    memcpy(info + i, label, ll); i += ll;
    info[i++] = (uint8_t)ctxlen;
    if (ctxlen) { memcpy(info + i, ctx, ctxlen); i += ctxlen; }
    hkdf_expand(secret, info, i, out, outlen);
}

static void derive_secret(const uint8_t secret[32], const char *label,
                          const uint8_t thash[32], uint8_t out[32]) {
    expand_label(secret, label, thash, 32, out, 32);
}

static void set_write_keys(tls_conn *c, const uint8_t secret[32]) {
    expand_label(secret, "key", 0, 0, c->wkey, 32);
    expand_label(secret, "iv",  0, 0, c->wiv, 12);
    c->wseq = 0; c->wkeys_set = 1;
}
static void set_read_keys(tls_conn *c, const uint8_t secret[32]) {
    expand_label(secret, "key", 0, 0, c->rkey, 32);
    expand_label(secret, "iv",  0, 0, c->riv, 12);
    c->rseq = 0; c->rkeys_set = 1;
}

static void make_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12]) {
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++) nonce[11-i] ^= (uint8_t)(seq >> (8*i));
}

static int send_plain(tls_conn *c, uint8_t type, uint16_t ver, const uint8_t *data, size_t len) {
    uint8_t hdr[5] = { type, (uint8_t)(ver>>8), (uint8_t)ver, (uint8_t)(len>>8), (uint8_t)len };
    if (io_write_full(c->fd, hdr, 5)) return fail(c, "write");
    if (io_write_full(c->fd, data, len)) return fail(c, "write");
    return 0;
}

static int record_send(tls_conn *c, uint8_t inner_type, const uint8_t *data, size_t len) {
    size_t inner_len = len + 1;
    size_t ct_total = inner_len + 16;
    uint8_t hdr[5] = { 23, 0x03, 0x03, (uint8_t)(ct_total>>8), (uint8_t)ct_total };
    uint8_t nonce[12]; make_nonce(c->wiv, c->wseq, nonce);
    memcpy(c->sbuf, data, len);
    c->sbuf[len] = inner_type;
    uint8_t tag[16];
    chacha20_poly1305_encrypt(c->wkey, nonce, hdr, 5, c->sbuf, inner_len, c->sbuf, tag);
    c->wseq++;
    if (io_write_full(c->fd, hdr, 5)) return fail(c, "write");
    if (io_write_full(c->fd, c->sbuf, inner_len)) return fail(c, "write");
    if (io_write_full(c->fd, tag, 16)) return fail(c, "write");
    return 0;
}

static int record_recv(tls_conn *c, uint8_t *ctype, uint8_t *out, size_t *outlen) {
    for (;;) {
        uint8_t hdr[5];
        if (io_read_full(c->fd, hdr, 5)) return fail(c, "connection closed");
        uint8_t rt = hdr[0];
        size_t rl = ((size_t)hdr[3] << 8) | hdr[4];
        if (rl > MAXREC) return fail(c, "record too large");
        if (io_read_full(c->fd, c->rec, rl)) return fail(c, "connection closed");
        if (rt == 20) continue;
        if (!c->rkeys_set) {
            *ctype = rt; memcpy(out, c->rec, rl); *outlen = rl; return 0;
        }
        if (rt != 23) return fail(c, "unexpected record type");
        if (rl < 17) return fail(c, "short record");
        uint8_t nonce[12]; make_nonce(c->riv, c->rseq, nonce);
        size_t ctlen = rl - 16;
        if (chacha20_poly1305_decrypt(c->rkey, nonce, hdr, 5, c->rec, ctlen, c->rec + ctlen, out))
            return fail(c, "AEAD decrypt failed");
        c->rseq++;
        size_t n = ctlen;
        while (n > 0 && out[n-1] == 0) n--;
        if (n == 0) return fail(c, "empty inner record");
        *ctype = out[n-1];
        *outlen = n - 1;
        if (*ctype == 21) {
            if (*outlen >= 2 && out[1] == 0) { *outlen = 0; return 0; }
            return fail(c, "tls alert");
        }
        return 0;
    }
}

static size_t build_client_hello(tls_conn *c, uint8_t *b) {
    size_t i = 0;
    b[i++] = 1;
    size_t hs_len_at = i; i += 3;
    b[i++] = 0x03; b[i++] = 0x03;
    crypto_random(b + i, 32); i += 32;
    b[i++] = 32; crypto_random(b + i, 32); i += 32;
    b[i++] = 0x00; b[i++] = 0x02;
    b[i++] = 0x13; b[i++] = 0x03;
    b[i++] = 0x01; b[i++] = 0x00;
    size_t ext_len_at = i; i += 2;
    size_t ext_start = i;

    b[i++] = 0x00; b[i++] = 0x2b;
    b[i++] = 0x00; b[i++] = 0x03;
    b[i++] = 0x02; b[i++] = 0x03; b[i++] = 0x04;

    b[i++] = 0x00; b[i++] = 0x0a;
    b[i++] = 0x00; b[i++] = 0x04;
    b[i++] = 0x00; b[i++] = 0x02;
    b[i++] = 0x00; b[i++] = 0x1d;

    {
        static const uint8_t sa[] = {0x08,0x07, 0x04,0x03, 0x08,0x04, 0x04,0x01, 0x05,0x03, 0x08,0x05, 0x06,0x01};
        uint16_t n = sizeof(sa);
        b[i++] = 0x00; b[i++] = 0x0d;
        b[i++] = (uint8_t)((n+2)>>8); b[i++] = (uint8_t)(n+2);
        b[i++] = (uint8_t)(n>>8); b[i++] = (uint8_t)n;
        memcpy(b + i, sa, n); i += n;
    }

    b[i++] = 0x00; b[i++] = 0x33;
    b[i++] = 0x00; b[i++] = 0x26;
    b[i++] = 0x00; b[i++] = 0x24;
    b[i++] = 0x00; b[i++] = 0x1d;
    b[i++] = 0x00; b[i++] = 0x20;
    memcpy(b + i, c->cli_pub, 32); i += 32;

    {
        uint16_t hn = (uint16_t)strlen(c->host);
        uint16_t list = hn + 3;
        uint16_t extd = list + 2;
        b[i++] = 0x00; b[i++] = 0x00;
        b[i++] = (uint8_t)(extd>>8); b[i++] = (uint8_t)extd;
        b[i++] = (uint8_t)(list>>8); b[i++] = (uint8_t)list;
        b[i++] = 0x00;
        b[i++] = (uint8_t)(hn>>8); b[i++] = (uint8_t)hn;
        memcpy(b + i, c->host, hn); i += hn;
    }

    uint16_t extlen = (uint16_t)(i - ext_start);
    b[ext_len_at] = (uint8_t)(extlen>>8); b[ext_len_at+1] = (uint8_t)extlen;
    size_t hslen = i - (hs_len_at + 3);
    b[hs_len_at] = (uint8_t)(hslen>>16); b[hs_len_at+1] = (uint8_t)(hslen>>8); b[hs_len_at+2] = (uint8_t)hslen;
    return i;
}

static int parse_server_hello(tls_conn *c, const uint8_t *buf, size_t len) {
    static const uint8_t hrr[32] = {
        0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,
        0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C };
    if (len < 44 || buf[0] != 2) return fail(c, "not ServerHello");
    const uint8_t *p = buf + 4;
    const uint8_t *end = buf + len;
    p += 2;
    if (!memcmp(p, hrr, 32)) return fail(c, "HelloRetryRequest unsupported");
    p += 32;
    if (p >= end) return fail(c, "malformed ServerHello");
    uint8_t sidlen = *p++;
    p += sidlen;
    if (p + 3 > end) return fail(c, "malformed ServerHello");
    uint16_t suite = ((uint16_t)p[0] << 8) | p[1]; p += 2;
    if (suite != 0x1303) return fail(c, "server did not pick chacha20-poly1305");
    p += 1;
    if (p + 2 > end) return fail(c, "malformed ServerHello");
    uint16_t extlen = ((uint16_t)p[0] << 8) | p[1]; p += 2;
    const uint8_t *ee = p + extlen;
    if (ee > end) ee = end;
    int got = 0;
    while (p + 4 <= ee) {
        uint16_t et = ((uint16_t)p[0] << 8) | p[1];
        uint16_t el = ((uint16_t)p[2] << 8) | p[3];
        p += 4;
        if (p + el > ee) break;
        if (et == 51 && el >= 4) {
            uint16_t g = ((uint16_t)p[0] << 8) | p[1];
            uint16_t kl = ((uint16_t)p[2] << 8) | p[3];
            if (g == 0x001d && kl == 32) { memcpy(c->server_pub, p + 4, 32); got = 1; }
        }
        p += el;
    }
    if (!got) return fail(c, "no x25519 key_share in ServerHello");
    return 0;
}

static int hs_fill(tls_conn *c) {
    uint8_t rt; size_t rl;
    if (record_recv(c, &rt, c->plain, &rl)) return -1;
    if (rt == 21) return fail(c, "alert during handshake");
    if (rt != 22) return fail(c, "expected handshake record");
    if (c->hs_len + rl > HSBUF) return fail(c, "handshake too large");
    memcpy(c->hsbuf + c->hs_len, c->plain, rl);
    c->hs_len += rl;
    return 0;
}

static int hs_next(tls_conn *c, uint8_t *type, const uint8_t **body, size_t *blen) {
    while (c->hs_len - c->hs_pos < 4) if (hs_fill(c)) return -1;
    const uint8_t *p = c->hsbuf + c->hs_pos;
    uint32_t len = ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    while (c->hs_len - c->hs_pos < 4 + (size_t)len) if (hs_fill(c)) return -1;
    p = c->hsbuf + c->hs_pos;
    *type = p[0];
    *body = p + 4;
    *blen = len;
    c->hs_last_msg = p;
    c->hs_last_total = 4 + len;
    return 0;
}

tls_conn *tls_client_new(int fd, const char *hostname) {
    tls_conn *c = (tls_conn *)malloc(sizeof(tls_conn));
    if (!c) return 0;
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->host = hostname;
    return c;
}

int tls_handshake(tls_conn *c) {
    crypto_random(c->cli_priv, 32);
    x25519_base(c->cli_pub, c->cli_priv);
    sha256_init(&c->th);

    uint8_t ch[700];
    size_t chl = build_client_hello(c, ch);
    sha256_update(&c->th, ch, chl);
    if (send_plain(c, 22, 0x0301, ch, chl)) return -1;

    uint8_t rt; size_t rl;
    if (record_recv(c, &rt, c->plain, &rl)) return -1;
    if (rt != 22) return fail(c, "expected ServerHello");
    if (parse_server_hello(c, c->plain, rl)) return -1;
    sha256_update(&c->th, c->plain, rl);

    uint8_t shared[32];
    x25519(shared, c->cli_priv, c->server_pub);

    uint8_t zeros[32]; memset(zeros, 0, 32);
    uint8_t empty_hash[32]; sha256("", 0, empty_hash);
    uint8_t early[32], derived[32];
    hkdf_extract(0, 0, zeros, 32, early);
    derive_secret(early, "derived", empty_hash, derived);
    hkdf_extract(derived, 32, shared, 32, c->hs_secret);

    uint8_t th_cs[32]; { sha256_ctx t = c->th; sha256_final(&t, th_cs); }
    derive_secret(c->hs_secret, "c hs traffic", th_cs, c->chs);
    derive_secret(c->hs_secret, "s hs traffic", th_cs, c->shs);
    set_write_keys(c, c->chs);
    set_read_keys(c, c->shs);

    c->hs_len = c->hs_pos = 0;
    for (;;) {
        uint8_t htype; const uint8_t *body; size_t blen;
        if (hs_next(c, &htype, &body, &blen)) return -1;
        if (htype == 20) {
            uint8_t thash[32]; { sha256_ctx t = c->th; sha256_final(&t, thash); }
            uint8_t fk[32]; expand_label(c->shs, "finished", 0, 0, fk, 32);
            uint8_t exp[32]; hmac_sha256(fk, 32, thash, 32, exp);
            if (blen != 32 || memcmp(exp, body, 32)) return fail(c, "bad server Finished");
            sha256_update(&c->th, c->hs_last_msg, c->hs_last_total);
            c->hs_pos += c->hs_last_total;
            break;
        }
        sha256_update(&c->th, c->hs_last_msg, c->hs_last_total);
        c->hs_pos += c->hs_last_total;
    }

    uint8_t th_sf[32]; { sha256_ctx t = c->th; sha256_final(&t, th_sf); }
    uint8_t derived2[32], master[32];
    derive_secret(c->hs_secret, "derived", empty_hash, derived2);
    hkdf_extract(derived2, 32, zeros, 32, master);
    uint8_t cap[32], sap[32];
    derive_secret(master, "c ap traffic", th_sf, cap);
    derive_secret(master, "s ap traffic", th_sf, sap);

    uint8_t ccs = 0x01;
    if (send_plain(c, 20, 0x0303, &ccs, 1)) return -1;

    uint8_t fkc[32]; expand_label(c->chs, "finished", 0, 0, fkc, 32);
    uint8_t vd[32]; hmac_sha256(fkc, 32, th_sf, 32, vd);
    uint8_t fin[36]; fin[0] = 20; fin[1] = 0; fin[2] = 0; fin[3] = 32; memcpy(fin + 4, vd, 32);
    if (record_send(c, 22, fin, 36)) return -1;

    set_write_keys(c, cap);
    set_read_keys(c, sap);
    c->established = 1;
    return 0;
}

int tls_write(tls_conn *c, const void *buf, size_t len) {
    const uint8_t *p = buf;
    size_t off = 0;
    while (off < len) {
        size_t n = len - off;
        if (n > 16384) n = 16384;
        if (record_send(c, 23, p + off, n)) return -1;
        off += n;
    }
    return (int)len;
}

int tls_read(tls_conn *c, void *buf, size_t len) {
    if (c->plain_off >= c->plain_len) {
        for (;;) {
            uint8_t rt; size_t rl;
            if (record_recv(c, &rt, c->plain, &rl)) return -1;
            if (rt == 23) { c->plain_off = 0; c->plain_len = rl; if (rl == 0) continue; break; }
            if (rt == 22) continue;
            if (rt == 21) return 0;
        }
    }
    size_t avail = c->plain_len - c->plain_off;
    size_t n = avail < len ? avail : len;
    memcpy(buf, c->plain + c->plain_off, n);
    c->plain_off += n;
    return (int)n;
}

void tls_free(tls_conn *c) {
    if (!c) return;
    if (c->established) {
        uint8_t cn[2] = { 1, 0 };
        record_send(c, 21, cn, 2);
    }
    free(c);
}

const char *tls_error(tls_conn *c) { return c->err; }

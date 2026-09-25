#include <inflate.h>
#include <stdlib.h>
#include <string.h>

#define FAST_BITS 10
#define FAST_SIZE (1 << FAST_BITS)
#define PROGRESS_STEP (1u << 20)

typedef struct {
    const uint8_t *in;
    size_t inlen, inpos;
    uint64_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t outlen, outcap;
    int err;
    inflate_progress_fn progress;
    void *progress_ctx;
    size_t progress_base, progress_total, progress_next;
} inf_state;

typedef struct {
    uint16_t fast[FAST_SIZE];
    short count[16];
    short symbol[288];
} huff;

static void fill(inf_state *s) {
    while (s->bitcnt <= 56 && s->inpos < s->inlen) {
        s->bitbuf |= (uint64_t)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
}

static int bits(inf_state *s, int need) {
    if (need == 0) return 0;
    if (s->bitcnt < need) {
        fill(s);
        if (s->bitcnt < need) { s->err = -1; return 0; }
    }
    int v = (int)(s->bitbuf & ((1u << need) - 1));
    s->bitbuf >>= need;
    s->bitcnt -= need;
    return v;
}

static int grow(inf_state *s, size_t need) {
    if (s->outlen + need <= s->outcap) return 0;
    size_t nc = s->outcap ? s->outcap : 65536;
    while (nc < s->outlen + need) nc *= 2;
    uint8_t *n = (uint8_t *)realloc(s->out, nc);
    if (!n) { s->err = -1; return -1; }
    s->out = n;
    s->outcap = nc;
    return 0;
}

static void report(inf_state *s) {
    if (!s->progress || s->inpos < s->progress_next) return;
    s->progress_next = s->inpos + PROGRESS_STEP;
    s->progress(s->progress_ctx, s->progress_base + s->inpos, s->progress_total);
}

static uint32_t reverse(uint32_t code, int len) {
    uint32_t r = 0;
    for (int i = 0; i < len; i++) { r = (r << 1) | (code & 1); code >>= 1; }
    return r;
}

static int build(huff *h, const uint8_t *lengths, int n) {
    for (int i = 0; i < 16; i++) h->count[i] = 0;
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    short offs[16];
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++) if (lengths[i]) h->symbol[offs[lengths[i]]++] = (short)i;

    memset(h->fast, 0, sizeof h->fast);
    int next[16];
    int code = 0;
    for (int len = 1; len < 16; len++) {
        next[len] = code;
        code = (code + h->count[len]) << 1;
    }
    for (int i = 0; i < n; i++) {
        int len = lengths[i];
        if (!len) continue;
        int c = next[len]++;
        if (len > FAST_BITS) continue;
        uint32_t r = reverse((uint32_t)c, len);
        for (uint32_t j = r; j < FAST_SIZE; j += 1u << len)
            h->fast[j] = (uint16_t)((len << 9) | i);
    }
    return 0;
}

static int decode_slow(inf_state *s, huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= bits(s, 1);
        if (s->err) return -1;
        int cnt = h->count[len];
        if (code - first < cnt) return h->symbol[index + (code - first)];
        index += cnt;
        first += cnt;
        first <<= 1;
        code <<= 1;
    }
    s->err = -1;
    return -1;
}

static inline int decode(inf_state *s, huff *h) {
    if (s->bitcnt < FAST_BITS) fill(s);
    if (s->bitcnt >= FAST_BITS) {
        uint16_t e = h->fast[s->bitbuf & (FAST_SIZE - 1)];
        if (e) {
            int len = e >> 9;
            s->bitbuf >>= len;
            s->bitcnt -= len;
            return e & 511;
        }
    }
    return decode_slow(s, h);
}

static const short LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const short LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const short DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const short DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int inflate_block(inf_state *s, huff *lh, huff *dh) {
    for (;;) {
        int sym = decode(s, lh);
        if (s->err) return -1;
        if (sym < 256) {
            if (s->outlen >= s->outcap && grow(s, 1)) return -1;
            s->out[s->outlen++] = (uint8_t)sym;
            continue;
        }
        if (sym == 256) return 0;
        sym -= 257;
        if (sym >= 29) { s->err = -1; return -1; }
        int len = LEN_BASE[sym] + bits(s, LEN_EXTRA[sym]);
        int dsym = decode(s, dh);
        if (s->err || dsym < 0 || dsym >= 30) { s->err = -1; return -1; }
        size_t dist = (size_t)DIST_BASE[dsym] + (size_t)bits(s, DIST_EXTRA[dsym]);
        if (s->err || dist > s->outlen) { s->err = -1; return -1; }
        if (grow(s, (size_t)len)) return -1;
        uint8_t *dst = s->out + s->outlen;
        const uint8_t *src = dst - dist;
        if (dist >= (size_t)len) {
            memcpy(dst, src, (size_t)len);
        } else {
            for (int i = 0; i < len; i++) dst[i] = src[i];
        }
        s->outlen += (size_t)len;
        report(s);
    }
}

static int fixed_block(inf_state *s) {
    static huff lh, dh;
    static int built = 0;
    if (!built) {
        uint8_t l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lh, l, 288);
        uint8_t d[30];
        for (i = 0; i < 30; i++) d[i] = 5;
        build(&dh, d, 30);
        built = 1;
    }
    return inflate_block(s, &lh, &dh);
}

static int dynamic_block(inf_state *s) {
    static const uint8_t ORDER[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    int hlit = bits(s, 5) + 257;
    int hdist = bits(s, 5) + 1;
    int hclen = bits(s, 4) + 4;
    if (s->err || hlit > 286 || hdist > 30) { s->err = -1; return -1; }
    uint8_t cl[19];
    memset(cl, 0, sizeof cl);
    for (int i = 0; i < hclen; i++) cl[ORDER[i]] = (uint8_t)bits(s, 3);
    huff *clh = malloc(sizeof *clh);
    huff *lh = malloc(sizeof *lh);
    huff *dh = malloc(sizeof *dh);
    if (!clh || !lh || !dh) { free(clh); free(lh); free(dh); s->err = -1; return -1; }
    build(clh, cl, 19);

    uint8_t lengths[286 + 30];
    int n = 0, total = hlit + hdist;
    int r = 0;
    while (n < total) {
        int sym = decode(s, clh);
        if (s->err) { r = -1; break; }
        if (sym < 16) lengths[n++] = (uint8_t)sym;
        else if (sym == 16) {
            if (n == 0) { s->err = -1; r = -1; break; }
            int rep = 3 + bits(s, 2);
            while (rep-- && n < total) { lengths[n] = lengths[n - 1]; n++; }
        } else if (sym == 17) {
            int rep = 3 + bits(s, 3);
            while (rep-- && n < total) lengths[n++] = 0;
        } else {
            int rep = 11 + bits(s, 7);
            while (rep-- && n < total) lengths[n++] = 0;
        }
    }
    if (r == 0) {
        build(lh, lengths, hlit);
        build(dh, lengths + hlit, hdist);
        r = inflate_block(s, lh, dh);
    }
    free(clh); free(lh); free(dh);
    return r;
}

static int stored_block(inf_state *s) {
    int drop = s->bitcnt & 7;
    s->bitbuf >>= drop;
    s->bitcnt -= drop;
    s->inpos -= (size_t)(s->bitcnt / 8);
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen) { s->err = -1; return -1; }
    size_t len = (size_t)(s->in[s->inpos] | (s->in[s->inpos + 1] << 8));
    s->inpos += 4;
    if (s->inpos + len > s->inlen) { s->err = -1; return -1; }
    if (grow(s, len)) return -1;
    memcpy(s->out + s->outlen, s->in + s->inpos, len);
    s->outlen += len;
    s->inpos += len;
    report(s);
    return 0;
}

static int inflate_run(inf_state *s, uint8_t **out, size_t *outlen, size_t *used) {
    int final = 0;
    while (!final) {
        final = bits(s, 1);
        int type = bits(s, 2);
        if (s->err) break;
        int r;
        if (type == 0) r = stored_block(s);
        else if (type == 1) r = fixed_block(s);
        else if (type == 2) r = dynamic_block(s);
        else { s->err = -1; break; }
        if (r < 0 || s->err) break;
    }
    if (s->err || grow(s, 1)) { free(s->out); return -1; }
    s->out[s->outlen] = 0;
    *out = s->out;
    *outlen = s->outlen;
    if (used) *used = s->inpos - (size_t)(s->bitcnt / 8);
    return 0;
}

int raw_inflate_used(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen,
                     size_t *used) {
    inf_state s;
    memset(&s, 0, sizeof s);
    s.in = in; s.inlen = inlen;
    return inflate_run(&s, out, outlen, used);
}

int raw_inflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen) {
    return raw_inflate_used(in, inlen, out, outlen, NULL);
}

int zlib_inflate_used(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen,
                      size_t *used) {
    if (inlen < 2) return -1;
    if ((in[0] & 0x0f) != 8) return -1;
    size_t off = 2;
    if (in[1] & 0x20) off += 4;
    if (off >= inlen) return -1;
    size_t raw_used = 0;
    int rc = raw_inflate_used(in + off, inlen - off, out, outlen, &raw_used);
    if (rc == 0 && used) *used = off + raw_used + 4;
    return rc;
}

int zlib_inflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen) {
    return zlib_inflate_used(in, inlen, out, outlen, NULL);
}

int gunzip_progress(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen,
                    inflate_progress_fn progress, void *ctx) {
    if (inlen < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return -1;
    uint8_t flg = in[3];
    size_t off = 10;
    if (flg & 4) {
        if (off + 2 > inlen) return -1;
        int xlen = in[off] | (in[off + 1] << 8);
        off += 2 + (size_t)xlen;
    }
    if (flg & 8)  { while (off < inlen && in[off]) off++; off++; }
    if (flg & 16) { while (off < inlen && in[off]) off++; off++; }
    if (flg & 2)  off += 2;
    if (off >= inlen) return -1;

    inf_state s;
    memset(&s, 0, sizeof s);
    s.in = in + off;
    s.inlen = inlen - off;
    s.progress = progress;
    s.progress_ctx = ctx;
    s.progress_base = off;
    s.progress_total = inlen;
    s.progress_next = PROGRESS_STEP;

    const uint8_t *t = in + inlen - 4;
    size_t isize = (size_t)t[0] | ((size_t)t[1] << 8) | ((size_t)t[2] << 16) | ((size_t)t[3] << 24);
    if (isize && isize < (size_t)1 << 31) {
        s.out = (uint8_t *)malloc(isize + 1);
        if (s.out) s.outcap = isize + 1;
    }
    int rc = inflate_run(&s, out, outlen, NULL);
    if (rc == 0 && progress) progress(ctx, inlen, inlen);
    return rc;
}

int gunzip(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen) {
    return gunzip_progress(in, inlen, out, outlen, NULL, NULL);
}

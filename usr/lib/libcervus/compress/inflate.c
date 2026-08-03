#include <inflate.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *in;
    size_t inlen, inpos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t outlen, outcap;
    int err;
} inf_state;

typedef struct {
    short count[16];
    short symbol[288];
} huff;

static int out_byte(inf_state *s, uint8_t b) {
    if (s->outlen >= s->outcap) {
        size_t nc = s->outcap ? s->outcap * 2 : 4096;
        uint8_t *n = (uint8_t *)realloc(s->out, nc);
        if (!n) { s->err = -1; return -1; }
        s->out = n; s->outcap = nc;
    }
    s->out[s->outlen++] = b;
    return 0;
}

static int bits(inf_state *s, int need) {
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) { s->err = -1; return 0; }
        s->bitbuf |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    int v = (int)(s->bitbuf & ((1u << need) - 1));
    s->bitbuf >>= need;
    s->bitcnt -= need;
    return v;
}

static void build(huff *h, const uint8_t *lengths, int n) {
    for (int i = 0; i < 16; i++) h->count[i] = 0;
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    short offs[16];
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++) if (lengths[i]) h->symbol[offs[lengths[i]]++] = (short)i;
}

static int decode(inf_state *s, huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= bits(s, 1);
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
        if (sym == 256) return 0;
        if (sym < 256) {
            if (out_byte(s, (uint8_t)sym)) return -1;
        } else {
            sym -= 257;
            if (sym >= 29) { s->err = -1; return -1; }
            int len = LEN_BASE[sym] + bits(s, LEN_EXTRA[sym]);
            int dsym = decode(s, dh);
            if (s->err || dsym >= 30) { s->err = -1; return -1; }
            int dist = DIST_BASE[dsym] + bits(s, DIST_EXTRA[dsym]);
            if ((size_t)dist > s->outlen) { s->err = -1; return -1; }
            size_t from = s->outlen - dist;
            for (int i = 0; i < len; i++) {
                if (out_byte(s, s->out[from + i])) return -1;
            }
        }
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
    if (hlit > 286 || hdist > 30) { s->err = -1; return -1; }
    uint8_t cl[19];
    memset(cl, 0, sizeof cl);
    for (int i = 0; i < hclen; i++) cl[ORDER[i]] = (uint8_t)bits(s, 3);
    huff clh;
    build(&clh, cl, 19);

    uint8_t lengths[286 + 30];
    int n = 0, total = hlit + hdist;
    while (n < total) {
        int sym = decode(s, &clh);
        if (s->err) return -1;
        if (sym < 16) lengths[n++] = (uint8_t)sym;
        else if (sym == 16) {
            if (n == 0) { s->err = -1; return -1; }
            int r = 3 + bits(s, 2);
            while (r-- && n < total) { lengths[n] = lengths[n - 1]; n++; }
        } else if (sym == 17) {
            int r = 3 + bits(s, 3);
            while (r-- && n < total) lengths[n++] = 0;
        } else {
            int r = 11 + bits(s, 7);
            while (r-- && n < total) lengths[n++] = 0;
        }
    }
    huff lh, dh;
    build(&lh, lengths, hlit);
    build(&dh, lengths + hlit, hdist);
    return inflate_block(s, &lh, &dh);
}

static int stored_block(inf_state *s) {
    s->bitbuf = 0; s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen) { s->err = -1; return -1; }
    int len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
    s->inpos += 4;
    if (s->inpos + len > s->inlen) { s->err = -1; return -1; }
    for (int i = 0; i < len; i++) if (out_byte(s, s->in[s->inpos++])) return -1;
    return 0;
}

int raw_inflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen) {
    inf_state s;
    memset(&s, 0, sizeof s);
    s.in = in; s.inlen = inlen;
    int final = 0;
    while (!final) {
        final = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.err) break;
        int r;
        if (type == 0) r = stored_block(&s);
        else if (type == 1) r = fixed_block(&s);
        else if (type == 2) r = dynamic_block(&s);
        else { s.err = -1; break; }
        if (r < 0 || s.err) break;
    }
    if (s.err) { free(s.out); return -1; }
    if (out_byte(&s, 0) < 0) { free(s.out); return -1; }
    s.outlen--;
    *out = s.out; *outlen = s.outlen;
    return 0;
}

int zlib_inflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen) {
    if (inlen < 2) return -1;
    if ((in[0] & 0x0f) != 8) return -1;
    size_t off = 2;
    if (in[1] & 0x20) off += 4;
    if (off >= inlen) return -1;
    return raw_inflate(in + off, inlen - off, out, outlen);
}

int gunzip(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen) {
    if (inlen < 10 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return -1;
    uint8_t flg = in[3];
    size_t off = 10;
    if (flg & 4) {
        if (off + 2 > inlen) return -1;
        int xlen = in[off] | (in[off + 1] << 8);
        off += 2 + xlen;
    }
    if (flg & 8)  { while (off < inlen && in[off]) off++; off++; }
    if (flg & 16) { while (off < inlen && in[off]) off++; off++; }
    if (flg & 2)  off += 2;
    if (off >= inlen) return -1;
    return raw_inflate(in + off, inlen - off, out, outlen);
}

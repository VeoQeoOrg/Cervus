#include <inflate.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_BITS 15
#define WINDOW      (1u << WINDOW_BITS)
#define HASH_BITS   15
#define HASH_SIZE   (1u << HASH_BITS)
#define MIN_MATCH   3
#define MAX_MATCH   258
#define MAX_CHAIN   64

typedef struct {
    uint8_t *out;
    size_t   len;
    size_t   cap;
    uint32_t bitbuf;
    int      bitcnt;
    int      failed;
} bitout_t;

static int bo_reserve(bitout_t *b, size_t extra)
{
    if (b->len + extra <= b->cap) return 0;
    size_t cap = b->cap ? b->cap * 2 : 1024;
    while (cap < b->len + extra) cap *= 2;
    uint8_t *n = realloc(b->out, cap);
    if (!n) { b->failed = 1; return -1; }
    b->out = n;
    b->cap = cap;
    return 0;
}

static void bo_bits(bitout_t *b, uint32_t value, int nbits)
{
    b->bitbuf |= (value & ((1u << nbits) - 1)) << b->bitcnt;
    b->bitcnt += nbits;
    while (b->bitcnt >= 8) {
        if (bo_reserve(b, 1) < 0) return;
        b->out[b->len++] = (uint8_t)(b->bitbuf & 0xFF);
        b->bitbuf >>= 8;
        b->bitcnt -= 8;
    }
}

static void bo_flush(bitout_t *b)
{
    if (b->bitcnt > 0) {
        if (bo_reserve(b, 1) < 0) return;
        b->out[b->len++] = (uint8_t)(b->bitbuf & 0xFF);
        b->bitbuf = 0;
        b->bitcnt = 0;
    }
}

static void bo_huff(bitout_t *b, uint32_t code, int nbits)
{
    for (int i = nbits - 1; i >= 0; i--)
        bo_bits(b, (code >> i) & 1, 1);
}

static void emit_literal(bitout_t *b, uint8_t lit)
{
    if (lit < 144) bo_huff(b, 0x30 + lit, 8);
    else           bo_huff(b, 0x190 + lit - 144, 9);
}

static const uint16_t LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static void emit_match(bitout_t *b, int len, int dist)
{
    int li = 28;
    while (li > 0 && LEN_BASE[li] > len) li--;
    int sym = 257 + li;

    if (sym < 280) bo_huff(b, sym - 256, 7);
    else           bo_huff(b, 0xC0 + sym - 280, 8);
    if (LEN_EXTRA[li]) bo_bits(b, (uint32_t)(len - LEN_BASE[li]), LEN_EXTRA[li]);

    int di = 29;
    while (di > 0 && DIST_BASE[di] > dist) di--;
    bo_huff(b, (uint32_t)di, 5);
    if (DIST_EXTRA[di]) bo_bits(b, (uint32_t)(dist - DIST_BASE[di]), DIST_EXTRA[di]);
}

static uint32_t hash3(const uint8_t *p)
{
    return (((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ (uint32_t)p[2])
           & (HASH_SIZE - 1);
}

static int raw_stored(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen)
{
    size_t blocks = inlen / 65535 + 1;
    size_t total  = inlen + blocks * 5;
    uint8_t *buf = malloc(total ? total : 1);
    if (!buf) return -1;

    size_t at = 0, done = 0;
    do {
        size_t n = inlen - done;
        if (n > 65535) n = 65535;
        int last = (done + n >= inlen);
        buf[at++] = (uint8_t)(last ? 1 : 0);
        buf[at++] = (uint8_t)(n & 0xFF);
        buf[at++] = (uint8_t)(n >> 8);
        buf[at++] = (uint8_t)(~n & 0xFF);
        buf[at++] = (uint8_t)((~n >> 8) & 0xFF);
        if (n) memcpy(buf + at, in + done, n);
        at += n;
        done += n;
    } while (done < inlen);

    *out = buf;
    *outlen = at;
    return 0;
}

int raw_deflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen)
{
    if (!in || !out || !outlen) return -1;

    int32_t *head = malloc(HASH_SIZE * sizeof(int32_t));
    int32_t *prev = malloc((inlen ? inlen : 1) * sizeof(int32_t));
    if (!head || !prev) { free(head); free(prev); return -1; }
    for (uint32_t i = 0; i < HASH_SIZE; i++) head[i] = -1;

    bitout_t b;
    memset(&b, 0, sizeof b);

    bo_bits(&b, 1, 1);
    bo_bits(&b, 1, 2);

    size_t i = 0;
    while (i < inlen) {
        int best_len = 0, best_dist = 0;

        if (i + MIN_MATCH <= inlen) {
            uint32_t h = hash3(in + i);
            int32_t cand = head[h];
            int chain = 0;
            while (cand >= 0 && chain++ < MAX_CHAIN) {
                size_t dist = i - (size_t)cand;
                if (dist == 0 || dist > WINDOW) break;
                size_t maxlen = inlen - i;
                if (maxlen > MAX_MATCH) maxlen = MAX_MATCH;
                size_t l = 0;
                while (l < maxlen && in[cand + l] == in[i + l]) l++;
                if ((int)l > best_len) {
                    best_len = (int)l;
                    best_dist = (int)dist;
                    if (best_len >= MAX_MATCH) break;
                }
                cand = prev[cand];
            }
            prev[i] = head[h];
            head[h] = (int32_t)i;
        }

        if (best_len >= MIN_MATCH) {
            emit_match(&b, best_len, best_dist);
            for (int k = 1; k < best_len; k++) {
                size_t j = i + k;
                if (j + MIN_MATCH <= inlen) {
                    uint32_t h = hash3(in + j);
                    prev[j] = head[h];
                    head[h] = (int32_t)j;
                }
            }
            i += best_len;
        } else {
            emit_literal(&b, in[i]);
            i++;
        }
        if (b.failed) break;
    }

    bo_huff(&b, 0, 7);
    bo_flush(&b);

    free(head);
    free(prev);

    if (b.failed) { free(b.out); return -1; }

    if (inlen && b.len >= inlen + inlen / 1000 + 5) {
        uint8_t *stored = NULL;
        size_t stored_len = 0;
        if (raw_stored(in, inlen, &stored, &stored_len) == 0 && stored_len < b.len) {
            free(b.out);
            *out = stored;
            *outlen = stored_len;
            return 0;
        }
        free(stored);
    }

    *out = b.out;
    *outlen = b.len;
    return 0;
}

static uint32_t adler32(const uint8_t *d, size_t n)
{
    uint32_t a = 1, s = 0;
    for (size_t i = 0; i < n; i++) {
        a = (a + d[i]) % 65521u;
        s = (s + a) % 65521u;
    }
    return (s << 16) | a;
}

int zlib_deflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen)
{
    uint8_t *raw = NULL;
    size_t rawlen = 0;
    if (raw_deflate(in, inlen, &raw, &rawlen) != 0) return -1;

    size_t total = 2 + rawlen + 4;
    uint8_t *buf = malloc(total);
    if (!buf) { free(raw); return -1; }

    buf[0] = 0x78;
    buf[1] = 0x9C;
    memcpy(buf + 2, raw, rawlen);
    free(raw);

    uint32_t sum = adler32(in, inlen);
    buf[2 + rawlen]     = (uint8_t)(sum >> 24);
    buf[2 + rawlen + 1] = (uint8_t)(sum >> 16);
    buf[2 + rawlen + 2] = (uint8_t)(sum >> 8);
    buf[2 + rawlen + 3] = (uint8_t)(sum);

    *out = buf;
    *outlen = total;
    return 0;
}

#include <image.h>
#include <stdlib.h>
#include <string.h>

#define GIF_MAX_CODES   4096
#define GIF_MAX_FRAMES  512

typedef struct {
    const uint8_t *d;
    size_t         n;
    size_t         p;
} gif_rd;

static int rd_u8(gif_rd *r, uint8_t *v) {
    if (r->p + 1 > r->n) return -1;
    *v = r->d[r->p++];
    return 0;
}

static int rd_u16(gif_rd *r, uint16_t *v) {
    if (r->p + 2 > r->n) return -1;
    *v = (uint16_t)(r->d[r->p] | (r->d[r->p + 1] << 8));
    r->p += 2;
    return 0;
}

typedef struct {
    gif_rd  *r;
    uint8_t  buf[255];
    int      have;
    int      pos;
    uint32_t acc;
    int      bits;
    int      ended;
} gif_bits;

static int gif_next_block(gif_bits *b) {
    uint8_t sz;
    if (rd_u8(b->r, &sz) != 0) return -1;
    if (sz == 0) { b->ended = 1; return 0; }
    if (b->r->p + sz > b->r->n) return -1;
    memcpy(b->buf, b->r->d + b->r->p, sz);
    b->r->p += sz;
    b->have = sz;
    b->pos  = 0;
    return 0;
}

static int gif_get_code(gif_bits *b, int width, int *out) {
    while (b->bits < width) {
        if (b->pos >= b->have) {
            if (b->ended) return -1;
            if (gif_next_block(b) != 0) return -1;
            if (b->ended) return -1;
        }
        b->acc |= (uint32_t)b->buf[b->pos++] << b->bits;
        b->bits += 8;
    }
    *out = (int)(b->acc & ((1u << width) - 1));
    b->acc >>= width;
    b->bits -= width;
    return 0;
}

static int gif_skip_blocks(gif_rd *r) {
    for (;;) {
        uint8_t sz;
        if (rd_u8(r, &sz) != 0) return -1;
        if (sz == 0) return 0;
        if (r->p + sz > r->n) return -1;
        r->p += sz;
    }
}

static int gif_lzw(gif_rd *r, uint8_t *out, int npix) {
    uint8_t min_size;
    if (rd_u8(r, &min_size) != 0) return -1;
    if (min_size < 1 || min_size > 11) return -1;

    static uint16_t prefix[GIF_MAX_CODES];
    static uint8_t  suffix[GIF_MAX_CODES];
    static uint8_t  first[GIF_MAX_CODES];
    static uint8_t  stack[GIF_MAX_CODES];

    int clear_code = 1 << min_size;
    int end_code   = clear_code + 1;
    int next_code  = end_code + 1;
    int code_size  = min_size + 1;
    int prev       = -1;

    for (int i = 0; i < clear_code; i++) {
        prefix[i] = 0xFFFF;
        suffix[i] = (uint8_t)i;
        first[i]  = (uint8_t)i;
    }

    gif_bits b;
    memset(&b, 0, sizeof(b));
    b.r = r;

    int written = 0;
    for (;;) {
        int code;
        if (gif_get_code(&b, code_size, &code) != 0) break;

        if (code == clear_code) {
            next_code = end_code + 1;
            code_size = min_size + 1;
            prev = -1;
            continue;
        }
        if (code == end_code) break;
        if (code > next_code) break;

        int sp = 0;
        if (code == next_code) {
            if (prev < 0) break;
            stack[sp++] = first[prev];
            int t = prev;
            while (prefix[t] != 0xFFFF) { stack[sp++] = suffix[t]; t = prefix[t]; }
            stack[sp++] = suffix[t];
        } else {
            int t = code;
            while (prefix[t] != 0xFFFF) { stack[sp++] = suffix[t]; t = prefix[t]; }
            stack[sp++] = suffix[t];
        }

        while (sp > 0 && written < npix) out[written++] = stack[--sp];

        if (prev >= 0 && next_code < GIF_MAX_CODES) {
            prefix[next_code] = (uint16_t)prev;
            suffix[next_code] = (code == next_code) ? first[prev] : first[code];
            first[next_code]  = first[prev];
            next_code++;
            if (next_code == (1 << code_size) && code_size < 12) code_size++;
        }
        prev = code;
        if (written >= npix) break;
    }

    gif_skip_blocks(r);
    return written > 0 ? 0 : -1;
}

static const int gif_il_start[4] = { 0, 4, 2, 1 };
static const int gif_il_step[4]  = { 8, 8, 4, 2 };

int gif_decode(const uint8_t *d, size_t n, gif_anim_t *out) {
    if (!d || !out || n < 13) return -1;
    if (memcmp(d, "GIF87a", 6) != 0 && memcmp(d, "GIF89a", 6) != 0) return -1;

    memset(out, 0, sizeof(*out));

    gif_rd r = { d, n, 6 };
    uint16_t sw, sh;
    uint8_t packed, bg, aspect;
    if (rd_u16(&r, &sw) || rd_u16(&r, &sh)) return -1;
    if (rd_u8(&r, &packed) || rd_u8(&r, &bg) || rd_u8(&r, &aspect)) return -1;
    if (sw == 0 || sh == 0 || sw > 8192 || sh > 8192) return -1;

    uint32_t gct[256];
    int gct_n = 0;
    if (packed & 0x80) {
        gct_n = 1 << ((packed & 7) + 1);
        if (r.p + (size_t)gct_n * 3 > n) return -1;
        for (int i = 0; i < gct_n; i++) {
            gct[i] = 0xFF000000u | ((uint32_t)d[r.p] << 16) |
                     ((uint32_t)d[r.p + 1] << 8) | d[r.p + 2];
            r.p += 3;
        }
    }

    size_t npx = (size_t)sw * sh;
    uint32_t *canvas = calloc(npx, 4);
    uint32_t *prev   = calloc(npx, 4);
    uint8_t  *idx    = malloc(npx);
    if (!canvas || !prev || !idx) { free(canvas); free(prev); free(idx); return -1; }

    uint32_t **frames = calloc(GIF_MAX_FRAMES, sizeof(uint32_t *));
    int *delays = calloc(GIF_MAX_FRAMES, sizeof(int));
    if (!frames || !delays) {
        free(canvas); free(prev); free(idx); free(frames); free(delays);
        return -1;
    }

    int nframes = 0;
    int transparent = -1;
    int disposal = 0;
    int delay_ms = 100;
    int loops = 0;

    for (;;) {
        uint8_t blk;
        if (rd_u8(&r, &blk) != 0) break;

        if (blk == 0x3B) break;

        if (blk == 0x21) {
            uint8_t label;
            if (rd_u8(&r, &label) != 0) break;
            if (label == 0xF9) {
                uint8_t bs;
                if (rd_u8(&r, &bs) != 0 || bs != 4) { gif_skip_blocks(&r); continue; }
                uint8_t gp, ti;
                uint16_t dl;
                if (rd_u8(&r, &gp) || rd_u16(&r, &dl) || rd_u8(&r, &ti)) break;
                uint8_t term;
                rd_u8(&r, &term);
                disposal    = (gp >> 2) & 7;
                transparent = (gp & 1) ? ti : -1;
                delay_ms    = dl ? dl * 10 : 100;
            } else if (label == 0xFF) {
                uint8_t bs;
                if (rd_u8(&r, &bs) != 0) break;
                if (bs == 11 && r.p + 11 <= n && memcmp(d + r.p, "NETSCAPE2.0", 11) == 0) {
                    r.p += 11;
                    uint8_t sub;
                    if (rd_u8(&r, &sub) == 0 && sub >= 3 && r.p + sub <= n) {
                        loops = d[r.p + 1] | (d[r.p + 2] << 8);
                        r.p += sub;
                    }
                    gif_skip_blocks(&r);
                } else {
                    if (r.p + bs > n) break;
                    r.p += bs;
                    gif_skip_blocks(&r);
                }
            } else {
                gif_skip_blocks(&r);
            }
            continue;
        }

        if (blk != 0x2C) continue;

        uint16_t ix, iy, iw, ih;
        uint8_t ip;
        if (rd_u16(&r, &ix) || rd_u16(&r, &iy) || rd_u16(&r, &iw) || rd_u16(&r, &ih)) break;
        if (rd_u8(&r, &ip) != 0) break;
        if (iw == 0 || ih == 0) break;
        if ((size_t)ix + iw > sw || (size_t)iy + ih > sh) break;

        uint32_t lct[256];
        const uint32_t *pal = gct;
        int pal_n = gct_n;
        if (ip & 0x80) {
            pal_n = 1 << ((ip & 7) + 1);
            if (r.p + (size_t)pal_n * 3 > n) break;
            for (int i = 0; i < pal_n; i++) {
                lct[i] = 0xFF000000u | ((uint32_t)d[r.p] << 16) |
                         ((uint32_t)d[r.p + 1] << 8) | d[r.p + 2];
                r.p += 3;
            }
            pal = lct;
        }
        if (pal_n == 0) break;

        size_t sub_px = (size_t)iw * ih;
        if (gif_lzw(&r, idx, (int)sub_px) != 0) break;

        if (disposal == 3) memcpy(prev, canvas, npx * 4);

        int interlaced = (ip & 0x40) ? 1 : 0;
        int pass = 0, row = 0;
        for (int sy = 0; sy < ih; sy++) {
            int dy;
            if (interlaced) {
                if (row >= ih) { pass++; if (pass > 3) break; row = gif_il_start[pass]; }
                dy = row;
                row += gif_il_step[pass];
            } else {
                dy = sy;
            }
            if (dy >= ih) continue;
            const uint8_t *srow = idx + (size_t)sy * iw;
            uint32_t *drow = canvas + (size_t)(iy + dy) * sw + ix;
            for (int x = 0; x < iw; x++) {
                int ci = srow[x];
                if (ci == transparent) continue;
                drow[x] = (ci < pal_n) ? pal[ci] : 0xFF000000u;
            }
        }

        if (nframes < GIF_MAX_FRAMES) {
            uint32_t *fr = malloc(npx * 4);
            if (!fr) break;
            memcpy(fr, canvas, npx * 4);
            frames[nframes] = fr;
            delays[nframes] = delay_ms;
            nframes++;
        }

        if (disposal == 2) {
            uint32_t fill = (transparent >= 0) ? 0u : ((bg < pal_n) ? pal[bg] : 0u);
            for (int y = 0; y < ih; y++) {
                uint32_t *drow = canvas + (size_t)(iy + y) * sw + ix;
                for (int x = 0; x < iw; x++) drow[x] = fill;
            }
        } else if (disposal == 3) {
            memcpy(canvas, prev, npx * 4);
        }

        transparent = -1;
        disposal = 0;
    }

    free(canvas);
    free(prev);
    free(idx);

    if (nframes == 0) { free(frames); free(delays); return -1; }

    out->w = sw;
    out->h = sh;
    out->nframes = nframes;
    out->frames = frames;
    out->delays_ms = delays;
    out->loops = loops;
    return 0;
}

void gif_free(gif_anim_t *a) {
    if (!a) return;
    if (a->frames) {
        for (int i = 0; i < a->nframes; i++) free(a->frames[i]);
        free(a->frames);
    }
    free(a->delays_ms);
    memset(a, 0, sizeof(*a));
}

int image_decode_gif(const uint8_t *d, size_t n, image_t *out) {
    gif_anim_t a;
    if (gif_decode(d, n, &a) != 0) return -1;
    out->w = a.w;
    out->h = a.h;
    out->px = a.frames[0];
    a.frames[0] = NULL;
    gif_free(&a);
    return 0;
}

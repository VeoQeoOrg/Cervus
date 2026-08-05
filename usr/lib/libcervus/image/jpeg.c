#include <image.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    uint8_t  bits[17];
    uint8_t  vals[256];
    int      mincode[17];
    int      maxcode[18];
    int      valptr[17];
    int      present;
} huff_t;

typedef struct {
    int id, h, v, tq, td, ta, dcpred;
    int cw, ch;
    uint8_t *plane;
} comp_t;

typedef struct {
    const uint8_t *d;
    size_t n, p;
    uint32_t bitbuf;
    int bitcnt;
    int marker;
} bitrd_t;

static void build_huff(huff_t *ht) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        ht->valptr[l] = k;
        ht->mincode[l] = code;
        code += ht->bits[l];
        ht->maxcode[l] = ht->bits[l] ? (code - 1) : -1;
        k += ht->bits[l];
        code <<= 1;
    }
    ht->maxcode[17] = 0x7fffffff;
}

static int fill_bit(bitrd_t *b) {
    if (b->marker) return 0;
    if (b->p >= b->n) { b->marker = 1; return 0; }
    uint8_t c = b->d[b->p++];
    if (c == 0xFF) {
        while (b->p < b->n && b->d[b->p] == 0xFF) b->p++;
        if (b->p < b->n) {
            uint8_t m = b->d[b->p];
            if (m == 0x00) { b->p++; }
            else { b->marker = 1; b->p--; return 0; }
        } else { b->marker = 1; return 0; }
    }
    b->bitbuf = (b->bitbuf << 8) | c;
    b->bitcnt += 8;
    return 1;
}

static int get_bit(bitrd_t *b) {
    if (b->bitcnt == 0 && !fill_bit(b)) return 0;
    b->bitcnt--;
    return (b->bitbuf >> b->bitcnt) & 1;
}

static int get_bits(bitrd_t *b, int nb) {
    int v = 0;
    for (int i = 0; i < nb; i++) v = (v << 1) | get_bit(b);
    return v;
}

static int recv_extend(bitrd_t *b, int s) {
    if (s == 0) return 0;
    int v = get_bits(b, s);
    if (v < (1 << (s - 1))) v -= (1 << s) - 1;
    return v;
}

static int huff_decode(bitrd_t *b, huff_t *ht) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | get_bit(b);
        if (ht->maxcode[l] >= 0 && code <= ht->maxcode[l])
            return ht->vals[ht->valptr[l] + code - ht->mincode[l]];
    }
    return 0;
}

static const int ZZ[64] = {
    0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};

static float g_cos[8][8];
static int   g_cos_init = 0;

static void idct_init(void) {
    if (g_cos_init) return;
    for (int u = 0; u < 8; u++)
        for (int x = 0; x < 8; x++)
            g_cos[u][x] = cosf((2.0f * x + 1.0f) * u * 3.14159265358979f / 16.0f) *
                          (u == 0 ? 0.353553390593f : 0.5f);
    g_cos_init = 1;
}

static void idct8x8(const int *blk, uint8_t *out, int stride) {
    float tmp[64];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += g_cos[u][x] * blk[y * 8 + u];
            tmp[y * 8 + x] = s;
        }
    }
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += g_cos[v][y] * tmp[v * 8 + x];
            int val = (int)lrintf(s) + 128;
            if (val < 0) val = 0; else if (val > 255) val = 255;
            out[y * stride + x] = (uint8_t)val;
        }
    }
}

static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }

int image_decode_jpeg(const uint8_t *d, size_t n, image_t *out) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return -1;
    idct_init();

    int qt[4][64]; memset(qt, 0, sizeof qt);
    huff_t hdc[4], hac[4];
    memset(hdc, 0, sizeof hdc); memset(hac, 0, sizeof hac);
    comp_t comp[4]; memset(comp, 0, sizeof comp);
    int ncomp = 0, W = 0, H = 0, restart = 0;

    size_t p = 2;
    int rc = -1;
    while (p + 4 <= n) {
        if (d[p] != 0xFF) { p++; continue; }
        uint8_t m = d[p + 1];
        p += 2;
        if (m == 0xD9) break;
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
        if (p + 2 > n) break;
        uint32_t seg = be16(d + p);
        const uint8_t *body = d + p + 2;
        size_t blen = seg >= 2 ? seg - 2 : 0;
        if (p + seg > n) break;

        if (m == 0xDB) {
            size_t q = 0;
            while (q < blen) {
                int pq = body[q] >> 4, tq = body[q] & 15; q++;
                if (tq > 3) break;
                for (int i = 0; i < 64; i++) {
                    if (pq) { qt[tq][i] = (int)be16(body + q); q += 2; }
                    else    { qt[tq][i] = body[q]; q += 1; }
                }
            }
        } else if (m == 0xC4) {
            size_t q = 0;
            while (q < blen) {
                int tc = body[q] >> 4, th = body[q] & 15; q++;
                if (th > 3) break;
                huff_t *ht = tc ? &hac[th] : &hdc[th];
                int total = 0;
                ht->bits[0] = 0;
                for (int i = 1; i <= 16; i++) { ht->bits[i] = body[q++]; total += ht->bits[i]; }
                if (total > 256) break;
                for (int i = 0; i < total; i++) ht->vals[i] = body[q++];
                build_huff(ht);
                ht->present = 1;
            }
        } else if (m == 0xC0 || m == 0xC1) {
            H = (int)be16(body + 1);
            W = (int)be16(body + 3);
            ncomp = body[5];
            if (ncomp < 1 || ncomp > 4) break;
            for (int c = 0; c < ncomp; c++) {
                comp[c].id = body[6 + c * 3];
                comp[c].h  = body[7 + c * 3] >> 4;
                comp[c].v  = body[7 + c * 3] & 15;
                comp[c].tq = body[8 + c * 3];
            }
        } else if (m == 0xC2) {
            return -1;
        } else if (m == 0xDD) {
            restart = (int)be16(body);
        } else if (m == 0xDA) {
            int ns = body[0];
            for (int i = 0; i < ns; i++) {
                int cid = body[1 + i * 2];
                int t = body[2 + i * 2];
                for (int c = 0; c < ncomp; c++) if (comp[c].id == cid) {
                    comp[c].td = t >> 4; comp[c].ta = t & 15;
                }
            }
            p += seg;
            goto decode;
        }
        p += seg;
    }
    return -1;

decode:;
    if (W <= 0 || H <= 0 || W > 20000 || H > 20000) return -1;
    int hmax = 1, vmax = 1;
    for (int c = 0; c < ncomp; c++) { if (comp[c].h > hmax) hmax = comp[c].h; if (comp[c].v > vmax) vmax = comp[c].v; }
    int mcux = (W + hmax * 8 - 1) / (hmax * 8);
    int mcuy = (H + vmax * 8 - 1) / (vmax * 8);

    for (int c = 0; c < ncomp; c++) {
        comp[c].cw = mcux * comp[c].h * 8;
        comp[c].ch = mcuy * comp[c].v * 8;
        comp[c].plane = calloc((size_t)comp[c].cw * comp[c].ch, 1);
        if (!comp[c].plane) { rc = -1; goto cleanup; }
        comp[c].dcpred = 0;
    }

    bitrd_t br; memset(&br, 0, sizeof br);
    br.d = d; br.n = n; br.p = p;
    int rst_cnt = restart;

    for (int my = 0; my < mcuy; my++) {
        for (int mx = 0; mx < mcux; mx++) {
            for (int c = 0; c < ncomp; c++) {
                huff_t *dch = &hdc[comp[c].td];
                huff_t *ach = &hac[comp[c].ta];
                int *q = qt[comp[c].tq];
                for (int by = 0; by < comp[c].v; by++) {
                    for (int bx = 0; bx < comp[c].h; bx++) {
                        int blk[64]; memset(blk, 0, sizeof blk);
                        int t = huff_decode(&br, dch);
                        int diff = recv_extend(&br, t);
                        comp[c].dcpred += diff;
                        blk[0] = comp[c].dcpred * q[0];
                        int k = 1;
                        while (k < 64) {
                            int rs = huff_decode(&br, ach);
                            int r = rs >> 4, s = rs & 15;
                            if (s == 0) { if (r != 15) break; k += 16; continue; }
                            k += r;
                            if (k >= 64) break;
                            int val = recv_extend(&br, s);
                            blk[ZZ[k]] = val * q[k];
                            k++;
                        }
                        int px = (mx * comp[c].h + bx) * 8;
                        int py = (my * comp[c].v + by) * 8;
                        idct8x8(blk, comp[c].plane + (size_t)py * comp[c].cw + px, comp[c].cw);
                    }
                }
            }
            if (restart && --rst_cnt == 0 && !(my == mcuy - 1 && mx == mcux - 1)) {
                br.bitcnt = 0; br.bitbuf = 0;
                while (br.p + 1 < br.n && !(br.d[br.p] == 0xFF && br.d[br.p + 1] >= 0xD0 && br.d[br.p + 1] <= 0xD7)) br.p++;
                if (br.p + 1 < br.n) br.p += 2;
                br.marker = 0;
                for (int c = 0; c < ncomp; c++) comp[c].dcpred = 0;
                rst_cnt = restart;
            }
        }
    }

    uint32_t *rgb = malloc((size_t)W * H * 4);
    if (!rgb) { rc = -1; goto cleanup; }

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int Y, Cb = 128, Cr = 128;
            {
                comp_t *cc = &comp[0];
                int cx = x * cc->h / hmax, cy = y * cc->v / vmax;
                Y = cc->plane[(size_t)cy * cc->cw + cx];
            }
            if (ncomp == 3) {
                comp_t *cb = &comp[1], *cr = &comp[2];
                int bx = x * cb->h / hmax, byy = y * cb->v / vmax;
                int rx = x * cr->h / hmax, ry = y * cr->v / vmax;
                Cb = cb->plane[(size_t)byy * cb->cw + bx];
                Cr = cr->plane[(size_t)ry * cr->cw + rx];
            }
            int r, g, b;
            if (ncomp == 1) { r = g = b = Y; }
            else {
                float fcr = Cr - 128, fcb = Cb - 128;
                r = (int)(Y + 1.402f * fcr);
                g = (int)(Y - 0.344136f * fcb - 0.714136f * fcr);
                b = (int)(Y + 1.772f * fcb);
            }
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            rgb[(size_t)y * W + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }

    out->w = W; out->h = H; out->px = rgb;
    rc = 0;

cleanup:
    for (int c = 0; c < ncomp; c++) free(comp[c].plane);
    return rc;
}

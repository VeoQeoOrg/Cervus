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
    int bw, bh;
    int nbw, nbh;
    int cw, ch;
    int16_t *coef;
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

typedef struct {
    bitrd_t br;
    int     eobrun;
    int     restart;
    int     rst_left;
    int     progressive;
    int     ss, se, ah, al;
} scan_t;

static void scan_restart(scan_t *sc, comp_t *comp, int ncomp) {
    sc->br.bitcnt = 0;
    sc->br.bitbuf = 0;
    size_t p = sc->br.p;
    const uint8_t *d = sc->br.d;
    while (p + 1 < sc->br.n &&
           !(d[p] == 0xFF && d[p + 1] >= 0xD0 && d[p + 1] <= 0xD7)) p++;
    if (p + 1 < sc->br.n) p += 2;
    sc->br.p = p;
    sc->br.marker = 0;
    sc->eobrun = 0;
    for (int c = 0; c < ncomp; c++) comp[c].dcpred = 0;
    sc->rst_left = sc->restart;
}

static void block_baseline(scan_t *sc, comp_t *c, huff_t *dch, huff_t *ach, int16_t *blk) {
    int t = huff_decode(&sc->br, dch);
    c->dcpred += recv_extend(&sc->br, t);
    blk[0] = (int16_t)c->dcpred;
    int k = 1;
    while (k < 64) {
        int rs = huff_decode(&sc->br, ach);
        int r = rs >> 4, s = rs & 15;
        if (s == 0) { if (r != 15) break; k += 16; continue; }
        k += r;
        if (k >= 64) break;
        blk[ZZ[k]] = (int16_t)recv_extend(&sc->br, s);
        k++;
    }
}

static void block_dc_first(scan_t *sc, comp_t *c, huff_t *dch, int16_t *blk) {
    int t = huff_decode(&sc->br, dch);
    c->dcpred += recv_extend(&sc->br, t);
    blk[0] = (int16_t)(c->dcpred << sc->al);
}

static void block_dc_refine(scan_t *sc, int16_t *blk) {
    if (get_bit(&sc->br)) blk[0] = (int16_t)(blk[0] + (1 << sc->al));
}

static void block_ac_first(scan_t *sc, huff_t *ach, int16_t *blk) {
    if (sc->eobrun) { sc->eobrun--; return; }
    int k = sc->ss;
    while (k <= sc->se) {
        int rs = huff_decode(&sc->br, ach);
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r < 15) {
                sc->eobrun = (1 << r) - 1;
                if (r) sc->eobrun += get_bits(&sc->br, r);
                break;
            }
            k += 16;
            continue;
        }
        k += r;
        if (k > sc->se) break;
        blk[ZZ[k]] = (int16_t)(recv_extend(&sc->br, s) << sc->al);
        k++;
    }
}

static void block_ac_refine(scan_t *sc, huff_t *ach, int16_t *blk) {
    int bit = 1 << sc->al;

    if (sc->eobrun) {
        sc->eobrun--;
        for (int k = sc->ss; k <= sc->se; k++) {
            int16_t *p = &blk[ZZ[k]];
            if (*p && get_bit(&sc->br) && (*p & bit) == 0)
                *p = (int16_t)(*p > 0 ? *p + bit : *p - bit);
        }
        return;
    }

    int k = sc->ss;
    do {
        int rs = huff_decode(&sc->br, ach);
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r < 15) {
                sc->eobrun = (1 << r) - 1;
                if (r) sc->eobrun += get_bits(&sc->br, r);
                r = 64;
            }
        } else {
            s = get_bit(&sc->br) ? bit : -bit;
        }
        while (k <= sc->se) {
            int16_t *p = &blk[ZZ[k++]];
            if (*p) {
                if (get_bit(&sc->br) && (*p & bit) == 0)
                    *p = (int16_t)(*p > 0 ? *p + bit : *p - bit);
            } else {
                if (r == 0) { if (s) *p = (int16_t)s; break; }
                r--;
            }
        }
    } while (k <= sc->se);
}

static void decode_one_block(scan_t *sc, comp_t *c, huff_t *hdc, huff_t *hac, int16_t *blk) {
    if (!sc->progressive) {
        block_baseline(sc, c, &hdc[c->td], &hac[c->ta], blk);
        return;
    }
    if (sc->ss == 0) {
        if (sc->ah == 0) block_dc_first(sc, c, &hdc[c->td], blk);
        else             block_dc_refine(sc, blk);
    } else {
        if (sc->ah == 0) block_ac_first(sc, &hac[c->ta], blk);
        else             block_ac_refine(sc, &hac[c->ta], blk);
    }
}

static void decode_scan(scan_t *sc, comp_t *comp, int ncomp,
                        int *sel, int nsel, int mcux, int mcuy,
                        huff_t *hdc, huff_t *hac) {
    sc->eobrun  = 0;
    sc->rst_left = sc->restart;
    for (int c = 0; c < ncomp; c++) comp[c].dcpred = 0;

    if (nsel == 1) {
        comp_t *c = &comp[sel[0]];
        for (int by = 0; by < c->nbh; by++) {
            for (int bx = 0; bx < c->nbw; bx++) {
                int16_t *blk = c->coef + ((size_t)by * c->bw + bx) * 64;
                decode_one_block(sc, c, hdc, hac, blk);
                if (sc->restart && --sc->rst_left == 0 &&
                    !(by == c->nbh - 1 && bx == c->nbw - 1))
                    scan_restart(sc, comp, ncomp);
            }
        }
        return;
    }

    for (int my = 0; my < mcuy; my++) {
        for (int mx = 0; mx < mcux; mx++) {
            for (int i = 0; i < nsel; i++) {
                comp_t *c = &comp[sel[i]];
                for (int by = 0; by < c->v; by++) {
                    for (int bx = 0; bx < c->h; bx++) {
                        int brow = my * c->v + by;
                        int bcol = mx * c->h + bx;
                        int16_t *blk = c->coef + ((size_t)brow * c->bw + bcol) * 64;
                        decode_one_block(sc, c, hdc, hac, blk);
                    }
                }
            }
            if (sc->restart && --sc->rst_left == 0 &&
                !(my == mcuy - 1 && mx == mcux - 1))
                scan_restart(sc, comp, ncomp);
        }
    }
}

static size_t next_marker(const uint8_t *d, size_t n, size_t p) {
    while (p + 1 < n) {
        if (d[p] == 0xFF) {
            uint8_t m = d[p + 1];
            if (m != 0x00 && m != 0xFF && !(m >= 0xD0 && m <= 0xD7)) return p;
        }
        p++;
    }
    return n;
}

int image_decode_jpeg(const uint8_t *d, size_t n, image_t *out) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return -1;
    idct_init();

    int qt[4][64]; memset(qt, 0, sizeof qt);
    huff_t hdc[4], hac[4];
    memset(hdc, 0, sizeof hdc); memset(hac, 0, sizeof hac);
    comp_t comp[4]; memset(comp, 0, sizeof comp);
    int ncomp = 0, W = 0, H = 0;
    int hmax = 1, vmax = 1, mcux = 0, mcuy = 0;
    int planned = 0, got_scan = 0;

    scan_t sc;
    memset(&sc, 0, sizeof sc);

    size_t p = 2;
    int rc = -1;

    while (p + 2 <= n) {
        if (d[p] != 0xFF) { p++; continue; }
        uint8_t m = d[p + 1];
        p += 2;
        if (m == 0xD9) break;
        if (m == 0x01 || m == 0xFF || (m >= 0xD0 && m <= 0xD7)) continue;
        if (p + 2 > n) break;
        uint32_t seg = be16(d + p);
        const uint8_t *body = d + p + 2;
        size_t blen = seg >= 2 ? seg - 2 : 0;
        if (seg < 2 || p + seg > n) break;

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
        } else if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            if (blen < 6) break;
            sc.progressive = (m == 0xC2);
            H = (int)be16(body + 1);
            W = (int)be16(body + 3);
            ncomp = body[5];
            if (ncomp < 1 || ncomp > 4) break;
            if (blen < (size_t)(6 + ncomp * 3)) break;
            for (int c = 0; c < ncomp; c++) {
                comp[c].id = body[6 + c * 3];
                comp[c].h  = body[7 + c * 3] >> 4;
                comp[c].v  = body[7 + c * 3] & 15;
                comp[c].tq = body[8 + c * 3];
                if (comp[c].h < 1 || comp[c].h > 4 || comp[c].v < 1 || comp[c].v > 4) goto done;
            }
        } else if (m == 0xDD) {
            if (blen >= 2) sc.restart = (int)be16(body);
        } else if (m == 0xDA) {
            if (W <= 0 || H <= 0 || W > 20000 || H > 20000 || ncomp < 1) break;

            if (!planned) {
                for (int c = 0; c < ncomp; c++) {
                    if (comp[c].h > hmax) hmax = comp[c].h;
                    if (comp[c].v > vmax) vmax = comp[c].v;
                }
                mcux = (W + hmax * 8 - 1) / (hmax * 8);
                mcuy = (H + vmax * 8 - 1) / (vmax * 8);
                for (int c = 0; c < ncomp; c++) {
                    comp[c].bw = mcux * comp[c].h;
                    comp[c].bh = mcuy * comp[c].v;
                    comp[c].cw = comp[c].bw * 8;
                    comp[c].ch = comp[c].bh * 8;
                    int px = (W * comp[c].h + hmax - 1) / hmax;
                    int py = (H * comp[c].v + vmax - 1) / vmax;
                    comp[c].nbw = (px + 7) / 8;
                    comp[c].nbh = (py + 7) / 8;
                    comp[c].coef = calloc((size_t)comp[c].bw * comp[c].bh * 64, sizeof(int16_t));
                    if (!comp[c].coef) goto done;
                }
                planned = 1;
            }

            int ns = body[0];
            if (ns < 1 || ns > 4 || blen < (size_t)(4 + ns * 2)) break;
            int sel[4], nsel = 0;
            for (int i = 0; i < ns; i++) {
                int cid = body[1 + i * 2];
                int t   = body[2 + i * 2];
                for (int c = 0; c < ncomp; c++) if (comp[c].id == cid) {
                    comp[c].td = t >> 4;
                    comp[c].ta = t & 15;
                    sel[nsel++] = c;
                }
            }
            if (nsel < 1) break;

            sc.ss = body[1 + ns * 2];
            sc.se = body[2 + ns * 2];
            sc.ah = body[3 + ns * 2] >> 4;
            sc.al = body[3 + ns * 2] & 15;
            if (!sc.progressive) { sc.ss = 0; sc.se = 63; sc.ah = 0; sc.al = 0; }
            if (sc.ss > 63 || sc.se > 63 || sc.ss > sc.se) break;

            sc.br.d = d;
            sc.br.n = n;
            sc.br.p = p + seg;
            sc.br.bitbuf = 0;
            sc.br.bitcnt = 0;
            sc.br.marker = 0;

            decode_scan(&sc, comp, ncomp, sel, nsel, mcux, mcuy, hdc, hac);
            got_scan = 1;

            p = next_marker(d, n, sc.br.p);
            continue;
        }
        p += seg;
    }

done:
    if (!planned || !got_scan) { rc = -1; goto cleanup; }

    for (int c = 0; c < ncomp; c++) {
        comp[c].plane = malloc((size_t)comp[c].cw * comp[c].ch);
        if (!comp[c].plane) { rc = -1; goto cleanup; }
        int *q = qt[comp[c].tq & 3];
        int qnat[64];
        for (int k = 0; k < 64; k++) qnat[ZZ[k]] = q[k];
        for (int by = 0; by < comp[c].bh; by++) {
            for (int bx = 0; bx < comp[c].bw; bx++) {
                const int16_t *src = comp[c].coef + ((size_t)by * comp[c].bw + bx) * 64;
                int blk[64];
                for (int i = 0; i < 64; i++) blk[i] = src[i] * qnat[i];
                idct8x8(blk, comp[c].plane + (size_t)by * 8 * comp[c].cw + bx * 8,
                        comp[c].cw);
            }
        }
    }

    {
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
                if (ncomp >= 3) {
                    comp_t *cb = &comp[1], *cr = &comp[2];
                    int bx = x * cb->h / hmax, byy = y * cb->v / vmax;
                    int rx = x * cr->h / hmax, ry = y * cr->v / vmax;
                    Cb = cb->plane[(size_t)byy * cb->cw + bx];
                    Cr = cr->plane[(size_t)ry * cr->cw + rx];
                }
                int r, g, b;
                if (ncomp < 3) { r = g = b = Y; }
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
    }

cleanup:
    for (int c = 0; c < 4; c++) { free(comp[c].coef); free(comp[c].plane); }
    return rc;
}

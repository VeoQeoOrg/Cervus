#include <image.h>
#include <inflate.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

int image_decode_png(const uint8_t *d, size_t n, image_t *out) {
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (n < 8 || memcmp(d, sig, 8) != 0) return -1;

    uint32_t w = 0, h = 0;
    int bitdepth = 0, ct = -1, interlace = 0;
    uint8_t plte[256 * 3]; int nplte = 0;
    uint8_t trns[256]; int ntrns = 0;
    uint8_t trns_r = 0, trns_g = 0, trns_b = 0; int have_ktrns = 0;

    uint8_t *idat = NULL; size_t idat_len = 0, idat_cap = 0;

    size_t p = 8;
    while (p + 8 <= n) {
        uint32_t len = be32(d + p);
        const uint8_t *type = d + p + 4;
        const uint8_t *body = d + p + 8;
        if (p + 12 + (size_t)len > n) break;

        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(body); h = be32(body + 4);
            bitdepth = body[8]; ct = body[9]; interlace = body[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            nplte = (int)(len / 3);
            if (nplte > 256) nplte = 256;
            memcpy(plte, body, (size_t)nplte * 3);
        } else if (!memcmp(type, "tRNS", 4)) {
            if (ct == 3) {
                ntrns = (int)(len > 256 ? 256 : len);
                memcpy(trns, body, (size_t)ntrns);
            } else if (ct == 0 && len >= 2) {
                trns_r = trns_g = trns_b = body[1]; have_ktrns = 1;
            } else if (ct == 2 && len >= 6) {
                trns_r = body[1]; trns_g = body[3]; trns_b = body[5]; have_ktrns = 1;
            }
        } else if (!memcmp(type, "IDAT", 4)) {
            if (idat_len + len > idat_cap) {
                size_t nc = (idat_len + len) * 2 + 4096;
                uint8_t *ni = realloc(idat, nc);
                if (!ni) { free(idat); return -1; }
                idat = ni; idat_cap = nc;
            }
            memcpy(idat + idat_len, body, len);
            idat_len += len;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        p += 12 + len;
    }

    if (ct < 0 || w == 0 || h == 0 || w > 20000 || h > 20000 || !idat) { free(idat); return -1; }
    if (interlace != 0) { free(idat); return -1; }
    if (bitdepth != 1 && bitdepth != 2 && bitdepth != 4 && bitdepth != 8 && bitdepth != 16) { free(idat); return -1; }

    int channels;
    switch (ct) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: free(idat); return -1;
    }

    uint8_t *raw = NULL; size_t rawlen = 0;
    if (zlib_inflate(idat, idat_len, &raw, &rawlen) != 0) { free(idat); return -1; }
    free(idat);

    size_t rowbytes = ((size_t)w * channels * bitdepth + 7) / 8;
    int bpp = (channels * bitdepth + 7) / 8;
    if (bpp < 1) bpp = 1;
    if (rawlen < (rowbytes + 1) * h) { free(raw); return -1; }

    uint8_t *img = malloc(rowbytes * h);
    if (!img) { free(raw); return -1; }

    for (uint32_t y = 0; y < h; y++) {
        uint8_t ft = raw[y * (rowbytes + 1)];
        const uint8_t *src = raw + y * (rowbytes + 1) + 1;
        uint8_t *cur = img + (size_t)y * rowbytes;
        const uint8_t *prev = y ? (img + (size_t)(y - 1) * rowbytes) : NULL;
        for (size_t i = 0; i < rowbytes; i++) {
            int a = (i >= (size_t)bpp) ? cur[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            int c = (prev && i >= (size_t)bpp) ? prev[i - bpp] : 0;
            int v = src[i];
            switch (ft) {
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
                default: break;
            }
            cur[i] = (uint8_t)v;
        }
    }
    free(raw);

    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px) { free(img); return -1; }

    int maxv = (1 << bitdepth) - 1;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = img + (size_t)y * rowbytes;
        uint32_t *drow = px + (size_t)y * w;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t r, g, bl, al = 255;
            if (bitdepth == 16) {
                const uint8_t *s = row + (size_t)x * channels * 2;
                if (ct == 0) { r = g = bl = s[0]; if (have_ktrns && s[0] == trns_r) al = 0; }
                else if (ct == 2) { r = s[0]; g = s[2]; bl = s[4]; if (have_ktrns && s[0]==trns_r && s[2]==trns_g && s[4]==trns_b) al=0; }
                else if (ct == 4) { r = g = bl = s[0]; al = s[2]; }
                else { r = s[0]; g = s[2]; bl = s[4]; al = s[6]; }
            } else if (bitdepth == 8) {
                const uint8_t *s = row + (size_t)x * channels;
                if (ct == 0) { r = g = bl = s[0]; if (have_ktrns && s[0] == trns_r) al = 0; }
                else if (ct == 2) { r = s[0]; g = s[1]; bl = s[2]; if (have_ktrns && s[0]==trns_r && s[1]==trns_g && s[2]==trns_b) al=0; }
                else if (ct == 3) {
                    int idx = s[0];
                    r = plte[idx*3]; g = plte[idx*3+1]; bl = plte[idx*3+2];
                    al = (idx < ntrns) ? trns[idx] : 255;
                } else if (ct == 4) { r = g = bl = s[0]; al = s[1]; }
                else { r = s[0]; g = s[1]; bl = s[2]; al = s[3]; }
            } else {
                int bitpos = (int)x * channels * bitdepth;
                int byte = bitpos >> 3;
                int shift = 8 - bitdepth - (bitpos & 7);
                int val = (row[byte] >> shift) & maxv;
                if (ct == 3) {
                    r = plte[val*3]; g = plte[val*3+1]; bl = plte[val*3+2];
                    al = (val < ntrns) ? trns[val] : 255;
                } else {
                    int gg = val * 255 / maxv;
                    r = g = bl = (uint8_t)gg;
                    if (have_ktrns && val == trns_r) al = 0;
                }
            }
            drow[x] = ((uint32_t)al << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | bl;
        }
    }
    free(img);

    out->w = (int)w; out->h = (int)h; out->px = px;
    return 0;
}

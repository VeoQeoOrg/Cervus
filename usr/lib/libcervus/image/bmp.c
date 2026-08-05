#include <image.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | ((uint32_t)rd16(p + 2) << 16); }

int image_decode_bmp(const uint8_t *d, size_t n, image_t *out) {
    if (n < 54 || d[0] != 'B' || d[1] != 'M') return -1;
    uint32_t off  = rd32(d + 10);
    uint32_t hsz  = rd32(d + 14);
    int32_t  w    = (int32_t)rd32(d + 18);
    int32_t  h    = (int32_t)rd32(d + 22);
    uint32_t bpp  = rd16(d + 28);
    uint32_t comp = rd32(d + 30);
    if (hsz < 40 || w <= 0 || w > 20000) return -1;

    int top_down = 0;
    if (h < 0) { top_down = 1; h = -h; }
    if (h <= 0 || h > 20000) return -1;
    if (comp != 0 && comp != 3) return -1;
    if (bpp != 8 && bpp != 24 && bpp != 32) return -1;

    uint32_t pal[256];
    if (bpp == 8) {
        uint32_t poff = 14 + hsz;
        uint32_t ncol = rd32(d + 46);
        if (ncol == 0) ncol = 256;
        if (ncol > 256) ncol = 256;
        for (uint32_t i = 0; i < 256; i++) pal[i] = 0xFF000000u;
        for (uint32_t i = 0; i < ncol; i++) {
            uint32_t e = poff + i * 4;
            if (e + 3 >= n) break;
            pal[i] = 0xFF000000u | ((uint32_t)d[e + 2] << 16) | ((uint32_t)d[e + 1] << 8) | d[e];
        }
    }

    size_t rowbytes = ((size_t)w * bpp + 31) / 32 * 4;
    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px) return -1;

    for (int y = 0; y < h; y++) {
        int srcy = top_down ? y : (h - 1 - y);
        size_t ro = off + (size_t)srcy * rowbytes;
        uint32_t *drow = px + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            uint32_t c = 0xFF000000u;
            if (bpp == 8) {
                size_t e = ro + x;
                if (e < n) c = pal[d[e]];
            } else if (bpp == 24) {
                size_t e = ro + (size_t)x * 3;
                if (e + 2 < n) c = 0xFF000000u | ((uint32_t)d[e + 2] << 16) | ((uint32_t)d[e + 1] << 8) | d[e];
            } else {
                size_t e = ro + (size_t)x * 4;
                if (e + 3 < n) c = ((uint32_t)d[e + 3] << 24) | ((uint32_t)d[e + 2] << 16) | ((uint32_t)d[e + 1] << 8) | d[e];
            }
            drow[x] = c;
        }
    }

    out->w = w; out->h = h; out->px = px;
    return 0;
}

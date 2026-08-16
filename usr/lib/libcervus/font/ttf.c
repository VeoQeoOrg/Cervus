#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/cervus.h>

static double ttf_fmod(double a, double b) {
    if (b == 0.0) return 0.0;
    double q = a / b;
    double t = (q < 0) ? ceil(q) : floor(q);
    return a - t * b;
}

static double ttf_acos(double x) {
    if (x <= -1.0) return 3.14159265358979323846;
    if (x >= 1.0) return 0.0;
    double neg = (x < 0);
    if (neg) x = -x;
    double r = (-0.0187293 * x + 0.0742610) * x;
    r = (r - 0.2121144) * x + 1.5707288;
    r = r * sqrt(1.0 - x);
    return neg ? (3.14159265358979323846 - r) : r;
}

#define STBTT_ifloor(x)   ((int)floor(x))
#define STBTT_iceil(x)    ((int)ceil(x))
#define STBTT_sqrt(x)     sqrt(x)
#define STBTT_pow(x,y)    pow(x,y)
#define STBTT_fmod(x,y)   ttf_fmod(x,y)
#define STBTT_cos(x)      cos(x)
#define STBTT_acos(x)     ttf_acos(x)
#define STBTT_fabs(x)     fabs(x)
#define STBTT_malloc(x,u) ((void)(u), malloc(x))
#define STBTT_free(x,u)   ((void)(u), free(x))
#define STBTT_assert(x)   ((void)0)
#define STBTT_strlen(x)   strlen(x)
#define STBTT_memcpy      memcpy
#define STBTT_memset      memset
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

static const uint32_t RANGES[][2] = {
    { 0x20, 0x7E },
    { 0xA0, 0xFF },
    { 0x400, 0x45F },
    { 0x2500, 0x257F },
};
#define NRANGES (sizeof(RANGES) / sizeof(RANGES[0]))

int cervus_ttf_render(const unsigned char *ttf, unsigned long len, unsigned px,
                      unsigned *out_w, unsigned *out_h, unsigned *out_n,
                      unsigned char **out_glyphs, unsigned short *cp2glyph) {
    (void)len;
    stbtt_fontinfo f;
    if (!stbtt_InitFont(&f, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) return -1;

    if (px < 6) px = 6;
    if (px > CERVUS_FONT_MAX_H) px = CERVUS_FONT_MAX_H;

    float scale = stbtt_ScaleForPixelHeight(&f, (float)px);
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&f, &ascent, &descent, &linegap);
    int baseline = (int)((float)ascent * scale + 0.5f);

    unsigned cellH = px;
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f, '0', &adv, &lsb);
    int cw = (int)((float)adv * scale + 0.5f);
    if (cw < 4) cw = (int)px / 2;
    if (cw < 4) cw = 4;
    if (cw > CERVUS_FONT_MAX_W) cw = CERVUS_FONT_MAX_W;
    unsigned cellW = (unsigned)cw;

    unsigned nglyph = 0;
    for (unsigned r = 0; r < NRANGES; r++)
        nglyph += RANGES[r][1] - RANGES[r][0] + 1;

    unsigned char *glyphs = calloc((size_t)nglyph * cellW * cellH, 1);
    if (!glyphs) return -1;
    for (unsigned i = 0; i < CERVUS_FONT_CP_MAP_SIZE; i++) cp2glyph[i] = 0xFFFF;

    static uint8_t tmp[128 * 256];
    unsigned slot = 0;
    for (unsigned r = 0; r < NRANGES; r++) {
        for (uint32_t cp = RANGES[r][0]; cp <= RANGES[r][1]; cp++, slot++) {
            if ((slot & 31) == 0) write(2, ".", 1);
            if (cp < CERVUS_FONT_CP_MAP_SIZE) cp2glyph[cp] = (uint16_t)slot;
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&f, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
            int gw = x1 - x0, gh = y1 - y0;
            if (gw <= 0 || gh <= 0 || gw > 128 || gh > 256) continue;
            stbtt_MakeCodepointBitmap(&f, tmp, gw, gh, gw, scale, scale, (int)cp);
            unsigned char *cell = glyphs + (size_t)slot * cellW * cellH;
            int dx = x0, dy = baseline + y0;
            for (int yy = 0; yy < gh; yy++) {
                int cy = dy + yy;
                if (cy < 0 || cy >= (int)cellH) continue;
                for (int xx = 0; xx < gw; xx++) {
                    int cx = dx + xx;
                    if (cx < 0 || cx >= (int)cellW) continue;
                    cell[(size_t)cy * cellW + cx] = tmp[(size_t)yy * gw + xx];
                }
            }
        }
    }

    *out_w = cellW; *out_h = cellH; *out_n = nglyph; *out_glyphs = glyphs;
    return 0;
}

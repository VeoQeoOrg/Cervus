#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/cervus.h>

#define CPN CERVUS_FONT_CP_MAP_SIZE

static uint8_t *read_file(const char *path, size_t *out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "setfont: cannot open %s\n", path); return NULL; }
    long sz = lseek(fd, 0, SEEK_END);
    if (sz <= 0) { close(fd); fprintf(stderr, "setfont: empty file\n"); return NULL; }
    lseek(fd, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { close(fd); return NULL; }
    long got = 0;
    while (got < sz) {
        long r = read(fd, buf + got, (size_t)(sz - got));
        if (r <= 0) break;
        got += r;
    }
    close(fd);
    if (got != sz) { free(buf); fprintf(stderr, "setfont: short read\n"); return NULL; }
    *out_len = (size_t)sz;
    return buf;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void expand_1bpp(uint8_t *dst, const uint8_t *src, uint32_t nglyph,
                        uint32_t w, uint32_t h, uint32_t bpr) {
    for (uint32_t g = 0; g < nglyph; g++) {
        const uint8_t *s = src + (size_t)g * bpr * h;
        uint8_t *d = dst + (size_t)g * w * h;
        for (uint32_t row = 0; row < h; row++) {
            const uint8_t *sr = s + (size_t)row * bpr;
            uint8_t *dr = d + (size_t)row * w;
            for (uint32_t col = 0; col < w; col++)
                dr[col] = (sr[col >> 3] & (0x80 >> (col & 7))) ? 255 : 0;
        }
    }
}

static void map_utf8(uint16_t *cp2, const uint8_t *ut, const uint8_t *end, uint32_t nglyph) {
    uint32_t glyph = 0;
    const uint8_t *p = ut;
    while (p < end && glyph < nglyph) {
        while (p < end && *p != 0xFF) {
            if (*p == 0xFE) { p++; continue; }
            uint32_t cp = 0; uint8_t b = *p;
            if (b < 0x80) { cp = b; p += 1; }
            else if ((b & 0xE0) == 0xC0 && p + 1 < end) { cp = ((uint32_t)(b & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
            else if ((b & 0xF0) == 0xE0 && p + 2 < end) { cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
            else { p += 1; continue; }
            if (cp < CPN && cp2[cp] == 0xFFFF) cp2[cp] = (uint16_t)glyph;
        }
        if (p < end) p++;
        glyph++;
    }
}

static void map_ucs2(uint16_t *cp2, const uint8_t *ut, const uint8_t *end, uint32_t nglyph) {
    uint32_t glyph = 0;
    const uint8_t *p = ut;
    while (p + 1 < end && glyph < nglyph) {
        for (;;) {
            if (p + 1 >= end) break;
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8)); p += 2;
            if (v == 0xFFFF) break;
            if (v == 0xFFFE) continue;
            if (v < CPN && cp2[v] == 0xFFFF) cp2[v] = (uint16_t)glyph;
        }
        glyph++;
    }
}

static void ascii_fallback(uint16_t *cp2, uint32_t nglyph) {
    for (uint32_t c = 0; c < 128 && c < nglyph; c++)
        if (cp2[c] == 0xFFFF) cp2[c] = (uint16_t)c;
}

static int load_psf(const uint8_t *f, size_t len) {
    uint32_t w, h, nglyph, bpr, charsize, headersize;
    const uint8_t *glyphs;
    int has_uni;
    const uint8_t *ut;
    int utf8;

    if (len >= 4 && f[0] == 0x72 && f[1] == 0xb5 && f[2] == 0x4a && f[3] == 0x86) {
        if (len < 32) { fprintf(stderr, "setfont: truncated PSF2\n"); return 1; }
        headersize = rd32(f + 8);
        uint32_t flags = rd32(f + 12);
        nglyph   = rd32(f + 16);
        charsize = rd32(f + 20);
        h        = rd32(f + 24);
        w        = rd32(f + 28);
        bpr      = (w + 7) / 8;
        if (charsize != bpr * h) charsize = bpr * h;
        glyphs   = f + headersize;
        has_uni  = (flags & 1);
        ut       = glyphs + (size_t)nglyph * charsize;
        utf8     = 1;
    } else if (len >= 4 && f[0] == 0x36 && f[1] == 0x04) {
        uint8_t mode = f[2];
        charsize = f[3];
        w = 8; h = charsize; bpr = 1;
        nglyph = (mode & 0x01) ? 512 : 256;
        headersize = 4;
        glyphs = f + 4;
        has_uni = (mode & 0x02);
        ut = glyphs + (size_t)nglyph * charsize;
        utf8 = 0;
    } else {
        fprintf(stderr, "setfont: not a PSF font\n");
        return 1;
    }

    if (w < 4 || w > CERVUS_FONT_MAX_W || h < 6 || h > CERVUS_FONT_MAX_H) {
        fprintf(stderr, "setfont: unsupported glyph size %ux%u\n", w, h);
        return 1;
    }
    if ((size_t)(glyphs - f) + (size_t)nglyph * charsize > len) {
        fprintf(stderr, "setfont: glyph data out of range\n");
        return 1;
    }

    uint8_t *out = malloc((size_t)nglyph * w * h);
    uint16_t *cp2 = malloc((size_t)CPN * sizeof(uint16_t));
    if (!out || !cp2) { free(out); free(cp2); fprintf(stderr, "setfont: out of memory\n"); return 1; }

    expand_1bpp(out, glyphs, nglyph, w, h, bpr);
    for (uint32_t i = 0; i < CPN; i++) cp2[i] = 0xFFFF;
    if (has_uni && ut < f + len) {
        if (utf8) map_utf8(cp2, ut, f + len, nglyph);
        else      map_ucs2(cp2, ut, f + len, nglyph);
    }
    ascii_fallback(cp2, nglyph);

    int r = cervus_setfont(w, h, nglyph, out, cp2);
    free(out); free(cp2);
    if (r < 0) { fprintf(stderr, "setfont: kernel rejected font (err %d)\n", r); return 1; }
    printf("Loaded %ux%u font, %u glyphs\n", w, h, nglyph);
    return 0;
}

static void usage(void) {
    fprintf(stderr,
        "usage: setfont <font.psf|font.psfu>   load a console font\n"
        "       setfont -r                     reset to the built-in font\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    if (!strcmp(argv[1], "-r") || !strcmp(argv[1], "--reset")) {
        if (cervus_setfont_reset() < 0) { fprintf(stderr, "setfont: reset failed\n"); return 1; }
        printf("Reset to built-in font\n");
        return 0;
    }
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) { usage(); return 0; }

    size_t len = 0;
    uint8_t *f = read_file(argv[1], &len);
    if (!f) return 1;
    int r = load_psf(f, len);
    free(f);
    return r;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <image.h>
#include <sys/cervus.h>

static const char USAGE[] =
    "Usage: img <file>\n"
    "View a PNG/JPEG/BMP/SVG image on the framebuffer (any key to exit).\n";

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fputs(USAGE, stdout);
        return argc < 2 ? 1 : 0;
    }

    image_t img;
    if (image_load(argv[1], &img) != 0) {
        fprintf(stderr, "img: cannot load %s\n", argv[1]);
        return 1;
    }

    cervus_fb_info_t fbi;
    if (cervus_fb_info(&fbi) != 0) {
        fprintf(stderr, "img: no framebuffer (%dx%d image loaded)\n", img.w, img.h);
        image_free(&img);
        return 1;
    }

    int sw = (int)fbi.width, sh = (int)fbi.height;
    int dw = img.w, dh = img.h;
    if (dw > sw || dh > sh) {
        double k = (double)sw / dw;
        if ((double)sh / dh < k) k = (double)sh / dh;
        dw = (int)(img.w * k);
        dh = (int)(img.h * k);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }

    image_t scaled = (dw != img.w || dh != img.h) ? image_scale(&img, dw, dh) : img;
    if (!scaled.px) scaled = img;

    int ox = (sw - dw) / 2, oy = (sh - dh) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    uint32_t *screen = calloc((size_t)sw * sh, 4);
    if (!screen) { image_free(&img); return 1; }
    for (int y = 0; y < dh && oy + y < sh; y++) {
        const uint32_t *s = scaled.px + (size_t)y * dw;
        uint32_t *d = screen + (size_t)(oy + y) * sw + ox;
        for (int x = 0; x < dw && ox + x < sw; x++) {
            uint32_t p = s[x];
            unsigned a = (p >> 24) & 0xFF;
            unsigned r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            d[x] = ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
        }
    }

    struct termios orig, raw;
    int have_tio = (tcgetattr(0, &orig) == 0);
    if (have_tio) {
        raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSAFLUSH, &raw);
    }

    cervus_fb_acquire();
    cervus_fb_blit(screen, 0, 0, sw, sh);

    char c;
    read(0, &c, 1);
    cervus_fb_release();
    if (have_tio) tcsetattr(0, TCSAFLUSH, &orig);

    free(screen);
    if (scaled.px != img.px) image_free(&scaled);
    image_free(&img);
    return 0;
}

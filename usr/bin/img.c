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

    uint32_t *frame = malloc((size_t)dw * dh * 4);
    if (!frame) { image_free(&img); return 1; }
    for (int i = 0; i < dw * dh; i++) {
        uint32_t p = scaled.px[i];
        unsigned a = (p >> 24) & 0xFF;
        unsigned r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        r = r * a / 255; g = g * a / 255; b = b * a / 255;
        frame[i] = (r << 16) | (g << 8) | b;
    }

    int ox = (sw - dw) / 2, oy = (sh - dh) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    struct termios orig, raw;
    int have_tio = (tcgetattr(0, &orig) == 0);
    if (have_tio) {
        raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSAFLUSH, &raw);
    }

    cervus_fb_acquire();
    uint32_t *black = calloc((size_t)sw, 4);
    if (black) { for (int y = 0; y < sh; y++) cervus_fb_blit(black, 0, y, sw, 1); free(black); }
    for (int y = 0; y < dh; y++) cervus_fb_blit(frame + (size_t)y * dw, ox, oy + y, dw, 1);

    char c;
    read(0, &c, 1);
    cervus_fb_release();
    if (have_tio) tcsetattr(0, TCSAFLUSH, &orig);

    free(frame);
    if (scaled.px != img.px) image_free(&scaled);
    image_free(&img);
    return 0;
}

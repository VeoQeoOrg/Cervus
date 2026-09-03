#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <image.h>
#include <sys/cervus.h>
#include <sys/syscall.h>

#define TIOCSNONBLOCK 0x5481

static const char USAGE[] =
    "Usage: img <file>\n"
    "View a PNG/JPEG/BMP/SVG/GIF image on the framebuffer (any key to exit).\n"
    "Animated GIFs play in a loop.\n";

static int sw, sh, ox, oy, dw, dh;

static void fit(int iw, int ih) {
    dw = iw; dh = ih;
    if (dw > sw || dh > sh) {
        double k = (double)sw / dw;
        if ((double)sh / dh < k) k = (double)sh / dh;
        dw = (int)(iw * k);
        dh = (int)(ih * k);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    ox = (sw - dw) / 2; oy = (sh - dh) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
}

static void render(const image_t *img, uint32_t *screen) {
    image_t scaled = (dw != img->w || dh != img->h) ? image_scale(img, dw, dh) : *img;
    if (!scaled.px) scaled = *img;
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
    if (scaled.px != img->px) image_free(&scaled);
}

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)sz);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(b); return NULL; }
    *len = got;
    return b;
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fputs(USAGE, stdout);
        return argc < 2 ? 1 : 0;
    }

    size_t flen = 0;
    uint8_t *fdata = slurp(argv[1], &flen);
    if (!fdata) {
        fprintf(stderr, "img: cannot read %s\n", argv[1]);
        return 1;
    }

    cervus_fb_info_t fbi;
    if (cervus_fb_info(&fbi) != 0) {
        fprintf(stderr, "img: no framebuffer\n");
        free(fdata);
        return 1;
    }
    sw = (int)fbi.width; sh = (int)fbi.height;

    gif_anim_t anim;
    int animated = 0;
    if (flen >= 6 && memcmp(fdata, "GIF8", 4) == 0 &&
        gif_decode(fdata, flen, &anim) == 0 && anim.nframes > 1)
        animated = 1;

    image_t still = { 0, 0, NULL };
    if (!animated && image_load_mem(fdata, flen, argv[1], &still) != 0) {
        fprintf(stderr, "img: cannot decode %s\n", argv[1]);
        free(fdata);
        return 1;
    }

    uint32_t *screen = calloc((size_t)sw * sh, 4);
    if (!screen) { free(fdata); return 1; }

    struct termios orig, raw;
    int have_tio = (tcgetattr(0, &orig) == 0);
    if (have_tio) {
        raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSAFLUSH, &raw);
    }

    cervus_fb_acquire();

    if (animated) {
        fit(anim.w, anim.h);
        memset(screen, 0, (size_t)sw * sh * 4);
        int nb = 1;
        ioctl(0, TIOCSNONBLOCK, &nb);
        int quit = 0;
        while (!quit) {
            for (int i = 0; i < anim.nframes && !quit; i++) {
                image_t fr = { anim.w, anim.h, anim.frames[i] };
                render(&fr, screen);
                cervus_fb_blit(screen, 0, 0, sw, sh);
                int ms = anim.delays_ms[i];
                if (ms < 20) ms = 20;
                for (int slept = 0; slept < ms && !quit; slept += 20) {
                    syscall1(SYS_SLEEP_NS, 20000000ULL);
                    char c;
                    if (read(0, &c, 1) == 1) quit = 1;
                }
            }
        }
        nb = 0;
        ioctl(0, TIOCSNONBLOCK, &nb);
        gif_free(&anim);
    } else {
        fit(still.w, still.h);
        memset(screen, 0, (size_t)sw * sh * 4);
        render(&still, screen);
        cervus_fb_blit(screen, 0, 0, sw, sh);
        char c;
        read(0, &c, 1);
        image_free(&still);
    }

    cervus_fb_release();
    if (have_tio) tcsetattr(0, TCSAFLUSH, &orig);
    free(screen);
    free(fdata);
    return 0;
}

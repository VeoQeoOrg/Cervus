#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: fbdemo\nDraw a color gradient to the framebuffer (any key to exit).\n";

int main(int argc, char **argv) {
    if (cervus_check_help_version(argc, argv, USAGE, "fbdemo")) return 0;
    (void)argc; (void)argv;

    cervus_fb_info_t fbi;
    if (cervus_fb_info(&fbi) != 0) {
        fputs("fbdemo: no framebuffer\n", stderr);
        return 1;
    }

    unsigned w = fbi.width, h = fbi.height;
    if (w > 1920) w = 1920;
    if (h > 1080) h = 1080;

    uint32_t *row = malloc((size_t)w * 4);
    if (!row) return 1;

    struct termios orig, raw;
    int have_tio = (tcgetattr(0, &orig) == 0);
    if (have_tio) {
        raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSAFLUSH, &raw);
    }

    cervus_fb_acquire();

    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            uint8_t r = (uint8_t)(x * 255 / w);
            uint8_t g = (uint8_t)(y * 255 / h);
            uint8_t b = (uint8_t)(255 - (x * 255 / w));
            row[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        cervus_fb_blit(row, 0, y, w, 1);
    }

    free(row);

    char c;
    read(0, &c, 1);

    cervus_fb_release();

    if (have_tio) tcsetattr(0, TCSAFLUSH, &orig);
    return 0;
}

#ifndef _CERVUS_IMAGE_H
#define _CERVUS_IMAGE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int       w;
    int       h;
    uint32_t *px;
} image_t;

int  image_load(const char *path, image_t *out);
int  image_load_mem(const uint8_t *data, size_t len, const char *hint, image_t *out);
void image_free(image_t *img);

int  image_decode_bmp(const uint8_t *d, size_t n, image_t *out);
int  image_decode_png(const uint8_t *d, size_t n, image_t *out);
int  image_decode_jpeg(const uint8_t *d, size_t n, image_t *out);
int  image_decode_svg(const uint8_t *d, size_t n, image_t *out);
int  image_decode_gif(const uint8_t *d, size_t n, image_t *out);

typedef struct {
    int        w;
    int        h;
    int        nframes;
    int        loops;
    uint32_t **frames;
    int       *delays_ms;
} gif_anim_t;

int  gif_decode(const uint8_t *d, size_t n, gif_anim_t *out);
void gif_free(gif_anim_t *a);

image_t image_scale(const image_t *src, int dw, int dh);

#endif

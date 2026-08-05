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

image_t image_scale(const image_t *src, int dw, int dh);

#endif

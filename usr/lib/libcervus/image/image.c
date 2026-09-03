#include <image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_ext(const char *path, const char *ext) {
    size_t pl = strlen(path), el = strlen(ext);
    if (pl < el) return 0;
    const char *p = path + pl - el;
    for (size_t i = 0; i < el; i++) {
        char a = p[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

int image_load_mem(const uint8_t *d, size_t n, const char *hint, image_t *out) {
    if (!d || n < 4) return -1;

    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G')
        return image_decode_png(d, n, out);
    if (d[0] == 0xFF && d[1] == 0xD8)
        return image_decode_jpeg(d, n, out);
    if (d[0] == 'B' && d[1] == 'M')
        return image_decode_bmp(d, n, out);
    if (n >= 6 && memcmp(d, "GIF8", 4) == 0)
        return image_decode_gif(d, n, out);

    if (hint) {
        if (has_ext(hint, ".svg")) return image_decode_svg(d, n, out);
        if (has_ext(hint, ".png")) return image_decode_png(d, n, out);
        if (has_ext(hint, ".jpg") || has_ext(hint, ".jpeg")) return image_decode_jpeg(d, n, out);
        if (has_ext(hint, ".bmp")) return image_decode_bmp(d, n, out);
        if (has_ext(hint, ".gif")) return image_decode_gif(d, n, out);
    }

    for (size_t i = 0; i < n && i < 64; i++)
        if (d[i] == '<') return image_decode_svg(d, n, out);

    return -1;
}

int image_load(const char *path, image_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return -1; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return -1; }
    int r = image_load_mem(buf, got, path, out);
    free(buf);
    return r;
}

void image_free(image_t *img) {
    if (!img) return;
    free(img->px);
    img->px = NULL;
    img->w = img->h = 0;
}

image_t image_scale(const image_t *src, int dw, int dh) {
    image_t out = { 0, 0, NULL };
    if (!src || !src->px || dw <= 0 || dh <= 0) return out;
    out.px = malloc((size_t)dw * dh * 4);
    if (!out.px) return out;
    out.w = dw; out.h = dh;
    for (int y = 0; y < dh; y++) {
        int sy = (int)((int64_t)y * src->h / dh);
        if (sy >= src->h) sy = src->h - 1;
        const uint32_t *srow = src->px + (size_t)sy * src->w;
        uint32_t *drow = out.px + (size_t)y * dw;
        for (int x = 0; x < dw; x++) {
            int sx = (int)((int64_t)x * src->w / dw);
            if (sx >= src->w) sx = src->w - 1;
            drow[x] = srow[sx];
        }
    }
    return out;
}

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <limine.h>
#include "../../../include/graphics/fb/fb.h"
#include "../../../include/io/serial.h"

uint32_t *g_backbuf = NULL;
uint32_t  g_bb_pitch = 0;
static uint32_t  g_bb_w = 0;
static uint32_t  g_bb_h = 0;

void fb_init_backbuffer(struct limine_framebuffer *fb) {
    if (!fb) return;
    g_bb_w = fb->width;
    g_bb_h = fb->height;
    g_bb_pitch = fb->pitch / 4;
    size_t sz = (size_t)g_bb_pitch * g_bb_h * sizeof(uint32_t);
    g_backbuf = (uint32_t *)malloc(sz);
    if (g_backbuf) {
        memcpy(g_backbuf, fb->address, sz);
        serial_printf("[FB] Backbuffer allocated: %ux%u (%zu KB)\n",
                      g_bb_w, g_bb_h, sz / 1024);
    } else {
        serial_printf("[FB] WARNING: backbuffer alloc failed, using direct VRAM\n");
    }
}

static inline uint32_t *fb_get_buf(struct limine_framebuffer *fb) {
    return g_backbuf ? g_backbuf : (uint32_t *)fb->address;
}

static inline uint32_t fb_get_pitch(struct limine_framebuffer *fb) {
    return g_backbuf ? g_bb_pitch : (fb->pitch / 4);
}

void fb_flush(struct limine_framebuffer *fb) {
    if (!fb || !g_backbuf) return;
    memcpy(fb->address, g_backbuf, (size_t)g_bb_pitch * g_bb_h * sizeof(uint32_t));
}

void fb_flush_lines(struct limine_framebuffer *fb, uint32_t y_start, uint32_t y_end) {
    if (!fb || !g_backbuf) return;
    if (y_start >= g_bb_h) return;
    if (y_end > g_bb_h) y_end = g_bb_h;
    if (y_start >= y_end) return;
    uint32_t *dst = (uint32_t *)fb->address + y_start * g_bb_pitch;
    uint32_t *src = g_backbuf + y_start * g_bb_pitch;
    size_t bytes = (size_t)(y_end - y_start) * g_bb_pitch * sizeof(uint32_t);
    memcpy(dst, src, bytes);
}

void fb_draw_pixel(struct limine_framebuffer *fb, uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb->width || y >= fb->height) return;
    uint32_t *buf = fb_get_buf(fb);
    uint32_t pitch = fb_get_pitch(fb);
    buf[y * pitch + x] = color;
}

void fb_fill_rect(struct limine_framebuffer *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb) return;
    uint32_t *buf = fb_get_buf(fb);
    uint32_t pitch = fb_get_pitch(fb);

    if (x >= fb->width || y >= fb->height) return;
    if (x + w > fb->width)  w = fb->width - x;
    if (y + h > fb->height) h = fb->height - y;

    for (uint32_t py = 0; py < h; py++) {
        uint32_t *row = buf + (y + py) * pitch + x;
        if (color == 0) {
            memset(row, 0, w * sizeof(uint32_t));
        } else {
            for (uint32_t px = 0; px < w; px++)
                row[px] = color;
        }
    }
}

void fb_clear(struct limine_framebuffer *fb, uint32_t color) {
    if (!fb) return;
    uint32_t *buf = fb_get_buf(fb);
    size_t total = (size_t)fb_get_pitch(fb) * fb->height;
    if (color == 0) {
        memset(buf, 0, total * sizeof(uint32_t));
    } else {
        for (size_t i = 0; i < total; i++)
            buf[i] = color;
    }
}

int psf_validate(void) {
    const uint8_t *raw = get_font_data();
    if (raw[0] == 0x72 && raw[1] == 0xb5 &&
        raw[2] == 0x4a && raw[3] == 0x86)
        return 2;
    if (raw[0] == 0x36 && raw[1] == 0x04)
        return 1;
    return 0;
}

void fb_draw_char(struct limine_framebuffer *fb, char c, uint32_t x, uint32_t y, uint32_t color) {
    const uint8_t *raw = get_font_data();
    uint32_t headersize = 32;
    uint32_t charsiz = *(uint32_t*)(raw + 20);
    uint8_t *glyphs = (uint8_t*)raw + headersize;

    uint32_t glyph_index = ((uint8_t)c < 128) ? (uint8_t)c : '?';
    if (glyph_index >= 512) glyph_index = '?';

    uint8_t *glyph = &glyphs[glyph_index * charsiz];
    uint32_t *buf = fb_get_buf(fb);
    uint32_t pitch = fb_get_pitch(fb);

    if (x + 8 > fb->width || y + 16 > fb->height) return;

    for (uint32_t row = 0; row < 16; row++) {
        uint8_t byte = glyph[row];
        uint32_t *line = buf + (y + row) * pitch + x;
        for (uint32_t col = 0; col < 8; col++) {
            if (byte & (0x80 >> col))
                line[col] = color;
        }
    }
}

void fb_draw_string(struct limine_framebuffer *fb, const char *str, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t orig_x = x;
    if (!psf_validate()) return;
    while (*str) {
        if (*str == '\n') { x = orig_x; y += 16; }
        else { fb_draw_char(fb, *str, x, y, color); x += 8; }
        str++;
    }
}

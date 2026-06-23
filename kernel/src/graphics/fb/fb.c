#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <limine.h>
#include "../../../include/graphics/fb/fb.h"
#include "../../../include/io/serial.h"
#include "../../../include/memory/vmm.h"

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

    uintptr_t fb_virt = (uintptr_t)fb->address & ~0xFFFULL;
    size_t fb_pages = (sz + 0xFFF) >> 12;
    if (vmm_remap_range_wc(vmm_get_kernel_pagemap(), fb_virt, fb_pages)) {
        serial_printf("[FB] Framebuffer remapped as WC: %zu pages from 0x%llx\n",
                      fb_pages, (unsigned long long)fb_virt);
    } else {
        serial_printf("[FB] WARNING: WC remap failed; framebuffer using default cache attr\n");
    }

    g_backbuf = (uint32_t *)malloc(sz);
    if (g_backbuf) {
        memcpy(g_backbuf, fb->address, sz);
        serial_printf("[FB] Backbuffer allocated: %ux%u (%zu KB)\n",
                      g_bb_w, g_bb_h, sz / 1024);
    } else {
        serial_printf("[FB] WARNING: backbuffer alloc failed, using direct VRAM\n");
    }
}

void fb_set_backbuffer(uint32_t *buf) {
    if (buf) g_backbuf = buf;
}

uint32_t *fb_get_backbuffer(void) {
    return g_backbuf;
}

uint32_t fb_backbuffer_pitch(void) {
    return g_bb_pitch;
}

size_t fb_backbuffer_bytes(void) {
    return (size_t)g_bb_pitch * g_bb_h * sizeof(uint32_t);
}

static inline uint32_t *fb_get_buf(struct limine_framebuffer *fb) {
    return g_backbuf ? g_backbuf : (uint32_t *)fb->address;
}

static inline uint32_t fb_get_pitch(struct limine_framebuffer *fb) {
    return g_backbuf ? g_bb_pitch : (fb->pitch / 4);
}

void fb_flush(struct limine_framebuffer *fb) {
    if (!fb || !g_backbuf) return;
    fb_flush_lines(fb, 0, g_bb_h);
}

void fb_flush_lines(struct limine_framebuffer *fb, uint32_t y_start, uint32_t y_end) {
    if (!fb || !g_backbuf) return;
    if (y_start >= g_bb_h) return;
    if (y_end > g_bb_h) y_end = g_bb_h;
    if (y_start >= y_end) return;
    uint32_t *dst = (uint32_t *)fb->address + y_start * g_bb_pitch;
    uint32_t *src = g_backbuf + y_start * g_bb_pitch;
    size_t bytes = (size_t)(y_end - y_start) * g_bb_pitch * sizeof(uint32_t);

    size_t qwords = bytes >> 3;
    size_t tail   = bytes &  7;
    asm volatile (
        "rep movsq\n\t"
        : "+D"(dst), "+S"(src), "+c"(qwords)
        :: "memory"
    );
    if (tail) {
        uint8_t *db = (uint8_t *)dst;
        uint8_t *sb = (uint8_t *)src;
        for (size_t i = 0; i < tail; i++) db[i] = sb[i];
    }
    asm volatile ("sfence" ::: "memory");
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

#define CP_MAP_SIZE 0x600

static uint16_t g_cp2glyph[CP_MAP_SIZE];
static int      g_cp_map_ready = 0;

static void build_unicode_map(void) {
    for (uint32_t i = 0; i < CP_MAP_SIZE; i++) g_cp2glyph[i] = 0xFFFF;

    const uint8_t *raw = get_font_data();
    uint32_t headersize = *(const uint32_t *)(raw + 8);
    uint32_t flags      = *(const uint32_t *)(raw + 12);
    uint32_t nglyph     = *(const uint32_t *)(raw + 16);
    uint32_t charsiz    = *(const uint32_t *)(raw + 20);

    if (!(flags & 1)) { g_cp_map_ready = 1; return; }

    const uint8_t *ut  = raw + headersize + (size_t)nglyph * charsiz;
    const uint8_t *end = raw + get_font_data_size();

    uint32_t glyph = 0;
    const uint8_t *p = ut;
    while (p < end && glyph < nglyph) {
        while (p < end && *p != 0xFF) {
            if (*p == 0xFE) { p++; continue; }
            uint32_t cp = 0;
            uint8_t b = *p;
            if (b < 0x80) { cp = b; p += 1; }
            else if ((b & 0xE0) == 0xC0 && p + 1 < end) {
                cp = ((uint32_t)(b & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
            } else if ((b & 0xF0) == 0xE0 && p + 2 < end) {
                cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6)
                   | (p[2] & 0x3F); p += 3;
            } else { p += 1; continue; }
            if (cp < CP_MAP_SIZE && g_cp2glyph[cp] == 0xFFFF)
                g_cp2glyph[cp] = (uint16_t)glyph;
        }
        if (p < end) p++;
        glyph++;
    }
    g_cp_map_ready = 1;
}

static uint32_t codepoint_to_glyph(uint32_t cp) {
    if (cp < 128) return cp;
    if (!g_cp_map_ready) build_unicode_map();
    if (cp < CP_MAP_SIZE && g_cp2glyph[cp] != 0xFFFF) return g_cp2glyph[cp];
    return '?';
}

void fb_draw_char(struct limine_framebuffer *fb, uint32_t cp, uint32_t x, uint32_t y, uint32_t color) {
    const uint8_t *raw = get_font_data();
    uint32_t headersize = *(const uint32_t *)(raw + 8);
    uint32_t charsiz = *(const uint32_t *)(raw + 20);
    uint8_t *glyphs = (uint8_t*)raw + headersize;

    uint32_t glyph_index = codepoint_to_glyph(cp);
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
        else { fb_draw_char(fb, (uint8_t)*str, x, y, color); x += 8; }
        str++;
    }
}

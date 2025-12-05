#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limine.h>
#include "../../../include/graphics/fb/fb.h"

void fb_draw_pixel(struct limine_framebuffer *fb, uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb->width || y >= fb->height) return;
    uint32_t *fb_ptr = (uint32_t *)fb->address;
    uint32_t pitch = fb->pitch / 4;
    fb_ptr[y * pitch + x] = color;
}

void fb_fill_rect(struct limine_framebuffer *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t py = y; py < y + h; py++) {
        for (uint32_t px = x; px < x + w; px++) {
            fb_draw_pixel(fb, px, py, color);
        }
    }
}

void fb_clear(struct limine_framebuffer *fb, uint32_t color) {
    uint32_t *fb_ptr = (uint32_t *)fb->address;
    size_t pixels = fb->width * fb->height;
    for (size_t i = 0; i < pixels; i++) {
        fb_ptr[i] = color;
    }
}

int psf_validate(void) {
    const uint8_t *raw = get_font_data();
    
    // Проверка для PSF v2
    if (raw[0] == 0x72 && raw[1] == 0xb5 && 
        raw[2] == 0x4a && raw[3] == 0x86) {
        return 2; // PSF v2
    }
    
    // Проверка для PSF v1
    if (raw[0] == 0x36 && raw[1] == 0x04) {
        return 1; // PSF v1
    }
    
    return 0; // Невалидный шрифт
}

void fb_draw_char(struct limine_framebuffer *fb, char c, uint32_t x, uint32_t y, uint32_t color) {
    const uint8_t *raw = get_font_data();
    uint32_t headersize = 32;
    uint32_t charsiz = *(uint32_t*)(raw + 20);
    
    uint8_t *glyphs = (uint8_t*)raw + headersize;
    uint32_t glyph_index;
    
    // Обработка разных диапазонов символов
    if ((uint8_t)c < 128) {
        glyph_index = (uint8_t)c;
    } else if ((uint8_t)c >= 128) {
        glyph_index = '?'; 
    } else {
        glyph_index = '?'; 
    }
    
    // Проверка границ
    if (glyph_index >= 512) {
        glyph_index = '?';
    }
    
    uint8_t *glyph = &glyphs[glyph_index * charsiz];

    // Отрисовка
    for (uint32_t row = 0; row < 16; row++) {
        uint8_t byte = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            if (byte & (0x80 >> col)) {
                fb_draw_pixel(fb, x + col, y + row, color);
            }
        }
    }
}

void fb_draw_string(struct limine_framebuffer *fb, const char *str, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t orig_x = x;

    if (!psf_validate()) return;

    while (*str) {
        if (*str == '\n') {
            x = orig_x;
            y += 16;
        } else {
            fb_draw_char(fb, *str, x, y, color);
            x += 8;
        }
        str++;
    }
}
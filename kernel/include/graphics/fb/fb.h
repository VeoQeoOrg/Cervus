#ifndef FB_H
#define FB_H

#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

#define COLOR_BLACK     RGB(  0,   0,   0)
#define COLOR_WHITE     RGB(255, 255, 255)
#define COLOR_RED       RGB(255,   0,   0)
#define COLOR_GREEN     RGB(  0, 255,   0)
#define COLOR_BLUE      RGB(  0,   0, 255)
#define COLOR_CYAN      RGB(  0, 255, 255)
#define COLOR_MAGENTA   RGB(255,   0, 255)
#define COLOR_YELLOW    RGB(255, 255,   0)
#define COLOR_ORANGE    RGB(255, 165,   0)
#define COLOR_GRAY      RGB(128, 128, 128)
#define COLOR_DARKGRAY  RGB( 64,  64,  64)
#define COLOR_BROWN     RGB(165,  42,  42)

// Структура заголовка PSFv2
struct psf_header {
    uint32_t magic;        // 0x72b54a86
    uint32_t version;      // 0
    uint32_t headersize;   // 32
    uint32_t flags;        // 0 или 1 (есть таблица Unicode)
    uint32_t numglyph;     // число глифов
    uint32_t bytesperglyph;// размер одного глифа
    uint32_t height;       // высота символа
    uint32_t width;        // ширина символа (обычно 8)
} __attribute__((packed));

extern uint8_t _binary_font_psf_start[];
extern uint8_t _binary_font_psf_end[];

static inline const uint8_t* get_font_data(void) {
    return (const uint8_t*)&_binary_font_psf_start;
}
static inline size_t get_font_data_size(void) {
    return _binary_font_psf_end - _binary_font_psf_start;
}

static inline const struct psf_header* get_psf_header(void) {
    return (const struct psf_header*)&_binary_font_psf_start;
}

int psf_validate(void);
void fb_draw_pixel(struct limine_framebuffer *fb, uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(struct limine_framebuffer *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_clear(struct limine_framebuffer *fb, uint32_t color);
void fb_draw_char(struct limine_framebuffer *fb, char c, uint32_t x, uint32_t y, uint32_t color);
void fb_draw_string(struct limine_framebuffer *fb, const char *str, uint32_t x, uint32_t y, uint32_t color);

#endif 
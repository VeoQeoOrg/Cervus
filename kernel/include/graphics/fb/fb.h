#ifndef FB_H
#define FB_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    void    *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
} fb_info_t;

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

struct psf_header {
    uint32_t magic;
    uint32_t version;
    uint32_t headersize;
    uint32_t flags;
    uint32_t numglyph;
    uint32_t bytesperglyph;
    uint32_t height;
    uint32_t width;
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
void fb_draw_pixel(fb_info_t *fb, uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(fb_info_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_clear(fb_info_t *fb, uint32_t color);
void fb_draw_char(fb_info_t *fb, uint32_t cp, uint32_t x, uint32_t y, uint32_t color);
void fb_draw_string(fb_info_t *fb, const char *str, uint32_t x, uint32_t y, uint32_t color);
void fb_init_backbuffer(fb_info_t *fb);
void fb_flush(fb_info_t *fb);
void fb_flush_lines(fb_info_t *fb, uint32_t y_start, uint32_t y_end);

void      fb_set_backbuffer(uint32_t *buf);
uint32_t *fb_get_backbuffer(void);
uint32_t  fb_backbuffer_pitch(void);
size_t    fb_backbuffer_bytes(void);

#endif

#include <stdio.h>
#include "../../../kernel/include/graphics/fb/fb.h"

extern struct limine_framebuffer *global_framebuffer;

void set_cursor_position(uint32_t x, uint32_t y) {
    cursor_x = x;
    cursor_y = y;
}

void set_text_color(uint32_t color) {
    text_color = color;
}

void set_background_color(uint32_t color) {
    bg_color = color;
}

void clear_screen(void) {
    if (global_framebuffer) {
        fb_clear(global_framebuffer, bg_color);
        cursor_x = 0;
        cursor_y = 0;
    }
}

void scroll_up(int lines) {
    if (lines <= 0) return;
    
    scroll_screen(lines);
    
    uint32_t scroll_pixels = (uint32_t)(lines * 16);
    if (cursor_y >= scroll_pixels) {
        cursor_y -= scroll_pixels;
    } else {
        cursor_y = 0;
    }
}
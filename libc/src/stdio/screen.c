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

void clear_screen(void) {
    if (global_framebuffer) {
        fb_clear(global_framebuffer, COLOR_BLACK);
        cursor_x = 0;
        cursor_y = 0;
    }
}
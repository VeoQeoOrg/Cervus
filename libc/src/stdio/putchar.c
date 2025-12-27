#include <stdio.h>
#include <string.h>
#include "../../../kernel/include/graphics/fb/fb.h"

uint32_t cursor_x = 0;
uint32_t cursor_y = 0;
uint32_t text_color = COLOR_WHITE;
uint32_t bg_color = COLOR_BLACK;

extern struct limine_framebuffer *global_framebuffer;

static int scroll_buffer_index = 0;
static int total_scroll_lines = 0;

uint32_t get_screen_width(void) {
    if (!global_framebuffer) return 1024;
    return global_framebuffer->width;
}

uint32_t get_screen_height(void) {
    if (!global_framebuffer) return 768;
    return global_framebuffer->height;
}

void scroll_screen(int lines) {
    if (!global_framebuffer || lines <= 0) return;
    
    uint32_t screen_width = get_screen_width();
    uint32_t screen_height = get_screen_height();
    uint32_t scroll_pixels = (uint32_t)(lines * 16);
    
    uint32_t *fb_ptr = (uint32_t *)global_framebuffer->address;
    uint32_t pitch = global_framebuffer->pitch / 4;
    
    for (uint32_t y = scroll_pixels; y < screen_height; y++) {
        uint32_t *src_row = fb_ptr + y * pitch;
        uint32_t *dst_row = fb_ptr + (y - scroll_pixels) * pitch;
        memcpy(dst_row, src_row, screen_width * sizeof(uint32_t));
    }
    
    for (uint32_t y = screen_height - scroll_pixels; y < screen_height; y++) {
        for (uint32_t x = 0; x < screen_width; x++) {
            fb_draw_pixel(global_framebuffer, x, y, bg_color);
        }
    }
}

int putchar(int c) {
    if (!global_framebuffer) {
        return EOF;
    }
    
    uint32_t screen_width = get_screen_width();
    uint32_t screen_height = get_screen_height();
    uint32_t char_width = 8;
    uint32_t char_height = 16;
    
    switch (c) {
        case '\n': 
            cursor_x = 0;
            cursor_y += char_height; 
            break;
            
        case '\r': 
            cursor_x = 0;
            break;
            
        case '\t': 
            cursor_x = (cursor_x + 32) & ~31; 
            break;
            
        case '\b': 
            if (cursor_x >= char_width) {
                cursor_x -= char_width;
                fb_draw_char(global_framebuffer, ' ', cursor_x, cursor_y, text_color);
            }
            break;
            
        default: 
            if ((uint8_t)c >= 32 && (uint8_t)c <= 126) { 
                fb_draw_char(global_framebuffer, (char)c, cursor_x, cursor_y, text_color);
                cursor_x += char_width; 
            }
            break;
    }
    
    if (cursor_x + char_width > screen_width) {
        cursor_x = 0;
        cursor_y += char_height;
    }
    
    if (cursor_y + char_height > screen_height) {
        int overflow_pixels = (int)((cursor_y + char_height) - screen_height);
        int lines_to_scroll = (overflow_pixels + (int)char_height - 1) / (int)char_height;
        
        scroll_screen(lines_to_scroll);
        cursor_y = screen_height - char_height;
        
        for (uint32_t x = 0; x < screen_width; x += char_width) {
            fb_draw_char(global_framebuffer, ' ', x, cursor_y, text_color);
        }
    }
    
    return (unsigned char)c;
}

void clear_screen_with_scroll(void) {
    if (global_framebuffer) {
        fb_clear(global_framebuffer, bg_color);
        cursor_x = 0;
        cursor_y = 0;
        scroll_buffer_index = 0;
        total_scroll_lines = 0;
    }
}

void get_cursor_position(uint32_t *x, uint32_t *y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}
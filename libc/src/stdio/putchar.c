#include <stdio.h>
#include "../../../kernel/include/graphics/fb/fb.h"

uint32_t cursor_x = 0;
uint32_t cursor_y = 0;
uint32_t text_color = COLOR_WHITE;

extern struct limine_framebuffer *global_framebuffer;

int putchar(int c) {
    if (!global_framebuffer) {
        return EOF;
    }

    switch (c) {
        case '\n': 
            cursor_x = 0;
            cursor_y += 16; 
            break;
            
        case '\r': 
            cursor_x = 0;
            break;
            
        case '\t': 
            cursor_x = (cursor_x + 32) & ~31; 
            break;
            
        case '\b': 
            if (cursor_x >= 8) {
                cursor_x -= 8;
                fb_draw_char(global_framebuffer, ' ', cursor_x, cursor_y, text_color);
            }
            break;
            
        default: 
            if ((uint8_t)c >= 32 && (uint8_t)c <= 126) { 
                fb_draw_char(global_framebuffer, (char)c, cursor_x, cursor_y, text_color);
                cursor_x += 8; 
            }
            break;
    }
    
    // Перенос строки, если достигнут правый край
    // Предполагаем ширину экрана 1024 пикселя для простоты
    // Можно сделать это динамическим на основе global_framebuffer->width
    if (cursor_x + 8 > 1024) {
        cursor_x = 0;
        cursor_y += 16;
    }
    
    // Прокрутка экрана, если достигнут нижний край
    // Предполагаем высоту 768 пикселей
    if (cursor_y + 16 > 768) {
        // TODO: Реализовать прокрутку экрана
        cursor_y = 0; // Пока просто сбрасываем в начало
    }
    
    return (unsigned char)c;
}
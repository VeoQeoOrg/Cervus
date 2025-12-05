#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define EOF (-1)

int putchar(int c);
int puts(const char *str);
int printf(const char *format, ...);

extern uint32_t cursor_x;
extern uint32_t cursor_y;

extern uint32_t text_color;

void set_cursor_position(uint32_t x, uint32_t y);
void set_text_color(uint32_t color);
void clear_screen(void);

#endif 
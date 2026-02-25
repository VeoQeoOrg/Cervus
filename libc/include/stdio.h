#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define EOF (-1)
#define MAX_SCROLL_LINES 1000

int putchar(int c);
int puts(const char *str);
int printf(const char *format, ...);

int sprintf (char* restrict buf, const char* restrict fmt, ...);
int snprintf(char* restrict buf, size_t size, const char* restrict fmt, ...);
int vsprintf (char* restrict buf, const char* restrict fmt, va_list ap);
int vsnprintf(char* restrict buf, size_t size, const char* restrict fmt, va_list ap);

int vprintf(const char* restrict fmt, va_list ap);

extern uint32_t cursor_x;
extern uint32_t cursor_y;

extern uint32_t text_color;
extern uint32_t bg_color;

void scroll_screen(int lines);
uint32_t get_screen_width(void);
uint32_t get_screen_height(void);

void set_cursor_position(uint32_t x, uint32_t y);
void set_text_color(uint32_t color);
void clear_screen(void);
void scroll_up(int lines);

#endif
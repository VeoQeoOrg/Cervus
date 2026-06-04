#ifndef LIBGUI_H
#define LIBGUI_H

#include <stdint.h>
#include <stddef.h>
#include <sys/cervus.h>

typedef struct {
    int x, y, w, h;
    uint32_t bg_color;
    uint32_t title_color;
    char title[32];
    int is_active;
} gui_window_t;

int gui_init(void);
void gui_clear(uint32_t color);
void gui_draw_rect(int x, int y, int w, int h, uint32_t color);
void gui_draw_window(gui_window_t *win);
void gui_flush(void);
void gui_deinit(void);

extern int gui_screen_w;
extern int gui_screen_h;

#endif

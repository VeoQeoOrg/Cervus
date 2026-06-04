#include <libgui.h>
#include <stdlib.h>
#include <string.h>

int gui_screen_w = 0;
int gui_screen_h = 0;
static uint32_t *backbuffer = NULL;

int gui_init(void) {
    cervus_fb_info_t fbi;
    if (cervus_fb_info(&fbi) != 0) return -1;
    gui_screen_w = fbi.width;
    gui_screen_h = fbi.height;
    
    backbuffer = malloc((size_t)(gui_screen_w * gui_screen_h * 4));
    if (!backbuffer) return -1;
    
    cervus_fb_acquire();
    return 0;
}

void gui_clear(uint32_t color) {
    if (!backbuffer) return;
    for (int i = 0; i < gui_screen_w * gui_screen_h; i++) {
        backbuffer[i] = color;
    }
}

void gui_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!backbuffer) return;
    for (int iy = y; iy < y + h; iy++) {
        if (iy < 0 || iy >= gui_screen_h) continue;
        for (int ix = x; ix < x + w; ix++) {
            if (ix < 0 || ix >= gui_screen_w) continue;
            backbuffer[iy * gui_screen_w + ix] = color;
        }
    }
}

void gui_draw_window(gui_window_t *win) {
    if (!backbuffer) return;
    
    // Shadow
    gui_draw_rect(win->x + 5, win->y + 5, win->w, win->h, 0x000000); // shadow
    
    // Window background
    gui_draw_rect(win->x, win->y, win->w, win->h, win->bg_color);
    
    // Window title bar
    uint32_t tb_color = win->is_active ? win->title_color : 0x777777;
    gui_draw_rect(win->x, win->y, win->w, 24, tb_color);
    
    // Close button
    gui_draw_rect(win->x + win->w - 20, win->y + 4, 16, 16, 0xCC3333);
}

void gui_flush(void) {
    if (!backbuffer) return;
    cervus_fb_blit(backbuffer, 0, 0, gui_screen_w, gui_screen_h);
}

void gui_deinit(void) {
    if (backbuffer) {
        free(backbuffer);
        backbuffer = NULL;
    }
    cervus_fb_release();
}

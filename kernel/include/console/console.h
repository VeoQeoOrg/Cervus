#ifndef CONSOLE_CONSOLE_H
#define CONSOLE_CONSOLE_H

#include <stdint.h>
#include <stddef.h>

#define CONSOLE_ESC_MAX_PARAMS 8

typedef struct {
    uint32_t cursor_x, cursor_y;
    uint32_t text_color, bg_color;
    int      cursor_visible, autowrap, flush_inhibit;
    int      scroll_buffer_index, total_scroll_lines;
    uint32_t dirty_y_min, dirty_y_max;
    int      ps_state, ps_nparams, ps_cur, ps_bold, ps_reverse;
    int      ps_params[CONSOLE_ESC_MAX_PARAMS];
    uint32_t saved_cx, saved_cy;
} console_state_t;

typedef struct {
    uint32_t ch;
    uint32_t fg;
    uint32_t bg;
} vt_cell_t;

void console_save_state(console_state_t *s);
void console_load_state(const console_state_t *s);
void console_reset_state(void);
void console_set_offscreen(int on);
void console_set_grid(vt_cell_t *grid, uint32_t cols, uint32_t rows);
void console_redraw_grid(void);
void console_flush_pending(void);
void vt_tick_flush(void);

#define VT_COUNT          12
#define VT_MONITOR_INDEX  1

void vt_init(void);
int  vt_active(void);
void vt_switch(int n);
void vt_handle_chord(int fn);
void console_input_char(char c);

int  vt_take_spawn_request(void);
void vt_mark_shell_running(int n, int running);
void vt_write(int vt, const char *buf, size_t len);
void vt_cursor(int vt, int on);
void vt_get_cursor(int vt, uint32_t *row, uint32_t *col);

void tty_vt_init(void);
void tty_vt_input(int vt, char c);

void monitor_init(void);
void monitor_activate(void);
void monitor_input(char c);
void monitor_tick(void);
void console_boot_logging_off(void);

#endif

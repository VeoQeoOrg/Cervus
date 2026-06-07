#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "../../../kernel/include/graphics/fb/fb.h"
#include "../../../kernel/include/console/console.h"

uint32_t cursor_x   = 0;
uint32_t cursor_y   = 0;
uint32_t text_color = COLOR_WHITE;
uint32_t bg_color   = COLOR_BLACK;

extern struct limine_framebuffer *global_framebuffer;

static int  cursor_visible      = 1;
static int  cursor_shape        = 1;
static int  scroll_buffer_index = 0;
static int  total_scroll_lines  = 0;
static int  flush_inhibit       = 0;
static int  autowrap            = 1;

static uint32_t dirty_y_min = 0xFFFFFFFFu;
static uint32_t dirty_y_max = 0;
static int        g_offscreen = 0;
static int        g_need_redraw = 0;
static unsigned long long g_last_flush_ns = 0;
static vt_cell_t *g_grid  = NULL;
static uint32_t   g_gcols = 0;
static uint32_t   g_grows = 0;

extern bool               hpet_is_available(void);
extern unsigned long long hpet_elapsed_ns(void);

#define CONSOLE_FLUSH_INTERVAL_NS 16000000ULL

void console_set_offscreen(int on) { g_offscreen = on; }

int console_cursor_visible(void) { return cursor_visible; }

void console_set_grid(vt_cell_t *grid, uint32_t cols, uint32_t rows) {
    g_grid = grid; g_gcols = cols; g_grows = rows;
    if (!grid) {
        g_need_redraw = 0;
        dirty_y_min = 0xFFFFFFFFu;
        dirty_y_max = 0;
    }
}

static inline void grid_put(uint32_t col, uint32_t row, uint32_t ch, uint32_t fg, uint32_t bg) {
    if (!g_grid || col >= g_gcols || row >= g_grows) return;
    vt_cell_t *c = &g_grid[(size_t)row * g_gcols + col];
    c->ch = ch; c->fg = fg; c->bg = bg;
}

static void grid_clear_cells(uint32_t col, uint32_t row, uint32_t n, uint32_t bg) {
    if (!g_grid || row >= g_grows) return;
    for (uint32_t i = 0; i < n && (col + i) < g_gcols; i++) {
        vt_cell_t *c = &g_grid[(size_t)row * g_gcols + col + i];
        c->ch = ' '; c->fg = text_color; c->bg = bg;
    }
}

void console_redraw_grid(void) {
    if (!global_framebuffer || !g_grid) return;
    for (uint32_t row = 0; row < g_grows; row++) {
        for (uint32_t col = 0; col < g_gcols; col++) {
            vt_cell_t *c = &g_grid[(size_t)row * g_gcols + col];
            uint32_t x = col * 8, y = row * 16;
            fb_fill_rect(global_framebuffer, x, y, 8, 16, c->bg);
            if (c->ch && c->ch != ' ')
                fb_draw_char(global_framebuffer, (char)c->ch, x, y, c->fg);
        }
    }
}

static void mark_dirty(uint32_t y_start, uint32_t h) {
    if (h == 0) return;
    uint32_t end = y_start + h;
    if (y_start < dirty_y_min) dirty_y_min = y_start;
    if (end      > dirty_y_max) dirty_y_max = end;
}

static void do_flush_now(void) {
    if (g_need_redraw) {
        console_redraw_grid();
        g_need_redraw = 0;
        dirty_y_min = 0;
        dirty_y_max = global_framebuffer ? global_framebuffer->height : 0;
    }
    if (dirty_y_min < dirty_y_max)
        fb_flush_lines(global_framebuffer, dirty_y_min, dirty_y_max);
    dirty_y_min = 0xFFFFFFFFu;
    dirty_y_max = 0;
    if (hpet_is_available()) g_last_flush_ns = hpet_elapsed_ns();
}

static void commit_dirty(void) {
    if (!global_framebuffer) return;
    if (g_offscreen) {
        dirty_y_min = 0xFFFFFFFFu; dirty_y_max = 0; g_need_redraw = 0;
        return;
    }
    if (!g_need_redraw && dirty_y_min >= dirty_y_max) return;
    if (!g_need_redraw && hpet_is_available()) {
        unsigned long long now = hpet_elapsed_ns();
        if (now - g_last_flush_ns < CONSOLE_FLUSH_INTERVAL_NS) return;
    }
    do_flush_now();
}

void console_flush_pending(void) {
    if (g_offscreen || !global_framebuffer) return;
    if (!g_need_redraw && dirty_y_min >= dirty_y_max) return;
    do_flush_now();
}

void console_force_full_redraw(void) {
    if (g_offscreen || !global_framebuffer) return;
    console_redraw_grid();
    g_need_redraw = 0;
    dirty_y_min = 0xFFFFFFFFu;
    dirty_y_max = 0;
    fb_flush(global_framebuffer);
    if (hpet_is_available()) g_last_flush_ns = hpet_elapsed_ns();
}

void putchar_flush_begin(void) {
    flush_inhibit++;
}
void putchar_flush_end(void) {
    if (flush_inhibit > 0) flush_inhibit--;
    if (flush_inhibit == 0) commit_dirty();
}

uint32_t get_screen_width(void) {
    if (!global_framebuffer) return 1024;
    return global_framebuffer->width;
}
uint32_t get_screen_height(void) {
    if (!global_framebuffer) return 768;
    return global_framebuffer->height;
}

uint32_t get_cursor_row(void) { return cursor_y / 16; }
uint32_t get_cursor_col(void) { return cursor_x / 8;  }

static void flush_all(void) {
    if (!flush_inhibit && !g_offscreen && global_framebuffer)
        fb_flush(global_framebuffer);
}

static void flush_region(uint32_t y_start, uint32_t h) {
    mark_dirty(y_start, h);
}

void scroll_screen(int lines) {
    if (lines <= 0) return;

    if (g_grid) {
        if ((uint32_t)lines < g_grows) {
            uint32_t mv = g_grows - (uint32_t)lines;
            memmove(g_grid, g_grid + (size_t)lines * g_gcols,
                    (size_t)mv * g_gcols * sizeof(vt_cell_t));
            for (uint32_t r = mv; r < g_grows; r++) grid_clear_cells(0, r, g_gcols, bg_color);
        } else {
            for (uint32_t r = 0; r < g_grows; r++) grid_clear_cells(0, r, g_gcols, bg_color);
        }
        if (!g_offscreen && global_framebuffer) {
            console_redraw_grid();
            mark_dirty(0, global_framebuffer->height);
            g_need_redraw = 0;
        }
        return;
    }

    if (g_offscreen || !global_framebuffer) return;
    uint32_t sh = get_screen_height();
    uint32_t sp = (uint32_t)(lines * 16);
    if (sp >= sh) { fb_clear(global_framebuffer, bg_color); flush_all(); return; }

    uint32_t *buf = (uint32_t *)global_framebuffer->address;
    extern uint32_t *g_backbuf;
    extern uint32_t  g_bb_pitch;
    uint32_t pitch;
    uint32_t *target;
    if (g_backbuf) {
        target = g_backbuf;
        pitch  = g_bb_pitch;
    } else {
        target = buf;
        pitch  = global_framebuffer->pitch / 4;
    }

    uint32_t rows_to_move = sh - sp;
    memmove(target, target + sp * pitch, rows_to_move * pitch * sizeof(uint32_t));
    memset(target + rows_to_move * pitch, 0, sp * pitch * sizeof(uint32_t));

    if (bg_color != 0) {
        uint32_t sw = get_screen_width();
        uint32_t *clear_start = target + rows_to_move * pitch;
        for (uint32_t y = 0; y < sp; y++) {
            uint32_t *row = clear_start + y * pitch;
            for (uint32_t x = 0; x < sw; x++)
                row[x] = bg_color;
        }
    }

    mark_dirty(0, sh);
}

static void cell_cell_at(uint32_t x, uint32_t y, uint32_t *ch, uint32_t *fg, uint32_t *bg) {
    *ch = ' '; *fg = text_color; *bg = bg_color;
    if (!g_grid || g_gcols == 0) return;
    uint32_t col = x / 8, row = y / 16;
    if (col >= g_gcols || row >= g_grows) return;
    vt_cell_t *c = &g_grid[(size_t)row * g_gcols + col];
    *ch = c->ch; *fg = c->fg; *bg = c->bg;
}

static void draw_cursor_at(uint32_t x, uint32_t y) {
    if (g_offscreen) return;
    if (!global_framebuffer || !cursor_visible) return;
    if (x + 8 > global_framebuffer->width || y + 16 > global_framebuffer->height) return;

    uint32_t ch, fg, bg;
    cell_cell_at(x, y, &ch, &fg, &bg);

    if (cursor_shape == 0) {
        fb_fill_rect(global_framebuffer, x, y, 8, 16, text_color);
        if (ch && ch != ' ')
            fb_draw_char(global_framebuffer, (char)ch, x, y, bg);
        fb_flush_lines(global_framebuffer, y, y + 16);
    } else if (cursor_shape == 2) {
        for (uint32_t row = 0; row < 16; row++) {
            fb_draw_pixel(global_framebuffer, x, y + row, text_color);
        }
        fb_flush_lines(global_framebuffer, y, y + 16);
    } else {
        for (uint32_t col = 0; col < 8; col++) {
            fb_draw_pixel(global_framebuffer, x + col, y + 14, text_color);
            fb_draw_pixel(global_framebuffer, x + col, y + 15, text_color);
        }
        fb_flush_lines(global_framebuffer, y + 14, y + 16);
    }
}

static void erase_cursor_at(uint32_t x, uint32_t y) {
    if (g_offscreen) return;
    if (!global_framebuffer) return;
    if (x + 8 > global_framebuffer->width || y + 16 > global_framebuffer->height) return;

    uint32_t ch, fg, bg;
    cell_cell_at(x, y, &ch, &fg, &bg);
    fb_fill_rect(global_framebuffer, x, y, 8, 16, bg);
    if (ch && ch != ' ')
        fb_draw_char(global_framebuffer, (char)ch, x, y, fg);
    fb_flush_lines(global_framebuffer, y, y + 16);
}

void draw_cursor(void)  { draw_cursor_at(cursor_x, cursor_y); }
void erase_cursor(void) { erase_cursor_at(cursor_x, cursor_y); }

static uint32_t ansi_color(int code, int bright) {
    static const uint32_t base[8] = {
        0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
        0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    };
    static const uint32_t bright8[8] = {
        0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
        0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
    };
    return bright ? bright8[code & 7] : base[code & 7];
}

#define ESC_MAX_PARAMS 8

typedef enum {
    PS_NORMAL,
    PS_ESC,
    PS_CSI,
    PS_CSI_PRIV,
    PS_CSI_SP,
    PS_ESC_SP,
} parse_state_t;

static parse_state_t ps_state   = PS_NORMAL;
static int           ps_params[ESC_MAX_PARAMS];
static int           ps_nparams = 0;
static int           ps_cur     = 0;
static int           ps_bold    = 0;

static void ps_reset_params(void) {
    for (int i = 0; i < ESC_MAX_PARAMS; i++) ps_params[i] = -1;
    ps_nparams = 0; ps_cur = 0;
}
static void ps_push_param(void) {
    if (ps_nparams < ESC_MAX_PARAMS) ps_params[ps_nparams++] = ps_cur;
    ps_cur = 0;
}
static int ps_get(int i, int def) {
    if (i >= ps_nparams || ps_params[i] < 0) return def;
    return ps_params[i];
}

static int      ps_reverse = 0;

static void handle_sgr(void) {
    if (ps_nparams == 0) {
        text_color = COLOR_WHITE; bg_color = COLOR_BLACK;
        ps_reverse = 0;
        return;
    }
    for (int i = 0; i < ps_nparams; i++) {
        int p = ps_params[i]; if (p < 0) p = 0;
        if      (p == 0)             { text_color = COLOR_WHITE; bg_color = COLOR_BLACK; ps_bold = 0; ps_reverse = 0; }
        else if (p == 1)             { ps_bold = 1; }
        else if (p == 22)            { ps_bold = 0; }
        else if (p == 7)             {
            if (!ps_reverse) {
                uint32_t tmp = text_color; text_color = bg_color; bg_color = tmp;
                ps_reverse = 1;
            }
        }
        else if (p == 27)            {
            if (ps_reverse) {
                uint32_t tmp = text_color; text_color = bg_color; bg_color = tmp;
                ps_reverse = 0;
            }
        }
        else if ((p == 38 || p == 48) && i + 2 < ps_nparams && ps_params[i + 1] == 2) {
            uint32_t r = (uint32_t)(ps_params[i + 2] & 0xFF);
            uint32_t g = (i + 3 < ps_nparams) ? (uint32_t)(ps_params[i + 3] & 0xFF) : 0;
            uint32_t b = (i + 4 < ps_nparams) ? (uint32_t)(ps_params[i + 4] & 0xFF) : 0;
            uint32_t c = (r << 16) | (g << 8) | b;
            if (p == 38) { if (ps_reverse) bg_color = c; else text_color = c; }
            else         { if (ps_reverse) text_color = c; else bg_color = c; }
            i += 4;
        }
        else if (p >= 30 && p <= 37) {
            uint32_t c = ansi_color(p-30, ps_bold);
            if (ps_reverse) bg_color = c; else text_color = c;
        }
        else if (p >= 90 && p <= 97) {
            uint32_t c = ansi_color(p-90, 1);
            if (ps_reverse) bg_color = c; else text_color = c;
        }
        else if (p >= 40 && p <= 47) {
            uint32_t c = ansi_color(p-40, 0);
            if (ps_reverse) text_color = c; else bg_color = c;
        }
        else if (p >= 100 && p <= 107) {
            uint32_t c = ansi_color(p-100, 1);
            if (ps_reverse) text_color = c; else bg_color = c;
        }
    }
}

static void erase_to_eol(void) {
    uint32_t col = cursor_x / 8, row = cursor_y / 16;
    grid_clear_cells(col, row, (g_gcols > col) ? (g_gcols - col) : 0, bg_color);
    if (g_offscreen || !global_framebuffer) return;
    uint32_t sw = get_screen_width();
    if (cursor_x >= sw) return;
    fb_fill_rect(global_framebuffer, cursor_x, cursor_y, sw - cursor_x, 16, bg_color);
    flush_region(cursor_y, 16);
}

static void cursor_move_right(int n) {
    uint32_t sw = get_screen_width();
    cursor_x += (uint32_t)(n * 8);
    if (cursor_x + 8 > sw) cursor_x = sw - 8;
}
static void cursor_move_left(int n) {
    uint32_t delta = (uint32_t)(n * 8);
    if (cursor_x >= delta) cursor_x -= delta;
    else cursor_x = 0;
}

static uint32_t saved_cx = 0, saved_cy = 0;

static void clear_cell(uint32_t x, uint32_t y) {
    grid_put(x / 8, y / 16, ' ', text_color, bg_color);
    if (g_offscreen || !global_framebuffer) return;
    fb_fill_rect(global_framebuffer, x, y, 8, 16, bg_color);
}

static void draw_and_advance(char c) {
    if (!global_framebuffer) return;
    uint32_t sh = get_screen_height();
    uint32_t sw = get_screen_width();
    grid_put(cursor_x / 8, cursor_y / 16, (uint8_t)c, text_color, bg_color);
    if (!g_offscreen) {
        fb_fill_rect(global_framebuffer, cursor_x, cursor_y, 8, 16, bg_color);
        fb_draw_char(global_framebuffer, c, cursor_x, cursor_y, text_color);
        flush_region(cursor_y, 16);
    }
    if (!autowrap && cursor_x + 8 >= sw) return;
    cursor_x += 8;
    if (cursor_x + 8 > sw) { cursor_x = 0; cursor_y += 16; }
    if (cursor_y + 16 > sh) { scroll_screen(1); cursor_y = sh - 16; }
}

int putchar(int c) {
    if (!global_framebuffer) return EOF;
    uint8_t ch = (uint8_t)c;

    if (ch == 0x1B) { ps_state = PS_ESC; return c; }

    switch (ps_state) {

    case PS_NORMAL:
        switch (ch) {
        case '\n':
            cursor_x = 0; cursor_y += 16;
            if (cursor_y + 16 > get_screen_height()) {
                scroll_screen(1); cursor_y = get_screen_height() - 16;
            }
            if (!flush_inhibit) commit_dirty();
            break;
        case '\r': cursor_x = 0; break;
        case '\t': cursor_x = (cursor_x + 32) & ~31u; break;
        case '\b':
            if (cursor_x >= 8) {
                cursor_x -= 8;
                clear_cell(cursor_x, cursor_y);
                flush_region(cursor_y, 16);
            }
            break;
        default:
            if (ch >= 32 && ch <= 126) draw_and_advance((char)ch);
            break;
        }
        break;

    case PS_ESC:
        if      (ch == '[') { ps_state = PS_CSI; ps_reset_params(); }
        else if (ch == ' ') { ps_state = PS_ESC_SP; }
        else                 { ps_state = PS_NORMAL; }
        break;

    case PS_ESC_SP:
        ps_state = PS_NORMAL;
        break;

    case PS_CSI:
        if (ch == '?') { ps_state = PS_CSI_PRIV; break; }
        if (ch == ' ') { ps_push_param(); ps_state = PS_CSI_SP; break; }
        __attribute__((fallthrough));
    case PS_CSI_PRIV:
        if (ch >= '0' && ch <= '9') {
            ps_cur = ps_cur * 10 + (ch - '0');
        } else if (ch == ';') {
            ps_push_param();
        } else {
            ps_push_param();

            if (ps_state == PS_CSI_PRIV) {
                int p = ps_get(0, 0);
                if (p == 25) {
                    if (ch == 'l') {
                        cursor_visible = 0;
                    } else if (ch == 'h') {
                        cursor_visible = 1;
                    }
                } else if (p == 2026) {
                    if (ch == 'h') {
                        flush_inhibit++;
                    } else if (ch == 'l') {
                        if (flush_inhibit > 0) flush_inhibit--;
                        if (flush_inhibit == 0) commit_dirty();
                    }
                } else if (p == 7) {
                    if      (ch == 'l') autowrap = 0;
                    else if (ch == 'h') autowrap = 1;
                }
                ps_state = PS_NORMAL;
                break;
            }

            switch (ch) {
            case 'm': handle_sgr(); break;
            case 'J': {
                int mode = ps_get(0, 0);
                uint32_t sh = get_screen_height();
                uint32_t sw = get_screen_width();
                uint32_t ccol = cursor_x / 8, crow = cursor_y / 16;
                if (mode == 2 || mode == 3) {
                    for (uint32_t r = 0; r < g_grows; r++) grid_clear_cells(0, r, g_gcols, bg_color);
                    cursor_x = 0; cursor_y = 0;
                    if (!g_offscreen) { fb_clear(global_framebuffer, bg_color); mark_dirty(0, sh); }
                } else if (mode == 0) {
                    grid_clear_cells(ccol, crow, (g_gcols > ccol) ? (g_gcols - ccol) : 0, bg_color);
                    for (uint32_t r = crow + 1; r < g_grows; r++) grid_clear_cells(0, r, g_gcols, bg_color);
                    if (!g_offscreen) {
                        if (cursor_x < sw)
                            fb_fill_rect(global_framebuffer, cursor_x, cursor_y, sw - cursor_x, 16, bg_color);
                        uint32_t y2 = cursor_y + 16;
                        if (y2 < sh)
                            fb_fill_rect(global_framebuffer, 0, y2, sw, sh - y2, bg_color);
                        mark_dirty(cursor_y, sh - cursor_y);
                    }
                } else if (mode == 1) {
                    for (uint32_t r = 0; r < crow; r++) grid_clear_cells(0, r, g_gcols, bg_color);
                    grid_clear_cells(0, crow, ccol + 1, bg_color);
                    if (!g_offscreen) {
                        if (cursor_y > 0)
                            fb_fill_rect(global_framebuffer, 0, 0, sw, cursor_y, bg_color);
                        if (cursor_x > 0)
                            fb_fill_rect(global_framebuffer, 0, cursor_y, cursor_x, 16, bg_color);
                        mark_dirty(0, cursor_y + 16);
                    }
                }
                if (!flush_inhibit) commit_dirty();
                break;
            }
            case 'K': {
                int mode = ps_get(0, 0);
                if (mode == 0) erase_to_eol();
                break;
            }
            case 'H':
            case 'f': {
                int row = ps_get(0, 1); if (row < 1) row = 1;
                int col = ps_get(1, 1); if (col < 1) col = 1;
                uint32_t cx = (uint32_t)((col - 1) * 8);
                uint32_t cy = (uint32_t)((row - 1) * 16);
                uint32_t sw = get_screen_width();
                uint32_t sh = get_screen_height();
                if (cx + 8 > sw) cx = (sw >= 8) ? sw - 8 : 0;
                if (cy + 16 > sh) cy = (sh >= 16) ? sh - 16 : 0;
                cursor_x = cx;
                cursor_y = cy;
                break;
            }
            case 'A': {
                int n = ps_get(0, 1); if (n < 1) n = 1;
                uint32_t d = (uint32_t)(n * 16);
                cursor_y = (cursor_y >= d) ? cursor_y - d : 0;
                break;
            }
            case 'B': {
                int n = ps_get(0, 1); if (n < 1) n = 1;
                cursor_y += (uint32_t)(n * 16);
                break;
            }
            case 'C': cursor_move_right(ps_get(0, 1)); break;
            case 'D': cursor_move_left (ps_get(0, 1)); break;
            case 'E': {
                int n = ps_get(0, 1); if (n < 1) n = 1;
                uint32_t sh = get_screen_height();
                cursor_x = 0;
                cursor_y += (uint32_t)(n * 16);
                if (cursor_y + 16 > sh) cursor_y = (sh >= 16) ? sh - 16 : 0;
                break;
            }
            case 'F': {
                int n = ps_get(0, 1); if (n < 1) n = 1;
                uint32_t d = (uint32_t)(n * 16);
                cursor_x = 0;
                cursor_y = (cursor_y >= d) ? cursor_y - d : 0;
                break;
            }
            case 'G': {
                int col = ps_get(0, 1); if (col < 1) col = 1;
                uint32_t sw = get_screen_width();
                uint32_t cx = (uint32_t)((col - 1) * 8);
                if (cx + 8 > sw) cx = (sw >= 8) ? sw - 8 : 0;
                cursor_x = cx;
                break;
            }
            case 'd': {
                int row = ps_get(0, 1); if (row < 1) row = 1;
                uint32_t sh = get_screen_height();
                uint32_t cy = (uint32_t)((row - 1) * 16);
                if (cy + 16 > sh) cy = (sh >= 16) ? sh - 16 : 0;
                cursor_y = cy;
                break;
            }
            case 's': saved_cx = cursor_x; saved_cy = cursor_y; break;
            case 'u': cursor_x = saved_cx; cursor_y = saved_cy; break;
            default: break;
            }
            ps_state = PS_NORMAL;
        }
        break;

    case PS_CSI_SP:
        if (ch == 'q') {
            int p = ps_get(0, 1);
            if      (p == 0 || p == 1 || p == 2) cursor_shape = 0;
            else if (p == 3 || p == 4)           cursor_shape = 1;
            else if (p == 5 || p == 6)           cursor_shape = 2;
        }
        ps_state = PS_NORMAL;
        break;
    }
    return c;
}

void clear_screen_with_scroll(void) {
    for (uint32_t r = 0; r < g_grows; r++) grid_clear_cells(0, r, g_gcols, bg_color);
    cursor_x = 0; cursor_y = 0;
    scroll_buffer_index = 0; total_scroll_lines = 0;
    if (!g_offscreen && global_framebuffer) {
        fb_clear(global_framebuffer, bg_color);
        flush_all();
    }
}
void get_cursor_position(uint32_t *x, uint32_t *y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

void console_save_state(console_state_t *s) {
    s->cursor_x = cursor_x; s->cursor_y = cursor_y;
    s->text_color = text_color; s->bg_color = bg_color;
    s->cursor_visible = cursor_visible; s->autowrap = autowrap;
    s->flush_inhibit = flush_inhibit;
    s->scroll_buffer_index = scroll_buffer_index;
    s->total_scroll_lines = total_scroll_lines;
    s->dirty_y_min = dirty_y_min; s->dirty_y_max = dirty_y_max;
    s->ps_state = (int)ps_state; s->ps_nparams = ps_nparams;
    s->ps_cur = ps_cur; s->ps_bold = ps_bold; s->ps_reverse = ps_reverse;
    for (int i = 0; i < CONSOLE_ESC_MAX_PARAMS; i++) s->ps_params[i] = ps_params[i];
    s->saved_cx = saved_cx; s->saved_cy = saved_cy;
}

void console_load_state(const console_state_t *s) {
    cursor_x = s->cursor_x; cursor_y = s->cursor_y;
    text_color = s->text_color; bg_color = s->bg_color;
    cursor_visible = s->cursor_visible; autowrap = s->autowrap;
    flush_inhibit = s->flush_inhibit;
    scroll_buffer_index = s->scroll_buffer_index;
    total_scroll_lines = s->total_scroll_lines;
    dirty_y_min = s->dirty_y_min; dirty_y_max = s->dirty_y_max;
    ps_state = (parse_state_t)s->ps_state; ps_nparams = s->ps_nparams;
    ps_cur = s->ps_cur; ps_bold = s->ps_bold; ps_reverse = s->ps_reverse;
    for (int i = 0; i < CONSOLE_ESC_MAX_PARAMS; i++) ps_params[i] = s->ps_params[i];
    saved_cx = s->saved_cx; saved_cy = s->saved_cy;
}

void console_reset_state(void) {
    cursor_x = 0; cursor_y = 0;
    text_color = COLOR_WHITE; bg_color = COLOR_BLACK;
    cursor_visible = 1; autowrap = 1; flush_inhibit = 0;
    scroll_buffer_index = 0; total_scroll_lines = 0;
    dirty_y_min = 0xFFFFFFFFu; dirty_y_max = 0;
    ps_state = PS_NORMAL; ps_nparams = 0; ps_cur = 0; ps_bold = 0; ps_reverse = 0;
    for (int i = 0; i < ESC_MAX_PARAMS; i++) ps_params[i] = -1;
    saved_cx = 0; saved_cy = 0;
}

void console_reset_attrs(void) {
    text_color = COLOR_WHITE;
    bg_color = COLOR_BLACK;
    cursor_visible = 1;
    autowrap = 1;
    ps_state = PS_NORMAL;
    ps_nparams = 0;
    ps_cur = 0;
    ps_bold = 0;
    ps_reverse = 0;
}
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "../../../kernel/include/graphics/fb/fb.h"
#include "../../../kernel/include/console/console.h"

uint32_t cursor_x   = 0;
uint32_t cursor_y   = 0;

static uint32_t theme_pal[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};
static uint32_t theme_fg = COLOR_WHITE;
static uint32_t theme_bg = COLOR_BLACK;

uint32_t text_color = COLOR_WHITE;
uint32_t bg_color   = COLOR_BLACK;

uint32_t console_theme_fg(void) { return theme_fg; }
uint32_t console_theme_bg(void) { return theme_bg; }

extern fb_info_t *global_framebuffer;

static int  cursor_visible      = 1;
static int  cursor_shape        = 1;
static uint32_t utf8_acc        = 0;
static int      utf8_need       = 0;
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

#define ALT_MAX_CELLS 20480
static vt_cell_t  alt_save[ALT_MAX_CELLS];
static size_t     alt_saved_n = 0;
static int        alt_active  = 0;
static uint32_t   alt_cx = 0, alt_cy = 0, alt_fg = 0, alt_bg = 0;

extern bool               hpet_is_available(void);
extern unsigned long long hpet_elapsed_ns(void);
extern unsigned long long clocksource_now_ns(void);

#define CONSOLE_FLUSH_INTERVAL_NS 8000000ULL

static inline uint32_t CW(void) { return fb_font_width(); }
static inline uint32_t CH(void) { return fb_font_height(); }

static uint32_t scroll_top = 0, scroll_bot = 0xFFFFFFFFu;
static uint32_t sc_fg = 0, sc_bg = 0;
static int      sc_bold = 0, sc_rev = 0, sc_valid = 0;

void console_set_offscreen(int on) { g_offscreen = on; }

int console_cursor_visible(void) { return cursor_visible; }

void console_set_grid(vt_cell_t *grid, uint32_t cols, uint32_t rows) {
    if (cols != g_gcols || rows != g_grows) { scroll_top = 0; scroll_bot = 0xFFFFFFFFu; }
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
            uint32_t x = col * CW(), y = row * CH();
            fb_fill_rect(global_framebuffer, x, y, CW(), CH(), c->bg);
            if (c->ch && c->ch != ' ')
                fb_draw_char(global_framebuffer, c->ch, x, y, c->fg);
        }
    }
    uint32_t used_w = g_gcols * CW(), used_h = g_grows * CH();
    uint32_t fw = global_framebuffer->width, fh = global_framebuffer->height;
    if (used_w < fw) fb_fill_rect(global_framebuffer, used_w, 0, fw - used_w, fh, theme_bg);
    if (used_h < fh) fb_fill_rect(global_framebuffer, 0, used_h, used_w < fw ? used_w : fw, fh - used_h, theme_bg);
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
    g_last_flush_ns = clocksource_now_ns();
}

static void commit_dirty(void) {
    if (!global_framebuffer) return;
    if (g_offscreen) {
        dirty_y_min = 0xFFFFFFFFu; dirty_y_max = 0; g_need_redraw = 0;
        return;
    }
    if (!g_need_redraw && dirty_y_min >= dirty_y_max) return;
    if (!g_need_redraw) {
        unsigned long long now = clocksource_now_ns();
        if (now && now - g_last_flush_ns < CONSOLE_FLUSH_INTERVAL_NS) return;
    }
    do_flush_now();
}

void console_flush_pending(void) {
    if (g_offscreen || !global_framebuffer) return;
    if (!g_need_redraw && dirty_y_min >= dirty_y_max) return;
    unsigned long long now = clocksource_now_ns();
    if (!g_need_redraw && now && now - g_last_flush_ns < CONSOLE_FLUSH_INTERVAL_NS) return;
    do_flush_now();
}

void console_flush_now(void) {
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
    g_last_flush_ns = clocksource_now_ns();
}

static void console_alt_enter(void) {
    if (alt_active) return;
    size_t n = (size_t)g_gcols * g_grows;
    if (n > ALT_MAX_CELLS) n = ALT_MAX_CELLS;
    if (g_grid) memcpy(alt_save, g_grid, n * sizeof(vt_cell_t));
    alt_saved_n = n;
    alt_cx = cursor_x; alt_cy = cursor_y;
    alt_fg = text_color; alt_bg = bg_color;
    alt_active = 1;
    for (uint32_t r = 0; r < g_grows; r++) grid_clear_cells(0, r, g_gcols, bg_color);
    cursor_x = 0; cursor_y = 0;
    if (!g_offscreen && global_framebuffer) {
        fb_clear(global_framebuffer, bg_color);
        mark_dirty(0, get_screen_height());
        g_need_redraw = 0;
        commit_dirty();
    }
}

static void console_alt_exit(void) {
    if (!alt_active) return;
    if (g_grid && alt_saved_n) memcpy(g_grid, alt_save, alt_saved_n * sizeof(vt_cell_t));
    cursor_x = alt_cx; cursor_y = alt_cy;
    text_color = alt_fg; bg_color = alt_bg;
    alt_active = 0;
    if (!g_offscreen && global_framebuffer) {
        console_redraw_grid();
        g_need_redraw = 0;
        mark_dirty(0, get_screen_height());
        do_flush_now();
    }
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
    uint32_t h = CH();
    return (global_framebuffer->height / h) * h;
}

uint32_t get_cursor_row(void) { return cursor_y / CH(); }
uint32_t get_cursor_col(void) { return cursor_x / CW(); }

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
            extern uint32_t *g_backbuf;
            extern uint32_t  g_bb_pitch;
            uint32_t sh = get_screen_height();
            uint32_t sw = get_screen_width();
            uint32_t sp = (uint32_t)lines * CH();
            uint32_t *tgt = g_backbuf ? g_backbuf : (uint32_t *)global_framebuffer->address;
            uint32_t pitch = g_backbuf ? g_bb_pitch : (global_framebuffer->pitch / 4);
            (void)sw;
            if (sp < sh) {
                uint32_t rows_mv = sh - sp;
                memmove(tgt, tgt + (size_t)sp * pitch,
                        (size_t)rows_mv * pitch * sizeof(uint32_t));
                uint32_t *cs = tgt + (size_t)rows_mv * pitch;
                size_t clear_words = (size_t)sp * pitch;
                if (bg_color == 0) {
                    memset(cs, 0, clear_words * sizeof(uint32_t));
                } else {
                    for (size_t i = 0; i < clear_words; i++) cs[i] = bg_color;
                }
            } else {
                fb_clear(global_framebuffer, bg_color);
            }
            mark_dirty(0, sh);
            g_need_redraw = 0;
        }
        return;
    }

    if (g_offscreen || !global_framebuffer) return;
    uint32_t sh = get_screen_height();
    uint32_t sp = (uint32_t)(lines * (int)CH());
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
    uint32_t col = x / CW(), row = y / CH();
    if (col >= g_gcols || row >= g_grows) return;
    vt_cell_t *c = &g_grid[(size_t)row * g_gcols + col];
    *ch = c->ch; *fg = c->fg; *bg = c->bg;
}

static void draw_cursor_at(uint32_t x, uint32_t y) {
    if (g_offscreen) return;
    if (!global_framebuffer || !cursor_visible) return;
    uint32_t cw = CW(), ch_h = CH();
    if (x + cw > global_framebuffer->width || y + ch_h > global_framebuffer->height) return;

    uint32_t ch, fg, bg;
    cell_cell_at(x, y, &ch, &fg, &bg);

    if (cursor_shape == 0) {
        fb_fill_rect(global_framebuffer, x, y, cw, ch_h, text_color);
        if (ch && ch != ' ')
            fb_draw_char(global_framebuffer, ch, x, y, bg);
    } else if (cursor_shape == 2) {
        for (uint32_t row = 0; row < ch_h; row++) {
            fb_draw_pixel(global_framebuffer, x, y + row, text_color);
        }
    } else {
        for (uint32_t col = 0; col < cw; col++) {
            fb_draw_pixel(global_framebuffer, x + col, y + ch_h - 2, text_color);
            fb_draw_pixel(global_framebuffer, x + col, y + ch_h - 1, text_color);
        }
    }
    mark_dirty(y, ch_h);
}

static void erase_cursor_at(uint32_t x, uint32_t y) {
    if (g_offscreen) return;
    if (!global_framebuffer) return;
    uint32_t cw = CW(), ch_h = CH();
    if (x + cw > global_framebuffer->width || y + ch_h > global_framebuffer->height) return;

    uint32_t ch, fg, bg;
    cell_cell_at(x, y, &ch, &fg, &bg);
    fb_fill_rect(global_framebuffer, x, y, cw, ch_h, bg);
    if (ch && ch != ' ')
        fb_draw_char(global_framebuffer, ch, x, y, fg);
    mark_dirty(y, ch_h);
}

void draw_cursor(void)  { draw_cursor_at(cursor_x, cursor_y); }
void erase_cursor(void) { erase_cursor_at(cursor_x, cursor_y); }

static uint32_t ansi_color(int code, int bright) {
    return theme_pal[(bright ? 8 : 0) + (code & 7)];
}

void console_get_theme(uint32_t pal[16], uint32_t *fg, uint32_t *bg) {
    if (pal) for (int i = 0; i < 16; i++) pal[i] = theme_pal[i];
    if (fg) *fg = theme_fg;
    if (bg) *bg = theme_bg;
}

uint32_t console_theme_remap(uint32_t colour, const uint32_t old_pal[16],
                             uint32_t old_fg, uint32_t old_bg) {
    if (colour == old_fg) return theme_fg;
    if (colour == old_bg) return theme_bg;
    if (old_pal)
        for (int i = 0; i < 16; i++)
            if (colour == old_pal[i]) return theme_pal[i];
    return colour;
}

static void alt_remap(const uint32_t old_pal[16], uint32_t old_fg, uint32_t old_bg);

void console_set_theme(const uint32_t pal[16], uint32_t fg, uint32_t bg) {
    uint32_t old_pal[16], old_fg = theme_fg, old_bg = theme_bg;
    for (int i = 0; i < 16; i++) old_pal[i] = theme_pal[i];

    if (pal) for (int i = 0; i < 16; i++) theme_pal[i] = pal[i] & 0xFFFFFF;
    theme_fg = fg & 0xFFFFFF;
    theme_bg = bg & 0xFFFFFF;

    text_color = console_theme_remap(text_color, old_pal, old_fg, old_bg);
    bg_color   = console_theme_remap(bg_color,   old_pal, old_fg, old_bg);
    alt_remap(old_pal, old_fg, old_bg);
}

static void alt_remap(const uint32_t old_pal[16], uint32_t old_fg, uint32_t old_bg) {
    if (!alt_active || alt_saved_n == 0) return;
    for (size_t i = 0; i < alt_saved_n; i++) {
        alt_save[i].fg = console_theme_remap(alt_save[i].fg, old_pal, old_fg, old_bg);
        alt_save[i].bg = console_theme_remap(alt_save[i].bg, old_pal, old_fg, old_bg);
    }
    alt_fg = console_theme_remap(alt_fg, old_pal, old_fg, old_bg);
    alt_bg = console_theme_remap(alt_bg, old_pal, old_fg, old_bg);
}

#define ESC_MAX_PARAMS CONSOLE_ESC_MAX_PARAMS

typedef enum {
    PS_NORMAL,
    PS_ESC,
    PS_ESC_INTER,
    PS_CSI,
    PS_STR,
    PS_STR_ESC,
} parse_state_t;

static parse_state_t ps_state   = PS_NORMAL;
static int           ps_params[ESC_MAX_PARAMS];
static int           ps_nparams = 0;
static int           ps_cur     = -1;
static int           ps_bold    = 0;
static int           ps_priv    = 0;
static int           ps_inter   = 0;
static int           ps_str_bel = 0;

static void ps_reset_params(void) {
    for (int i = 0; i < ESC_MAX_PARAMS; i++) ps_params[i] = -1;
    ps_nparams = 0; ps_cur = -1; ps_priv = 0; ps_inter = 0;
}
static void ps_push_param(void) {
    if (ps_nparams < ESC_MAX_PARAMS) ps_params[ps_nparams++] = ps_cur;
    ps_cur = -1;
}
static int ps_get(int i, int def) {
    if (i >= ps_nparams || ps_params[i] < 0) return def;
    return ps_params[i];
}

static int      ps_reverse = 0;

static uint32_t xterm256_to_rgb(int n) {
    static const uint8_t steps[6] = { 0, 95, 135, 175, 215, 255 };
    if (n < 8)   return ansi_color(n, 0);
    if (n < 16)  return ansi_color(n - 8, 1);
    if (n <= 231) {
        n -= 16;
        return ((uint32_t)steps[(n / 36) % 6] << 16) |
               ((uint32_t)steps[(n / 6) % 6] << 8) |
                (uint32_t)steps[n % 6];
    }
    int v = 8 + (n - 232) * 10;
    if (v > 255) v = 255;
    return ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)v;
}

static void handle_sgr(void) {
    if (ps_nparams == 0) {
        text_color = theme_fg; bg_color = theme_bg;
        ps_reverse = 0;
        return;
    }
    for (int i = 0; i < ps_nparams; i++) {
        int p = ps_params[i]; if (p < 0) p = 0;
        if      (p == 0)             { text_color = theme_fg; bg_color = theme_bg; ps_bold = 0; ps_reverse = 0; }
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
        else if ((p == 38 || p == 48) && i + 2 < ps_nparams && ps_params[i + 1] == 5) {
            uint32_t c = xterm256_to_rgb(ps_params[i + 2] & 0xFF);
            if (p == 38) { if (ps_reverse) bg_color = c; else text_color = c; }
            else         { if (ps_reverse) text_color = c; else bg_color = c; }
            i += 2;
        }
        else if (p == 39) {
            if (ps_reverse) bg_color = theme_fg; else text_color = theme_fg;
        }
        else if (p == 49) {
            if (ps_reverse) text_color = theme_bg; else bg_color = theme_bg;
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

static char g_reply[48];
static int  g_reply_len;

static void reply_ch(char c) { if (g_reply_len < (int)sizeof g_reply) g_reply[g_reply_len++] = c; }
static void reply_str(const char *s) { while (*s) reply_ch(*s++); }
static void reply_num(int v) {
    char t[12]; int n = 0;
    if (v <= 0) { reply_ch('0'); return; }
    while (v > 0 && n < 12) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0) reply_ch(t[--n]);
}

int console_take_reply(char *out, int cap) {
    int n = g_reply_len;
    if (n > cap) n = cap;
    for (int i = 0; i < n; i++) out[i] = g_reply[i];
    g_reply_len = 0;
    return n;
}

static void erase_to_eol(void) {
    uint32_t col = cursor_x / CW(), row = cursor_y / CH();
    grid_clear_cells(col, row, (g_gcols > col) ? (g_gcols - col) : 0, bg_color);
    if (g_offscreen || !global_framebuffer) return;
    uint32_t sw = get_screen_width();
    if (cursor_x >= sw) return;
    fb_fill_rect(global_framebuffer, cursor_x, cursor_y, sw - cursor_x, CH(), bg_color);
    flush_region(cursor_y, CH());
}

static void cursor_move_right(int n) {
    uint32_t sw = get_screen_width(), cw = CW();
    cursor_x += (uint32_t)n * cw;
    if (cursor_x + cw > sw) cursor_x = (sw >= cw) ? sw - cw : 0;
}
static void cursor_move_left(int n) {
    uint32_t delta = (uint32_t)n * CW();
    if (cursor_x >= delta) cursor_x -= delta;
    else cursor_x = 0;
}

static uint32_t saved_cx = 0, saved_cy = 0;



static uint32_t rows_total(void) {
    if (g_grows) return g_grows;
    uint32_t h = CH();
    return h ? get_screen_height() / h : 1;
}

static uint32_t reg_top(void) {
    return scroll_top < rows_total() ? scroll_top : 0;
}

static uint32_t reg_bot(void) {
    uint32_t n = rows_total();
    if (scroll_bot < n && scroll_bot > reg_top()) return scroll_bot;
    return n ? n - 1 : 0;
}

static void render_rows(uint32_t r0, uint32_t r1) {
    if (g_offscreen || !global_framebuffer || !g_grid) return;
    if (r1 > g_grows) r1 = g_grows;
    if (r0 >= r1) return;
    uint32_t cw = CW(), chh = CH();
    for (uint32_t row = r0; row < r1; row++) {
        for (uint32_t col = 0; col < g_gcols; col++) {
            vt_cell_t *c = &g_grid[(size_t)row * g_gcols + col];
            uint32_t x = col * cw, y = row * chh;
            fb_fill_rect(global_framebuffer, x, y, cw, chh, c->bg);
            if (c->ch && c->ch != ' ') fb_draw_char(global_framebuffer, c->ch, x, y, c->fg);
        }
    }
    mark_dirty(r0 * chh, (r1 - r0) * chh);
}

static void region_up(uint32_t top, uint32_t bot, uint32_t n) {
    if (top > bot || n == 0) return;
    uint32_t h = bot - top + 1;
    if (n > h) n = h;
    if (top == 0 && bot + 1 >= rows_total()) { scroll_screen((int)n); return; }
    if (!g_grid) return;
    if (n < h)
        memmove(&g_grid[(size_t)top * g_gcols], &g_grid[(size_t)(top + n) * g_gcols],
                (size_t)(h - n) * g_gcols * sizeof(vt_cell_t));
    for (uint32_t r = bot + 1 - n; r <= bot; r++) grid_clear_cells(0, r, g_gcols, bg_color);
    render_rows(top, bot + 1);
}

static void region_down(uint32_t top, uint32_t bot, uint32_t n) {
    if (top > bot || n == 0 || !g_grid) return;
    uint32_t h = bot - top + 1;
    if (n > h) n = h;
    if (n < h)
        memmove(&g_grid[(size_t)(top + n) * g_gcols], &g_grid[(size_t)top * g_gcols],
                (size_t)(h - n) * g_gcols * sizeof(vt_cell_t));
    for (uint32_t r = top; r < top + n; r++) grid_clear_cells(0, r, g_gcols, bg_color);
    render_rows(top, bot + 1);
}

static void line_feed(void) {
    uint32_t chh = CH();
    uint32_t row = cursor_y / chh;
    if (row == reg_bot()) region_up(reg_top(), reg_bot(), 1);
    else if (row + 1 < rows_total()) cursor_y += chh;
}

static void reverse_index(void) {
    uint32_t chh = CH();
    uint32_t row = cursor_y / chh;
    if (row == reg_top()) region_down(reg_top(), reg_bot(), 1);
    else if (row > 0) cursor_y -= chh;
}

static void row_shift(int n, int insert) {
    uint32_t col = cursor_x / CW(), row = cursor_y / CH();
    if (!g_grid || row >= g_grows || col >= g_gcols || n <= 0) return;
    vt_cell_t *r = &g_grid[(size_t)row * g_gcols];
    uint32_t cnt = g_gcols - col;
    if ((uint32_t)n > cnt) n = (int)cnt;
    if (insert) {
        memmove(&r[col + n], &r[col], (size_t)(cnt - n) * sizeof(vt_cell_t));
        grid_clear_cells(col, row, (uint32_t)n, bg_color);
    } else {
        memmove(&r[col], &r[col + n], (size_t)(cnt - n) * sizeof(vt_cell_t));
        grid_clear_cells(g_gcols - (uint32_t)n, row, (uint32_t)n, bg_color);
    }
    render_rows(row, row + 1);
}

static void erase_line_part(int mode) {
    uint32_t col = cursor_x / CW(), row = cursor_y / CH();
    if (mode == 0) { erase_to_eol(); return; }
    if (mode == 1) grid_clear_cells(0, row, col + 1, bg_color);
    else           grid_clear_cells(0, row, g_gcols, bg_color);
    render_rows(row, row + 1);
}

static void cursor_save(void) {
    saved_cx = cursor_x; saved_cy = cursor_y;
    sc_fg = text_color; sc_bg = bg_color; sc_bold = ps_bold; sc_rev = ps_reverse; sc_valid = 1;
}

static void cursor_restore(void) {
    cursor_x = saved_cx; cursor_y = saved_cy;
    if (sc_valid) { text_color = sc_fg; bg_color = sc_bg; ps_bold = sc_bold; ps_reverse = sc_rev; }
}

static void set_private_mode(int p, int on) {
    if (p == 25) cursor_visible = on;
    else if (p == 7) autowrap = on;
    else if (p == 2026) {
        if (on) flush_inhibit++;
        else {
            if (flush_inhibit > 0) flush_inhibit--;
            if (flush_inhibit == 0) commit_dirty();
        }
    } else if (p == 1049 || p == 1047 || p == 47) {
        if (on) console_alt_enter();
        else    console_alt_exit();
    }
}

static void full_reset(void) {
    text_color = theme_fg; bg_color = theme_bg;
    ps_bold = 0; ps_reverse = 0; autowrap = 1; cursor_visible = 1;
    scroll_top = 0; scroll_bot = 0xFFFFFFFFu; sc_valid = 0;
    for (uint32_t r = 0; r < g_grows; r++) grid_clear_cells(0, r, g_gcols, bg_color);
    cursor_x = 0; cursor_y = 0;
    if (!g_offscreen && global_framebuffer) {
        fb_clear(global_framebuffer, bg_color);
        mark_dirty(0, get_screen_height());
    }
}

static void draw_and_advance(uint32_t cp) {
    if (!global_framebuffer) return;
    uint32_t sh = get_screen_height();
    uint32_t sw = get_screen_width();
    uint32_t cw = CW(), chh = CH();
    uint32_t gcol = cursor_x / cw, grow = cursor_y / chh;
    int unchanged = 0;
    if (g_grid && gcol < g_gcols && grow < g_grows) {
        vt_cell_t *cc = &g_grid[(size_t)grow * g_gcols + gcol];
        unchanged = (cc->ch == cp && cc->fg == text_color && cc->bg == bg_color);
    }
    grid_put(gcol, grow, cp, text_color, bg_color);
    if (!g_offscreen && !unchanged) {
        fb_fill_rect(global_framebuffer, cursor_x, cursor_y, cw, chh, bg_color);
        fb_draw_char(global_framebuffer, cp, cursor_x, cursor_y, text_color);
        flush_region(cursor_y, chh);
    }
    (void)sh;
    if (!autowrap && cursor_x + cw >= sw) return;
    cursor_x += cw;
    if (cursor_x + cw > sw) { cursor_x = 0; line_feed(); }
}

static void csi_dispatch(uint8_t ch) {
    if (ps_inter) {
        if (ps_inter == ' ' && ch == 'q') {
            int p = ps_get(0, 1);
            if      (p == 0 || p == 1 || p == 2) cursor_shape = 0;
            else if (p == 3 || p == 4)           cursor_shape = 1;
            else if (p == 5 || p == 6)           cursor_shape = 2;
        }
        return;
    }
    if (ps_priv == '?') {
        if (ch == 'h' || ch == 'l')
            for (int i = 0; i < ps_nparams; i++)
                if (ps_params[i] >= 0) set_private_mode(ps_params[i], ch == 'h');
        return;
    }
    if (ps_priv == '>') {
        if (ch == 'c') reply_str("\x1b[>0;0;0c");
        return;
    }
    if (ps_priv) return;

    uint32_t sh = get_screen_height(), sw = get_screen_width();
    uint32_t cw = CW(), chh = CH();
    int n1 = ps_get(0, 1); if (n1 < 1) n1 = 1;

    switch (ch) {
    case 'm': handle_sgr(); break;
    case 'J': {
        int mode = ps_get(0, 0);
        uint32_t ccol = cursor_x / cw, crow = cursor_y / chh;
        if (mode == 2 || mode == 3) {
            for (uint32_t r = 0; r < g_grows; r++) grid_clear_cells(0, r, g_gcols, bg_color);
            cursor_x = 0; cursor_y = 0;
            if (!g_offscreen) { fb_clear(global_framebuffer, bg_color); mark_dirty(0, sh); }
        } else if (mode == 0) {
            grid_clear_cells(ccol, crow, (g_gcols > ccol) ? (g_gcols - ccol) : 0, bg_color);
            for (uint32_t r = crow + 1; r < g_grows; r++) grid_clear_cells(0, r, g_gcols, bg_color);
            if (!g_offscreen) {
                if (cursor_x < sw)
                    fb_fill_rect(global_framebuffer, cursor_x, cursor_y, sw - cursor_x, chh, bg_color);
                uint32_t y2 = cursor_y + chh;
                if (y2 < sh) fb_fill_rect(global_framebuffer, 0, y2, sw, sh - y2, bg_color);
                mark_dirty(cursor_y, sh - cursor_y);
            }
        } else if (mode == 1) {
            for (uint32_t r = 0; r < crow; r++) grid_clear_cells(0, r, g_gcols, bg_color);
            grid_clear_cells(0, crow, ccol + 1, bg_color);
            render_rows(0, crow + 1);
        }
        if (!flush_inhibit) commit_dirty();
        break;
    }
    case 'K': erase_line_part(ps_get(0, 0)); break;
    case 'H':
    case 'f': {
        int row = ps_get(0, 1); if (row < 1) row = 1;
        int col = ps_get(1, 1); if (col < 1) col = 1;
        uint32_t cx = (uint32_t)(col - 1) * cw;
        uint32_t cy = (uint32_t)(row - 1) * chh;
        if (cx + cw > sw) cx = (sw >= cw) ? sw - cw : 0;
        if (cy + chh > sh) cy = (sh >= chh) ? sh - chh : 0;
        cursor_x = cx;
        cursor_y = cy;
        break;
    }
    case 'A': {
        uint32_t d = (uint32_t)n1 * chh;
        cursor_y = (cursor_y >= d) ? cursor_y - d : 0;
        break;
    }
    case 'B': {
        cursor_y += (uint32_t)n1 * chh;
        if (cursor_y + chh > sh) cursor_y = (sh >= chh) ? sh - chh : 0;
        break;
    }
    case 'C': cursor_move_right(n1); break;
    case 'D': cursor_move_left(n1); break;
    case 'E': {
        cursor_x = 0;
        cursor_y += (uint32_t)n1 * chh;
        if (cursor_y + chh > sh) cursor_y = (sh >= chh) ? sh - chh : 0;
        break;
    }
    case 'F': {
        uint32_t d = (uint32_t)n1 * chh;
        cursor_x = 0;
        cursor_y = (cursor_y >= d) ? cursor_y - d : 0;
        break;
    }
    case 'G':
    case '`': {
        uint32_t cx = (uint32_t)(n1 - 1) * cw;
        if (cx + cw > sw) cx = (sw >= cw) ? sw - cw : 0;
        cursor_x = cx;
        break;
    }
    case 'd': {
        uint32_t cy = (uint32_t)(n1 - 1) * chh;
        if (cy + chh > sh) cy = (sh >= chh) ? sh - chh : 0;
        cursor_y = cy;
        break;
    }
    case 'r': {
        uint32_t n = rows_total();
        int top = ps_get(0, 1), bot = ps_get(1, (int)n);
        if (top < 1) top = 1;
        if (bot < 1 || (uint32_t)bot > n) bot = (int)n;
        if (top < bot) {
            scroll_top = (uint32_t)(top - 1);
            scroll_bot = (uint32_t)(bot - 1);
            cursor_x = 0; cursor_y = 0;
        }
        break;
    }
    case 'L': {
        uint32_t row = cursor_y / chh;
        if (row >= reg_top() && row <= reg_bot()) { region_down(row, reg_bot(), (uint32_t)n1); cursor_x = 0; }
        break;
    }
    case 'M': {
        uint32_t row = cursor_y / chh;
        if (row >= reg_top() && row <= reg_bot()) { region_up(row, reg_bot(), (uint32_t)n1); cursor_x = 0; }
        break;
    }
    case '@': row_shift(n1, 1); break;
    case 'P': row_shift(n1, 0); break;
    case 'X': {
        uint32_t col = cursor_x / cw, row = cursor_y / chh;
        grid_clear_cells(col, row, (uint32_t)n1, bg_color);
        render_rows(row, row + 1);
        break;
    }
    case 'S': region_up(reg_top(), reg_bot(), (uint32_t)n1); break;
    case 'T': region_down(reg_top(), reg_bot(), (uint32_t)n1); break;
    case 's': cursor_save(); break;
    case 'u': cursor_restore(); break;
    case 'n': {
        int p = ps_get(0, 0);
        if (p == 6) {
            reply_ch(0x1b); reply_ch('[');
            reply_num((int)(cursor_y / chh) + 1); reply_ch(';');
            reply_num((int)(cursor_x / cw) + 1); reply_ch('R');
        } else if (p == 5) {
            reply_str("\x1b[0n");
        }
        break;
    }
    case 'c':
        reply_str("\x1b[?1;2c");
        break;
    default: break;
    }
}

static void esc_dispatch(uint8_t ch) {
    switch (ch) {
    case '[': ps_state = PS_CSI; ps_reset_params(); return;
    case ']': ps_state = PS_STR; ps_str_bel = 1; return;
    case 'P': case 'X': case '^': case '_':
        ps_state = PS_STR; ps_str_bel = 0; return;
    case '7': cursor_save(); break;
    case '8': cursor_restore(); break;
    case 'D': line_feed(); break;
    case 'E': cursor_x = 0; line_feed(); break;
    case 'M': reverse_index(); break;
    case 'c': full_reset(); break;
    default:
        if (ch >= 0x20 && ch <= 0x2F) { ps_state = PS_ESC_INTER; return; }
        break;
    }
    ps_state = PS_NORMAL;
}

int putchar(int c) {
    if (!global_framebuffer) return EOF;
    uint8_t ch = (uint8_t)c;

    if (ps_state == PS_STR) {
        if (ch == 0x1B) ps_state = PS_STR_ESC;
        else if (ch == 0x07 || ch == 0x9C) ps_state = PS_NORMAL;
        return c;
    }
    if (ps_state == PS_STR_ESC) {
        if (ch == '\\') { ps_state = PS_NORMAL; return c; }
        if (ch == 0x1B) return c;
        ps_state = PS_ESC;
    }

    if (ch == 0x1B) { ps_state = PS_ESC; return c; }
    if (ch == 0x18 || ch == 0x1A) { ps_state = PS_NORMAL; return c; }

    switch (ps_state) {
    case PS_NORMAL:
        switch (ch) {
        case '\n':
        case '\v':
        case '\f':
            cursor_x = 0;
            line_feed();
            break;
        case '\r': cursor_x = 0; break;
        case '\t': {
            uint32_t cw = CW(), col = cursor_x / cw;
            uint32_t nx = ((col & ~3u) + 4u) * cw;
            uint32_t sw = get_screen_width();
            cursor_x = (nx + cw <= sw) ? nx : ((sw >= cw) ? sw - cw : 0);
            break;
        }
        case '\b':
            if (cursor_x >= CW()) cursor_x -= CW();
            break;
        case 0x07: case 0x0E: case 0x0F: case 0x00:
            break;
        default:
            if (ch >= 32 && ch <= 126) {
                utf8_need = 0;
                draw_and_advance(ch);
            } else if (utf8_need > 0 && (ch & 0xC0) == 0x80) {
                utf8_acc = (utf8_acc << 6) | (ch & 0x3F);
                if (--utf8_need == 0) draw_and_advance(utf8_acc);
            } else if ((ch & 0xE0) == 0xC0) {
                utf8_acc = ch & 0x1F; utf8_need = 1;
            } else if ((ch & 0xF0) == 0xE0) {
                utf8_acc = ch & 0x0F; utf8_need = 2;
            } else if ((ch & 0xF8) == 0xF0) {
                utf8_acc = ch & 0x07; utf8_need = 3;
            } else {
                utf8_need = 0;
            }
            break;
        }
        break;

    case PS_ESC:
        esc_dispatch(ch);
        break;

    case PS_ESC_INTER:
        if (ch >= 0x30 && ch <= 0x7E) ps_state = PS_NORMAL;
        break;

    case PS_CSI:
        if (ch >= '0' && ch <= '9') {
            if (ps_cur < 0) ps_cur = 0;
            if (ps_cur < 100000) ps_cur = ps_cur * 10 + (ch - '0');
        } else if (ch == ';' || ch == ':') {
            ps_push_param();
        } else if (ch >= 0x3C && ch <= 0x3F) {
            if (ps_nparams == 0 && ps_cur < 0 && !ps_priv) ps_priv = ch;
        } else if (ch >= 0x20 && ch <= 0x2F) {
            ps_inter = ch;
        } else if (ch >= 0x40 && ch <= 0x7E) {
            if (ps_cur >= 0 || ps_nparams > 0) ps_push_param();
            ps_state = PS_NORMAL;
            csi_dispatch(ch);
        }
        break;

    default:
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
    s->ps_priv = ps_priv; s->ps_inter = ps_inter; s->ps_str_bel = ps_str_bel;
    s->scroll_top = scroll_top; s->scroll_bot = scroll_bot;
    s->sc_fg = sc_fg; s->sc_bg = sc_bg; s->sc_bold = sc_bold; s->sc_rev = sc_rev; s->sc_valid = sc_valid;
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
    ps_priv = s->ps_priv; ps_inter = s->ps_inter; ps_str_bel = s->ps_str_bel;
    scroll_top = s->scroll_top; scroll_bot = s->scroll_bot;
    sc_fg = s->sc_fg; sc_bg = s->sc_bg; sc_bold = s->sc_bold; sc_rev = s->sc_rev; sc_valid = s->sc_valid;
    for (int i = 0; i < CONSOLE_ESC_MAX_PARAMS; i++) ps_params[i] = s->ps_params[i];
    saved_cx = s->saved_cx; saved_cy = s->saved_cy;
}

void console_reset_state(void) {
    cursor_x = 0; cursor_y = 0;
    text_color = theme_fg; bg_color = theme_bg;
    cursor_visible = 1; autowrap = 1; flush_inhibit = 0;
    scroll_buffer_index = 0; total_scroll_lines = 0;
    dirty_y_min = 0xFFFFFFFFu; dirty_y_max = 0;
    ps_state = PS_NORMAL; ps_nparams = 0; ps_cur = -1; ps_bold = 0; ps_reverse = 0;
    ps_priv = 0; ps_inter = 0; ps_str_bel = 0;
    for (int i = 0; i < ESC_MAX_PARAMS; i++) ps_params[i] = -1;
    saved_cx = 0; saved_cy = 0;
    scroll_top = 0; scroll_bot = 0xFFFFFFFFu; sc_valid = 0;
}

void console_reset_attrs(void) {
    text_color = theme_fg;
    bg_color = theme_bg;
    cursor_visible = 1;
    autowrap = 1;
    ps_state = PS_NORMAL;
    ps_nparams = 0;
    ps_cur = -1;
    ps_bold = 0;
    ps_reverse = 0;
    scroll_top = 0;
    scroll_bot = 0xFFFFFFFFu;
}
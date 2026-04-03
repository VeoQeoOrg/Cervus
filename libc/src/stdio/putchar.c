#include <stdio.h>
#include <string.h>
#include "../../../kernel/include/graphics/fb/fb.h"

uint32_t cursor_x   = 0;
uint32_t cursor_y   = 0;
uint32_t text_color = COLOR_WHITE;
uint32_t bg_color   = COLOR_BLACK;

extern struct limine_framebuffer *global_framebuffer;

static int  cursor_visible  = 1;
static int  scroll_buffer_index = 0;
static int  total_scroll_lines  = 0;
static int  flush_inhibit = 0;

uint32_t get_screen_width(void) {
    if (!global_framebuffer) return 1024;
    return global_framebuffer->width;
}
uint32_t get_screen_height(void) {
    if (!global_framebuffer) return 768;
    return global_framebuffer->height;
}

static void flush_all(void) {
    if (!flush_inhibit && global_framebuffer)
        fb_flush(global_framebuffer);
}

static void flush_region(uint32_t y_start, uint32_t h) {
    if (!flush_inhibit && global_framebuffer)
        fb_flush_lines(global_framebuffer, y_start, y_start + h);
}

void scroll_screen(int lines) {
    if (!global_framebuffer || lines <= 0) return;
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
        pitch = g_bb_pitch;
    } else {
        target = buf;
        pitch = global_framebuffer->pitch / 4;
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

    flush_all();
}

static void draw_cursor_at(uint32_t x, uint32_t y) {
    if (!global_framebuffer || !cursor_visible) return;
    if (x + 8 > global_framebuffer->width || y + 16 > global_framebuffer->height) return;
    for (uint32_t col = 0; col < 8; col++) {
        fb_draw_pixel(global_framebuffer, x + col, y + 14, text_color);
        fb_draw_pixel(global_framebuffer, x + col, y + 15, text_color);
    }
    flush_region(y + 14, 2);
}

static void erase_cursor_at(uint32_t x, uint32_t y) {
    if (!global_framebuffer) return;
    if (x + 8 > global_framebuffer->width || y + 16 > global_framebuffer->height) return;
    for (uint32_t col = 0; col < 8; col++) {
        fb_draw_pixel(global_framebuffer, x + col, y + 14, bg_color);
        fb_draw_pixel(global_framebuffer, x + col, y + 15, bg_color);
    }
    flush_region(y + 14, 2);
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

static void handle_sgr(void) {
    if (ps_nparams == 0) { text_color = COLOR_WHITE; ps_bold = 0; return; }
    for (int i = 0; i < ps_nparams; i++) {
        int p = ps_params[i]; if (p < 0) p = 0;
        if      (p == 0)             { text_color = COLOR_WHITE; ps_bold = 0; }
        else if (p == 1)             { ps_bold = 1; }
        else if (p == 22)            { ps_bold = 0; }
        else if (p >= 30 && p <= 37) { text_color = ansi_color(p-30, ps_bold); }
        else if (p >= 90 && p <= 97) { text_color = ansi_color(p-90, 1); }
    }
}

static void erase_to_eol(void) {
    if (!global_framebuffer) return;
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
    if (!global_framebuffer) return;
    fb_fill_rect(global_framebuffer, x, y, 8, 16, bg_color);
}

static void draw_and_advance(char c) {
    if (!global_framebuffer) return;
    uint32_t sh = get_screen_height();
    uint32_t sw = get_screen_width();
    clear_cell(cursor_x, cursor_y);
    fb_draw_char(global_framebuffer, c, cursor_x, cursor_y, text_color);
    flush_region(cursor_y, 16);
    cursor_x += 8;
    if (cursor_x + 8 > sw) { cursor_x = 0; cursor_y += 16; }
    if (cursor_y + 16 > sh) { scroll_screen(1); cursor_y = sh - 16; }
}

int putchar(int c) {
    if (!global_framebuffer) return EOF;
    uint8_t ch = (uint8_t)c;

    switch (ps_state) {

    case PS_NORMAL:
        if (ch == 0x1B) { ps_state = PS_ESC; return c; }
        switch (ch) {
        case '\n':
            cursor_x = 0; cursor_y += 16;
            if (cursor_y + 16 > get_screen_height()) {
                scroll_screen(1); cursor_y = get_screen_height() - 16;
            }
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
        else                { ps_state = PS_NORMAL; }
        break;

    case PS_ESC_SP:
        ps_state = PS_NORMAL;
        break;

    case PS_CSI:
        if (ch == '?') { ps_state = PS_CSI_PRIV; break; }
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
                if (p == 25) cursor_visible = (ch == 'h') ? 1 : 0;
                ps_state = PS_NORMAL;
                break;
            }

            switch (ch) {
            case 'm': handle_sgr(); break;
            case 'J': {
                int mode = ps_get(0, 0);
                if (mode == 2 || mode == 3) {
                    fb_clear(global_framebuffer, bg_color);
                    cursor_x = 0; cursor_y = 0;
                    flush_all();
                }
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
                cursor_x = (uint32_t)((col - 1) * 8);
                cursor_y = (uint32_t)((row - 1) * 16);
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
            case 's': saved_cx = cursor_x; saved_cy = cursor_y; break;
            case 'u': cursor_x = saved_cx; cursor_y = saved_cy; break;
            default: break;
            }
            ps_state = PS_NORMAL;
        }
        break;
    }
    return c;
}

void clear_screen_with_scroll(void) {
    if (global_framebuffer) {
        fb_clear(global_framebuffer, bg_color);
        cursor_x = 0; cursor_y = 0;
        scroll_buffer_index = 0; total_scroll_lines = 0;
        flush_all();
    }
}
void get_cursor_position(uint32_t *x, uint32_t *y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

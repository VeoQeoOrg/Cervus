#include "../../include/console/console.h"
#include "../../include/console/klog.h"
#include "../../include/graphics/fb/fb.h"
#include "../../include/apic/apic.h"
#include <string.h>
#include <stdio.h>
#include <limine.h>

extern struct limine_framebuffer *global_framebuffer;

#define MON_FG        0xAAAAAA
#define MON_BG        0x000000
#define MON_STATUS_FG 0x000000
#define MON_STATUS_BG 0x00AAAA

enum { MON_LIVE, MON_PAUSED, MON_SEARCH };

static int      g_mode = MON_LIVE;
static uint64_t g_top;
static uint64_t g_cursor;
static char     g_query[96];
static int      g_qlen;
static char     g_last[96];

static volatile int g_boot_echo = 1;
static uint64_t     g_shown;
static uint32_t     g_boot_row;
static volatile int g_dirty;

static int g_in_esc;
static int g_in_csi;
static char g_csi[8];
static int g_csi_len;

static uint32_t mon_cols(void) { return global_framebuffer ? global_framebuffer->width  / 8  : 80; }
static uint32_t mon_rows(void) { return global_framebuffer ? global_framebuffer->height / 16 : 25; }

static void mon_draw_line(uint32_t row, const char *s, uint32_t fg, uint32_t bg) {
    if (!global_framebuffer) return;
    uint32_t y = row * 16;
    fb_fill_rect(global_framebuffer, 0, y, global_framebuffer->width, 16, bg);
    uint32_t cols = mon_cols();
    uint32_t x = 0;
    for (uint32_t i = 0; i < cols && s[i]; i++) {
        fb_draw_char(global_framebuffer, s[i], x, y, fg);
        x += 8;
    }
}

#define MON_CUR_FG 0x000000
#define MON_CUR_BG 0x8899AA
#define MON_HIT_FG 0x000000
#define MON_HIT_BG 0xD0B000

static void mon_draw_hl(uint32_t row, const char *s, int is_cursor) {
    if (!global_framebuffer) return;
    uint32_t y = row * 16;
    uint32_t base_fg = is_cursor ? MON_CUR_FG : MON_FG;
    uint32_t base_bg = is_cursor ? MON_CUR_BG : MON_BG;
    fb_fill_rect(global_framebuffer, 0, y, global_framebuffer->width, 16, base_bg);
    uint32_t cols = mon_cols();
    int qlen = (int)strlen(g_last);
    char hit[512];
    int slen = 0;
    while (s[slen] && slen < (int)sizeof(hit)) { hit[slen] = 0; slen++; }
    if (qlen > 0 && slen >= qlen) {
        for (int k = 0; k + qlen <= slen; k++) {
            if (strncmp(s + k, g_last, qlen) == 0)
                for (int m = 0; m < qlen; m++) hit[k + m] = 1;
        }
    }
    for (uint32_t i = 0; i < cols && s[i]; i++) {
        uint32_t fg = base_fg, bg = base_bg;
        if (i < (uint32_t)sizeof(hit) && hit[i]) { fg = MON_HIT_FG; bg = MON_HIT_BG; }
        if (bg != base_bg) fb_fill_rect(global_framebuffer, i * 8, y, 8, 16, bg);
        fb_draw_char(global_framebuffer, s[i], i * 8, y, fg);
    }
}

static void mon_build_status(char *out, size_t n) {
    if (g_mode == MON_SEARCH) {
        size_t p = 0;
        out[p++] = '/';
        for (int i = 0; i < g_qlen && p < n - 1; i++) out[p++] = g_query[i];
        out[p] = 0;
        return;
    }
    const char *pre = (g_mode == MON_LIVE)
        ? "  [debug monitor] LIVE   space/PgDn:page  arrows:scroll  /:find  n:next"
        : "  [debug monitor] PAUSED   space/PgDn:page  arrows:scroll  /:find  n:next  G:live";
    size_t p = 0;
    for (const char *q = pre; *q && p < n - 1; q++) out[p++] = *q;
    out[p] = 0;
}

static void mon_render(int show_status) {
    if (!global_framebuffer) return;
    uint32_t rows = mon_rows();
    uint32_t content = show_status ? (rows - 1) : rows;
    uint64_t total = klog_total();
    uint64_t first = klog_first();

    int paused = (show_status && g_mode != MON_LIVE);
    if (paused) {
        if (g_cursor > total) g_cursor = total;
        if (g_cursor < first) g_cursor = first;
        if (g_cursor < g_top) g_top = g_cursor;
        if (g_cursor >= g_top + content) g_top = g_cursor - content + 1;
        if (g_top < first) g_top = first;
    } else {
        g_top = (total + 1 > content) ? (total + 1 - content) : first;
    }

    char line[KLOG_LINE_MAX];
    char display[KLOG_LINE_MAX + 16];
    for (uint32_t r = 0; r < content; r++) {
        uint64_t ln = g_top + r;
        int is_cur = (paused && ln == g_cursor);
        if (ln <= total && klog_get_line(ln, line, sizeof line) >= 0) {
            snprintf(display, sizeof display, "%5llu  %s",
                     (unsigned long long)ln, line);
            mon_draw_hl(r, display, is_cur);
        } else {
            mon_draw_hl(r, "", 0);
        }
    }
    if (show_status) {
        char st[200];
        mon_build_status(st, sizeof st);
        mon_draw_line(rows - 1, st, MON_STATUS_FG, MON_STATUS_BG);
    }
    fb_flush(global_framebuffer);
    g_shown = klog_total();
}

static void mon_scroll_region(uint32_t height_px, uint32_t by_px) {
    if (by_px == 0 || by_px >= height_px) return;
    uint32_t *bb = fb_get_backbuffer();
    if (!bb) return;
    uint32_t pitch = fb_backbuffer_pitch();
    uint32_t move_px = height_px - by_px;
    memmove(bb, bb + (size_t)by_px * pitch, (size_t)move_px * pitch * sizeof(uint32_t));
    memset(bb + (size_t)move_px * pitch, 0, (size_t)by_px * pitch * sizeof(uint32_t));
}

static void mon_append_live(int show_status) {
    if (!global_framebuffer) return;
    uint32_t rows    = mon_rows();
    uint32_t content = show_status ? (rows - 1) : rows;
    uint64_t total   = klog_total();
    uint64_t first   = klog_first();

    if (g_shown < first) g_shown = first;
    if (total <= g_shown) return;

    uint64_t nnew = total - g_shown;
    if (nnew >= content) {
        mon_render(show_status);
        return;
    }

    mon_scroll_region(content * 16, (uint32_t)nnew * 16);
    char line[KLOG_LINE_MAX];
    char display[KLOG_LINE_MAX + 16];
    for (uint64_t i = 0; i < nnew; i++) {
        uint32_t row = (uint32_t)(content - nnew + i);
        if (klog_get_line(g_shown + i, line, sizeof line) >= 0) {
            snprintf(display, sizeof display, "%5llu  %s",
                     (unsigned long long)(g_shown + i), line);
            mon_draw_line(row, display, MON_FG, MON_BG);
        } else {
            mon_draw_line(row, "", MON_FG, MON_BG);
        }
    }
    if (show_status) {
        char st[200];
        mon_build_status(st, sizeof st);
        mon_draw_line(rows - 1, st, MON_STATUS_FG, MON_STATUS_BG);
    }
    fb_flush(global_framebuffer);
    g_shown = total;
}

static void mon_boot_echo(void) {
    if (!global_framebuffer) return;
    uint32_t rows  = mon_rows();
    uint64_t total = klog_total();
    uint64_t first = klog_first();
    if (g_shown < first) g_shown = first;
    char line[KLOG_LINE_MAX];
    while (g_shown < total) {
        if (g_boot_row >= rows) g_boot_row = 0;
        int got = klog_get_line(g_shown, line, sizeof line);
        mon_draw_line(g_boot_row, got >= 0 ? line : "", MON_FG, MON_BG);
        fb_flush_lines(global_framebuffer, g_boot_row * 16, g_boot_row * 16 + 16);
        g_boot_row++;
        g_shown++;
    }
}

static void mon_notify(void) {
    g_dirty = 1;
}

void monitor_tick(void) {
    if (!g_dirty) return;
    g_dirty = 0;
    if (vt_active() == VT_MONITOR_INDEX) {
        if (g_mode == MON_LIVE) mon_append_live(1);
        return;
    }
    if (g_boot_echo) mon_boot_echo();
}

void monitor_init(void) {
    g_mode = MON_LIVE;
    klog_set_notify(mon_notify);
}

void monitor_activate(void) {
    g_in_esc = g_in_csi = g_csi_len = 0;
    mon_render(1);
}

void console_boot_logging_off(void) {
    extern void clear_screen_with_scroll(void);
    g_boot_echo = 0;
    clear_screen_with_scroll();
}

static void mon_do_search(uint64_t from) {
    if (!g_last[0]) return;
    uint64_t total = klog_total();
    uint64_t first = klog_first();
    char line[KLOG_LINE_MAX];
    for (uint64_t ln = from; ln <= total; ln++) {
        if (klog_get_line(ln, line, sizeof line) >= 0 && strstr(line, g_last)) {
            g_cursor = ln;
            return;
        }
    }
    for (uint64_t ln = first; ln < from && ln <= total; ln++) {
        if (klog_get_line(ln, line, sizeof line) >= 0 && strstr(line, g_last)) {
            g_cursor = ln;
            return;
        }
    }
}

static void mon_page(int dir) {
    uint32_t content = mon_rows() - 1;
    uint64_t total = klog_total(), first = klog_first();
    if (dir > 0) {
        g_cursor += content;
        if (g_cursor > total) g_cursor = total;
    } else {
        if (g_cursor > first + content) g_cursor -= content;
        else g_cursor = first;
    }
}

static void mon_line(int dir) {
    uint64_t total = klog_total(), first = klog_first();
    if (dir > 0) { if (g_cursor < total) g_cursor++; }
    else         { if (g_cursor > first) g_cursor--; }
}

static void mon_pause_here(void) {
    if (g_mode == MON_LIVE) {
        g_cursor = klog_total();
        g_mode = MON_PAUSED;
    }
}

static void mon_key(char c) {
    if (g_mode == MON_SEARCH) {
        if (c == '\n' || c == '\r') {
            memcpy(g_last, g_query, sizeof g_query);
            g_last[sizeof g_last - 1] = 0;
            g_mode = MON_PAUSED;
            mon_do_search(g_cursor);
            mon_render(1);
        } else if (c == '\b' || c == 0x7F) {
            if (g_qlen > 0) g_qlen--;
            g_query[g_qlen] = 0;
            mon_render(1);
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) {
            if (g_qlen < (int)sizeof(g_query) - 1) { g_query[g_qlen++] = c; g_query[g_qlen] = 0; }
            mon_render(1);
        }
        return;
    }

    switch (c) {
        case ' ': case 'f': mon_pause_here(); mon_page(+1); mon_render(1); break;
        case 'b':           mon_pause_here(); mon_page(-1); mon_render(1); break;
        case 'j':           mon_pause_here(); mon_line(+1); mon_render(1); break;
        case 'k':           mon_pause_here(); mon_line(-1); mon_render(1); break;
        case 'g':           mon_pause_here(); g_cursor = klog_first(); mon_render(1); break;
        case 'G':           g_mode = MON_LIVE; mon_render(1); break;
        case '/':           mon_pause_here(); g_mode = MON_SEARCH; g_qlen = 0; g_query[0] = 0; mon_render(1); break;
        case 'n':           mon_pause_here(); mon_do_search(g_cursor + 1); mon_render(1); break;
        default: break;
    }
}

static void mon_csi(char final) {
    switch (final) {
        case 'A': mon_pause_here(); mon_line(-1); mon_render(1); break;
        case 'B': mon_pause_here(); mon_line(+1); mon_render(1); break;
        case 'H': mon_pause_here(); g_cursor = klog_first(); mon_render(1); break;
        case 'F': g_mode = MON_LIVE; mon_render(1); break;
        case '~':
            if (g_csi_len > 0 && g_csi[0] == '5') { mon_pause_here(); mon_page(-1); mon_render(1); }
            else if (g_csi_len > 0 && g_csi[0] == '6') { mon_pause_here(); mon_page(+1); mon_render(1); }
            break;
        default: break;
    }
}

void monitor_input(char c) {
    if (g_in_esc) {
        g_in_esc = 0;
        if (c == '[') { g_in_csi = 1; g_csi_len = 0; return; }
        mon_key(c);
        return;
    }
    if (g_in_csi) {
        if ((unsigned char)c >= 0x40 && (unsigned char)c <= 0x7E) {
            g_in_csi = 0;
            mon_csi(c);
            return;
        }
        if (g_csi_len < (int)sizeof(g_csi)) g_csi[g_csi_len++] = c;
        return;
    }
    if (c == 0x1B) { g_in_esc = 1; return; }
    mon_key(c);
}

#include "../../include/console/console.h"
#include "../../include/console/klog.h"
#include "../../include/graphics/fb/fb.h"
#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdio.h>

extern fb_info_t *global_framebuffer;

static uint32_t mon_pal(int idx) {
    uint32_t pal[16];
    console_get_theme(pal, NULL, NULL);
    return pal[idx & 15];
}

#define MON_FG        console_theme_fg()
#define MON_BG        console_theme_bg()
#define MON_STATUS_FG console_theme_bg()
#define MON_STATUS_BG mon_pal(6)

enum { MON_LIVE, MON_PAUSED, MON_SEARCH };

static int      g_mode = MON_LIVE;
static int      g_filter = -1;
static uint64_t g_pause_mark = 0;
static int      g_return_vt = -1;

void monitor_set_return_vt(int vt) { g_return_vt = vt; }

static uint32_t mon_level_fg(int lvl) {
    uint32_t pal[16];
    console_get_theme(pal, NULL, NULL);
    switch (lvl) {
        case KLOG_LVL_ERR:  return pal[9];
        case KLOG_LVL_WARN: return pal[11];
        case KLOG_LVL_OK:   return pal[10];
        case KLOG_LVL_DBG:  return pal[8];
        default:            return console_theme_fg();
    }
}

static int mon_passes(uint64_t ln) {
    if (g_filter < 0) return 1;
    return klog_line_level(ln) == g_filter;
}
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

static uint32_t mon_cols(void) { return global_framebuffer ? global_framebuffer->width  / fb_font_width()  : 80; }
static uint32_t mon_rows(void) { return global_framebuffer ? global_framebuffer->height / fb_font_height() : 25; }

static void mon_draw_line(uint32_t row, const char *s, uint32_t fg, uint32_t bg) {
    if (!global_framebuffer) return;
    uint32_t cw = fb_font_width(), chh = fb_font_height();
    uint32_t y = row * chh;
    fb_fill_rect(global_framebuffer, 0, y, global_framebuffer->width, chh, bg);
    uint32_t cols = mon_cols();
    uint32_t x = 0;
    for (uint32_t i = 0; i < cols && s[i]; i++) {
        fb_draw_char(global_framebuffer, (uint8_t)s[i], x, y, fg);
        x += cw;
    }
}

#define MON_CUR_FG console_theme_bg()
#define MON_CUR_BG mon_pal(12)
#define MON_HIT_FG console_theme_bg()
#define MON_HIT_BG mon_pal(11)

static uint32_t mon_line_rows(const char *s) {
    uint32_t cols = mon_cols();
    uint32_t len = (uint32_t)strlen(s);
    if (cols == 0) return 1;
    if (len == 0) return 1;
    return (len + cols - 1) / cols;
}

static void mon_draw_hl_lvl(uint32_t row, const char *s, uint32_t off, int is_cursor,
                            int lvl) {
    if (!global_framebuffer) return;
    uint32_t cw = fb_font_width(), chh = fb_font_height();
    uint32_t y = row * chh;
    uint32_t base_fg = is_cursor ? MON_CUR_FG : mon_level_fg(lvl);
    uint32_t base_bg = is_cursor ? MON_CUR_BG : MON_BG;
    fb_fill_rect(global_framebuffer, 0, y, global_framebuffer->width, chh, base_bg);
    uint32_t cols = mon_cols();
    int qlen = (int)strlen(g_last);
    char hit[KLOG_LINE_MAX + 16];
    int slen = (int)strlen(s);
    if (slen > (int)sizeof(hit)) slen = (int)sizeof(hit);
    for (int i = 0; i < slen; i++) hit[i] = 0;
    if (qlen > 0 && slen >= qlen) {
        for (int k = 0; k + qlen <= slen; k++) {
            if (strncmp(s + k, g_last, qlen) == 0)
                for (int m = 0; m < qlen; m++) hit[k + m] = 1;
        }
    }
    for (uint32_t i = 0; i < cols && s[off + i]; i++) {
        uint32_t si = off + i;
        uint32_t fg = base_fg, bg = base_bg;
        if (si < (uint32_t)slen && hit[si]) { fg = MON_HIT_FG; bg = MON_HIT_BG; }
        if (bg != base_bg) fb_fill_rect(global_framebuffer, i * cw, y, cw, chh, bg);
        fb_draw_char(global_framebuffer, (uint8_t)s[si], i * cw, y, fg);
    }
}

static void mon_draw_hl(uint32_t row, const char *s, uint32_t off, int is_cursor) {
    mon_draw_hl_lvl(row, s, off, is_cursor, KLOG_LVL_INFO);
}

static uint32_t mon_draw_wrapped_lvl(uint32_t row, uint32_t limit, const char *s,
                                     int is_cursor, int lvl) {
    uint32_t cols = mon_cols();
    uint32_t need = mon_line_rows(s);
    uint32_t drawn = 0;
    for (uint32_t k = 0; k < need && row + drawn < limit; k++) {
        mon_draw_hl_lvl(row + drawn, s, k * cols, is_cursor, lvl);
        drawn++;
    }
    return drawn;
}

static uint32_t mon_draw_wrapped(uint32_t row, uint32_t limit, const char *s,
                                 int is_cursor) {
    return mon_draw_wrapped_lvl(row, limit, s, is_cursor, KLOG_LVL_INFO);
}

static void mon_format(uint64_t ln, char *out, size_t cap) {
    char line[KLOG_LINE_MAX];
    if (klog_get_line(ln, line, sizeof line) < 0) { out[0] = 0; return; }
    snprintf(out, cap, "%5llu  %s", (unsigned long long)ln, line);
}

static uint32_t mon_rows_for(uint64_t ln) {
    char display[KLOG_LINE_MAX + 16];
    mon_format(ln, display, sizeof display);
    return mon_line_rows(display);
}

static void mon_build_status(char *out, size_t n) {
    if (g_mode == MON_SEARCH) {
        size_t p = 0;
        out[p++] = '/';
        for (int i = 0; i < g_qlen && p < n - 1; i++) out[p++] = g_query[i];
        out[p] = 0;
        return;
    }
    static const char *const FLT[] = { "all", "info", "warn", "err", "ok", "debug" };
    const char *flt = (g_filter < 0) ? FLT[0] : FLT[1 + g_filter];

    static const char *const LVL[] = { "nothing", "errors", "warnings", "info", "everything" };
    log_level_t lv = klog_get_level();
    const char *lvname = LVL[((int)lv >= 0 && (int)lv <= 4) ? (int)lv : 3];

    char buf[240];
    if (g_mode == MON_LIVE) {
        snprintf(buf, sizeof buf,
                 "  [debug monitor] LIVE   scroll  /:find  n:next  1-5:show %s  0:all"
                 "  L:keeping %s  q:quit", flt, lvname);
    } else {
        uint64_t behind = klog_total() > g_pause_mark
                        ? klog_total() - g_pause_mark : 0;
        snprintf(buf, sizeof buf,
                 "  [debug monitor] HELD (+%llu new)  G:live  /:find  n:next"
                 "  1-5:show %s  0:all  L:keeping %s  q:quit",
                 (unsigned long long)behind, flt, lvname);
    }
    size_t p = 0;
    for (const char *q = buf; *q && p < n - 1; q++) out[p++] = *q;
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
        if (g_top < first) g_top = first;
        if (g_cursor < g_top) g_top = g_cursor;

        uint32_t used = 0;
        for (uint64_t ln = g_top; ln <= g_cursor; ln++)
            if (mon_passes(ln)) used += mon_rows_for(ln);
        while (used > content && g_top < g_cursor) {
            if (mon_passes(g_top)) used -= mon_rows_for(g_top);
            g_top++;
        }
        while (g_top < g_cursor && !mon_passes(g_top)) g_top++;
    } else {
        uint64_t ln = total;
        uint32_t used = 0;
        for (;;) {
            uint32_t need = mon_passes(ln) ? mon_rows_for(ln) : 0;
            if (used + need > content && used > 0) { ln++; break; }
            used += need;
            if (ln <= first) break;
            ln--;
        }
        g_top = ln;
    }

    char display[KLOG_LINE_MAX + 16];
    uint32_t r = 0;
    for (uint64_t ln = g_top; ln <= total && r < content; ln++) {
        if (!mon_passes(ln)) continue;
        mon_format(ln, display, sizeof display);
        uint32_t used = mon_draw_wrapped_lvl(r, content, display,
                                             paused && ln == g_cursor,
                                             klog_line_level(ln));
        if (used == 0) break;
        r += used;
    }
    if (r == 0 && g_filter >= 0) {
        static const char *const FN[] = { "info", "warning", "error", "ok", "debug" };
        char msg[96];
        snprintf(msg, sizeof msg,
                 "  nothing logged at level '%s' yet - press 0 to show everything",
                 FN[g_filter]);
        mon_draw_hl(0, msg, 0, 0);
        r = 1;
    }
    while (r < content) mon_draw_hl(r++, "", 0, 0);
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
    uint32_t newrows = 0;
    for (uint64_t i = 0; i < nnew; i++) newrows += mon_rows_for(g_shown + 1 + i);
    if (nnew >= content || newrows >= content) {
        mon_render(show_status);
        return;
    }

    mon_scroll_region(content * fb_font_height(), newrows * fb_font_height());
    char display[KLOG_LINE_MAX + 16];
    uint32_t row = content - newrows;
    for (uint64_t i = 0; i < nnew; i++) {
        mon_format(g_shown + 1 + i, display, sizeof display);
        row += mon_draw_wrapped(row, content, display, 0);
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
        fb_flush_lines(global_framebuffer, g_boot_row * fb_font_height(), g_boot_row * fb_font_height() + fb_font_height());
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
    g_boot_echo = 0;
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

static uint64_t mon_next_shown(uint64_t from, int dir) {
    uint64_t total = klog_total(), first = klog_first();
    if (dir > 0) {
        for (uint64_t ln = from + 1; ln <= total; ln++)
            if (mon_passes(ln)) return ln;
        return from;
    }
    for (uint64_t ln = from; ln > first; ln--)
        if (mon_passes(ln - 1)) return ln - 1;
    return from;
}

static void mon_line(int dir) {
    uint64_t total = klog_total(), first = klog_first();
    if (!mon_passes(g_cursor)) {
        uint64_t near = mon_next_shown(g_cursor, dir);
        if (near == g_cursor) near = mon_next_shown(g_cursor, -dir);
        if (near != g_cursor) { g_cursor = near; return; }
    }
    uint64_t next = mon_next_shown(g_cursor, dir);
    if (next != g_cursor) { g_cursor = next; return; }
    if (g_filter < 0) {
        if (dir > 0) { if (g_cursor < total) g_cursor++; }
        else         { if (g_cursor > first) g_cursor--; }
    }
}

static void mon_page(int dir) {
    uint32_t content = mon_rows() - 1;
    for (uint32_t i = 0; i < content; i++) {
        uint64_t before = g_cursor;
        mon_line(dir);
        if (g_cursor == before) break;
    }
}

static void mon_set_filter(int f) {
    g_filter = f;
    if (g_mode != MON_LIVE && !mon_passes(g_cursor)) {
        uint64_t near = mon_next_shown(g_cursor, -1);
        if (near == g_cursor) near = mon_next_shown(g_cursor, +1);
        g_cursor = near;
        g_top = near;
    }
    mon_render(1);
}

static void mon_pause_here(void) {
    if (g_mode == MON_LIVE) {
        g_cursor = klog_total();
        g_pause_mark = g_cursor;
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
        case 'q': case 'Q': case 27:
            if (g_return_vt >= 0) { int v = g_return_vt; g_return_vt = -1; vt_switch(v); }
            break;
        case '0': mon_set_filter(-1);            break;
        case '1': mon_set_filter(KLOG_LVL_INFO); break;
        case '2': mon_set_filter(KLOG_LVL_WARN); break;
        case '3': mon_set_filter(KLOG_LVL_ERR);  break;
        case '4': mon_set_filter(KLOG_LVL_OK);   break;
        case '5': mon_set_filter(KLOG_LVL_DBG);  break;
        case 'L': case 'l': {
            log_level_t lv = klog_get_level();
            lv = (lv >= LOG_LEVEL_DEBUG) ? LOG_LEVEL_ERR : (log_level_t)(lv + 1);
            klog_set_level(lv);
            mon_render(1);
            break;
        }
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

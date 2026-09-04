#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <curses.h>

#define PER_LINE   16
#define MAX_EDITS  8192
#define CHUNK      65536

typedef struct { long long off; unsigned char val; unsigned char orig; } edit_t;

static int        g_fd = -1;
static char       g_path[512];
static long long  g_size;
static int        g_rw;
static int        g_dirty;

static edit_t     g_edit[MAX_EDITS];
static int        g_nedit;

static long long  g_top;
static long long  g_cur;
static int        g_ascii_pane;
static int        g_nib;
static char       g_msg[160];

static WINDOW    *w_head, *w_body, *w_foot;
static int        g_rows_visible;

static long long  g_last_needle_len;
static unsigned char g_last_needle[64];

static void set_msg(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_msg, sizeof g_msg, fmt, ap);
    va_end(ap);
}

static int find_edit(long long off) {
    for (int i = 0; i < g_nedit; i++) if (g_edit[i].off == off) return i;
    return -1;
}

static int raw_byte(long long off, unsigned char *out) {
    if (off < 0 || off >= g_size) return 0;
    if (lseek(g_fd, (off_t)off, SEEK_SET) < 0) return 0;
    return read(g_fd, out, 1) == 1;
}

static int byte_at(long long off, unsigned char *out, int *edited) {
    int e = find_edit(off);
    if (e >= 0) { *out = g_edit[e].val; if (edited) *edited = 1; return 1; }
    if (edited) *edited = 0;
    return raw_byte(off, out);
}

static void put_edit(long long off, unsigned char val) {
    int e = find_edit(off);
    if (e >= 0) {
        g_edit[e].val = val;
        if (g_edit[e].val == g_edit[e].orig) {
            g_edit[e] = g_edit[--g_nedit];
        }
    } else {
        if (g_nedit >= MAX_EDITS) { set_msg("edit buffer full - save first"); return; }
        unsigned char orig = 0;
        raw_byte(off, &orig);
        if (orig == val) return;
        g_edit[g_nedit].off = off;
        g_edit[g_nedit].val = val;
        g_edit[g_nedit].orig = orig;
        g_nedit++;
    }
    g_dirty = (g_nedit > 0);
}

static void undo_last(void) {
    if (g_nedit == 0) { set_msg("nothing to undo"); return; }
    g_nedit--;
    g_cur = g_edit[g_nedit].off;
    g_dirty = (g_nedit > 0);
    set_msg("undid change at %08llx", (unsigned long long)g_cur);
}

static int save_file(void) {
    if (!g_rw) { set_msg("read-only - reopen with -w to write"); return -1; }
    if (g_nedit == 0) { set_msg("no changes"); return 0; }
    int n = g_nedit;
    for (int i = 0; i < g_nedit; i++) {
        if (lseek(g_fd, (off_t)g_edit[i].off, SEEK_SET) < 0 ||
            write(g_fd, &g_edit[i].val, 1) != 1) {
            set_msg("write failed at %08llx: %s",
                    (unsigned long long)g_edit[i].off, strerror(errno));
            return -1;
        }
    }
    fsync(g_fd);
    g_nedit = 0;
    g_dirty = 0;
    set_msg("wrote %d byte%s", n, n == 1 ? "" : "s");
    return 0;
}

static void clamp_view(void) {
    if (g_cur < 0) g_cur = 0;
    if (g_cur >= g_size) g_cur = g_size ? g_size - 1 : 0;
    long long line = g_cur / PER_LINE;
    long long top_line = g_top / PER_LINE;
    if (line < top_line) top_line = line;
    if (line >= top_line + g_rows_visible) top_line = line - g_rows_visible + 1;
    if (top_line < 0) top_line = 0;
    g_top = top_line * PER_LINE;
}

static void draw_head(void) {
    werase(w_head);
    wattrset(w_head, COLOR_PAIR(1) | A_BOLD);
    for (int x = 0; x < COLS; x++) mvwaddch(w_head, 0, x, ' ');
    char left[256];
    snprintf(left, sizeof left, " hexed  %s  %s%s",
             g_path, g_rw ? "rw" : "ro", g_dirty ? "  *modified*" : "");
    mvwaddnstr(w_head, 0, 0, left, COLS);
    char right[64];
    snprintf(right, sizeof right, "%lld bytes ", g_size);
    int rx = COLS - (int)strlen(right);
    if (rx > (int)strlen(left)) mvwaddnstr(w_head, 0, rx, right, COLS - rx);
    wnoutrefresh(w_head);
}

static void draw_body(void) {
    werase(w_body);
    for (int row = 0; row < g_rows_visible; row++) {
        long long base = g_top + (long long)row * PER_LINE;
        if (base >= g_size) break;

        wattrset(w_body, COLOR_PAIR(2));
        mvwprintw(w_body, row, 0, "%08llx", (unsigned long long)base);

        for (int i = 0; i < PER_LINE; i++) {
            long long off = base + i;
            int hx = 10 + i * 3 + (i >= 8 ? 1 : 0);
            int ax = 10 + PER_LINE * 3 + 2 + i;
            if (ax >= COLS) continue;

            if (off >= g_size) {
                wattrset(w_body, A_NORMAL);
                mvwaddnstr(w_body, row, hx, "  ", 2);
                continue;
            }
            unsigned char b = 0;
            int edited = 0;
            byte_at(off, &b, &edited);

            attr_t base_attr = edited ? (COLOR_PAIR(4) | A_BOLD) : A_NORMAL;
            attr_t hex_attr = base_attr;
            attr_t asc_attr = edited ? (COLOR_PAIR(4) | A_BOLD) : COLOR_PAIR(3);
            if (off == g_cur) {
                if (g_ascii_pane) asc_attr |= A_REVERSE;
                else              hex_attr |= A_REVERSE;
            }

            char hb[3];
            snprintf(hb, sizeof hb, "%02x", b);
            wattrset(w_body, hex_attr);
            mvwaddnstr(w_body, row, hx, hb, 2);

            wattrset(w_body, asc_attr);
            mvwaddch(w_body, row, ax, isprint(b) ? b : '.');
        }
    }
    wnoutrefresh(w_body);
}

static void draw_foot(void) {
    werase(w_foot);
    wattrset(w_foot, COLOR_PAIR(5));
    for (int x = 0; x < COLS; x++) mvwaddch(w_foot, 0, x, ' ');

    unsigned char b = 0;
    int edited = 0;
    byte_at(g_cur, &b, &edited);
    char info[128];
    snprintf(info, sizeof info,
             " %08llx  %02x  %3u  %c   %s",
             (unsigned long long)g_cur, b, b, isprint(b) ? b : '.',
             g_ascii_pane ? "ascii" : "hex");
    mvwaddnstr(w_foot, 0, 0, info, COLS);

    if (g_msg[0]) {
        wattrset(w_foot, COLOR_PAIR(6) | A_BOLD);
        int mx = COLS - (int)strlen(g_msg) - 1;
        if (mx < (int)strlen(info) + 2) mx = (int)strlen(info) + 2;
        if (mx < COLS) mvwaddnstr(w_foot, 0, mx, g_msg, COLS - mx);
    }

    wattrset(w_foot, COLOR_PAIR(5));
    mvwaddnstr(w_foot, 1, 0,
        " Tab pane  g goto  / search  n next  u undo  s save  q quit ", COLS);
    wnoutrefresh(w_foot);
}

static void draw_all(void) {
    clamp_view();
    draw_head();
    draw_body();
    draw_foot();
    doupdate();
}

static int prompt(const char *label, char *buf, int cap) {
    werase(w_foot);
    wattrset(w_foot, COLOR_PAIR(6) | A_BOLD);
    for (int x = 0; x < COLS; x++) mvwaddch(w_foot, 0, x, ' ');
    mvwaddnstr(w_foot, 0, 0, label, COLS);
    wattrset(w_foot, A_NORMAL);
    curs_set(1);
    int rc = mvwgetnstr(w_foot, 0, (int)strlen(label) + 1, buf, cap);
    curs_set(0);
    return rc;
}

static int parse_needle(const char *s, unsigned char *out, int cap) {
    if (s[0] == '"') {
        int n = 0;
        for (int i = 1; s[i] && s[i] != '"' && n < cap; i++) out[n++] = (unsigned char)s[i];
        return n;
    }
    int n = 0, hi = -1;
    for (int i = 0; s[i] && n < cap; i++) {
        int c = tolower((unsigned char)s[i]);
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c == ' ') continue;
        else return -1;
        if (hi < 0) hi = v;
        else { out[n++] = (unsigned char)((hi << 4) | v); hi = -1; }
    }
    if (hi >= 0) return -1;
    return n;
}

static long long search_from(long long start, const unsigned char *pat, int plen) {
    if (plen <= 0) return -1;
    unsigned char *buf = malloc(CHUNK + plen);
    if (!buf) return -1;
    long long off = start;
    while (off < g_size) {
        if (lseek(g_fd, (off_t)off, SEEK_SET) < 0) break;
        long want = CHUNK + plen - 1;
        if (off + want > g_size) want = (long)(g_size - off);
        long got = 0;
        while (got < want) {
            long n = read(g_fd, buf + got, (size_t)(want - got));
            if (n <= 0) break;
            got += n;
        }
        if (got < plen) break;
        for (long i = 0; i + plen <= got; i++) {
            for (int k = 0; k < plen; k++) {
                long long o = off + i + k;
                unsigned char b = buf[i + k];
                int e = find_edit(o);
                if (e >= 0) b = g_edit[e].val;
                if (b != pat[k]) goto next;
            }
            free(buf);
            return off + i;
        next: ;
        }
        off += got - plen + 1;
    }
    free(buf);
    return -1;
}

static void do_goto(void) {
    char buf[64] = {0};
    if (prompt("goto offset (hex, or +/- delta):", buf, sizeof buf) != OK || !buf[0]) return;
    long long v;
    if (buf[0] == '+' || buf[0] == '-') {
        v = strtoll(buf + 1, NULL, 16);
        g_cur += (buf[0] == '-') ? -v : v;
    } else {
        v = strtoll(buf, NULL, 16);
        g_cur = v;
    }
    set_msg("");
}

static void do_search(int again) {
    if (!again) {
        char buf[128] = {0};
        if (prompt("search (hex bytes, or \"text\"):", buf, sizeof buf) != OK || !buf[0]) return;
        int n = parse_needle(buf, g_last_needle, sizeof g_last_needle);
        if (n <= 0) { set_msg("bad pattern"); g_last_needle_len = 0; return; }
        g_last_needle_len = n;
    }
    if (g_last_needle_len <= 0) { set_msg("no pattern"); return; }
    long long hit = search_from(g_cur + 1, g_last_needle, (int)g_last_needle_len);
    if (hit < 0) hit = search_from(0, g_last_needle, (int)g_last_needle_len);
    if (hit < 0) { set_msg("not found"); return; }
    g_cur = hit;
    set_msg("found at %08llx", (unsigned long long)hit);
}

static int confirm_quit(void) {
    if (!g_dirty) return 1;
    char buf[8] = {0};
    if (prompt("unsaved changes - discard? (yes/no):", buf, sizeof buf) != OK) return 0;
    return buf[0] == 'y' || buf[0] == 'Y';
}

static void edit_hex(int c) {
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return;
    if (!g_rw) { set_msg("read-only - reopen with -w"); return; }

    unsigned char b = 0;
    byte_at(g_cur, &b, NULL);
    if (g_nib == 0) { b = (unsigned char)((v << 4) | (b & 0x0F)); g_nib = 1; }
    else            { b = (unsigned char)((b & 0xF0) | v); g_nib = 0; }
    put_edit(g_cur, b);
    if (g_nib == 0 && g_cur + 1 < g_size) g_cur++;
}

static void usage(void) {
    fputs("Usage: hexed [-w] <file>\n"
          "  -w   open for writing (default is read-only)\n", stderr);
}

int main(int argc, char **argv) {
    int ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        if (!strcmp(argv[ai], "-w")) g_rw = 1;
        else if (!strcmp(argv[ai], "-h") || !strcmp(argv[ai], "--help")) { usage(); return 0; }
        else { usage(); return 1; }
    }
    if (ai >= argc) { usage(); return 1; }

    snprintf(g_path, sizeof g_path, "%s", argv[ai]);
    g_fd = open(g_path, g_rw ? O_RDWR : O_RDONLY);
    if (g_fd < 0 && g_rw) {
        g_fd = open(g_path, O_RDONLY);
        if (g_fd >= 0) { g_rw = 0; }
    }
    if (g_fd < 0) {
        fprintf(stderr, "hexed: cannot open %s: %s\n", g_path, strerror(errno));
        return 1;
    }
    off_t end = lseek(g_fd, 0, SEEK_END);
    g_size = (end > 0) ? (long long)end : 0;
    if (g_size == 0) {
        fprintf(stderr, "hexed: %s is empty\n", g_path);
        close(g_fd);
        return 1;
    }

    if (!initscr()) { fprintf(stderr, "hexed: cannot start curses\n"); return 1; }
    start_color();
    init_pair(1, COLOR_WHITE,  COLOR_BLUE);
    init_pair(2, COLOR_CYAN,   COLOR_BLACK);
    init_pair(3, COLOR_GREEN,  COLOR_BLACK);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    init_pair(5, COLOR_BLACK,  COLOR_CYAN);
    init_pair(6, COLOR_WHITE,  COLOR_RED);
    curs_set(0);
    keypad(stdscr, TRUE);

    g_rows_visible = LINES - 3;
    if (g_rows_visible < 1) g_rows_visible = 1;
    w_head = newwin(1, COLS, 0, 0);
    w_body = newwin(g_rows_visible, COLS, 1, 0);
    w_foot = newwin(2, COLS, LINES - 2, 0);
    keypad(w_foot, TRUE);
    if (!w_head || !w_body || !w_foot) { endwin(); return 1; }

    set_msg("%s", g_rw ? "" : "read-only");
    int running = 1;
    while (running) {
        draw_all();
        int c = wgetch(stdscr);
        switch (c) {
            case KEY_UP:    g_cur -= PER_LINE; g_nib = 0; break;
            case KEY_DOWN:  g_cur += PER_LINE; g_nib = 0; break;
            case KEY_LEFT:  g_cur--; g_nib = 0; break;
            case KEY_RIGHT: g_cur++; g_nib = 0; break;
            case KEY_PPAGE: g_cur -= (long long)PER_LINE * g_rows_visible; g_nib = 0; break;
            case KEY_NPAGE: g_cur += (long long)PER_LINE * g_rows_visible; g_nib = 0; break;
            case KEY_HOME:  g_cur = 0; g_nib = 0; break;
            case KEY_END:   g_cur = g_size - 1; g_nib = 0; break;
            case '\t':      g_ascii_pane = !g_ascii_pane; g_nib = 0; break;
            case KEY_RESIZE: break;
            case 'g': case 'G': do_goto(); break;
            case '/':           do_search(0); break;
            case 'n':
                if (g_ascii_pane) goto ascii_input;
                do_search(1);
                break;
            case 'u':
                if (g_ascii_pane) goto ascii_input;
                undo_last();
                break;
            case 's':
                if (g_ascii_pane) goto ascii_input;
                save_file();
                break;
            case 'q':
                if (g_ascii_pane) goto ascii_input;
                if (confirm_quit()) running = 0;
                break;
            case ERR:
                running = 0;
                break;
            default:
            ascii_input:
                if (g_ascii_pane) {
                    if (c >= 32 && c < 127) {
                        if (!g_rw) { set_msg("read-only - reopen with -w"); break; }
                        put_edit(g_cur, (unsigned char)c);
                        if (g_cur + 1 < g_size) g_cur++;
                    }
                } else {
                    edit_hex(c);
                }
                break;
        }
    }

    endwin();
    close(g_fd);
    return 0;
}

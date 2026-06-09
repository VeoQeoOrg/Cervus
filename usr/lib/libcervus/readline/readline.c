#include <readline.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/cervus.h>

#define RL_LINE_MAX  4096
#define RL_HIST_MAX  1024

static char        g_history[RL_HIST_MAX][RL_LINE_MAX];
static int         g_hist_count = 0, g_hist_head = 0;
static char        g_hist_path[1024];
static int         g_hist_file_set = 0;

static rl_complete_fn g_complete_cb = NULL;
static rl_suggest_fn  g_suggest_cb  = NULL;
static char           g_input_color[48] = "";

static int g_cols = 80;
static int g_rows = 25;
static int g_start_row = 0;
static int g_prompt_len = 0;
static const char *g_rl_buf = NULL;

static void term_update_size(void) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 8 && ws.ws_row >= 2) {
        g_cols = (int)ws.ws_col;
        g_rows = (int)ws.ws_row;
    }
}

static int term_get_cursor_row(void) {
    struct cursor_pos cp;
    if (ioctl(1, TIOCGCURSOR, &cp) == 0) return (int)cp.row;
    return 0;
}


static void vt_goto(int row, int col) {
    char b[24];
    int n = snprintf(b, sizeof(b), "\x1b[%d;%dH", row + 1, col + 1);
    write(1, b, n);
}

static void vt_eol(void) { write(1, "\x1b[K", 3); }

static int utf8_is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

static int vis_cols(int byte_pos) {
    if (!g_rl_buf) return byte_pos;
    int cols = 0;
    for (int i = 0; i < byte_pos; i++)
        if (!utf8_is_cont((unsigned char)g_rl_buf[i])) cols++;
    return cols;
}

static int utf8_next(const char *buf, int len, int p) {
    if (p >= len) return len;
    p++;
    while (p < len && utf8_is_cont((unsigned char)buf[p])) p++;
    return p;
}

static int utf8_prev(const char *buf, int p) {
    if (p <= 0) return 0;
    p--;
    while (p > 0 && utf8_is_cont((unsigned char)buf[p])) p--;
    return p;
}

static void sync_start_row(int cur_byte_pos) {
    int real_row = term_get_cursor_row();
    int row_offset = (g_prompt_len + vis_cols(cur_byte_pos)) / g_cols;
    g_start_row = real_row - row_offset;
    if (g_start_row < 0) g_start_row = 0;
}

static void input_pos_to_screen(int pos, int *row, int *col) {
    int abs = g_prompt_len + vis_cols(pos);
    *row = g_start_row + abs / g_cols;
    *col = abs % g_cols;
}

static void cursor_to(int pos) {
    int row, col;
    input_pos_to_screen(pos, &row, &col);
    if (row >= g_rows) row = g_rows - 1;
    if (row < 0) row = 0;
    vt_goto(row, col);
}

static int last_row_of(int len) {
    int abs = g_prompt_len + vis_cols(len);
    return g_start_row + (abs > 0 ? (abs - 1) : 0) / g_cols;
}

static void redraw(const char *buf, int from, int new_len, int old_len, int pos) {
    cursor_to(from);
    if (new_len > from) write(1, buf + from, new_len - from);
    sync_start_row(new_len);
    if (old_len > new_len) {
        int old_last = last_row_of(old_len);
        int new_last = last_row_of(new_len);
        cursor_to(new_len); vt_eol();
        for (int r = new_last + 1; r <= old_last; r++) {
            if (r >= g_rows) break;
            vt_goto(r, 0); vt_eol();
        }
    }
    cursor_to(pos);
}

static void replace_line(char *buf, int *len, int *pos, const char *newtext, int newlen) {
    int old_len = *len;
    for (int i = 0; i < newlen; i++) buf[i] = newtext[i];
    buf[newlen] = '\0';
    *len = newlen;
    *pos = newlen;
    redraw(buf, 0, newlen, old_len, newlen);
}

static void insert_str(char *buf, int *len, int *pos, int maxlen, const char *s, int slen) {
    if (*len + slen >= maxlen) return;
    for (int i = *len; i >= *pos; i--) buf[i + slen] = buf[i];
    for (int i = 0; i < slen; i++) buf[*pos + i] = s[i];
    *len += slen;
    buf[*len] = '\0';
    cursor_to(*pos);
    write(1, buf + *pos, *len - *pos);
    sync_start_row(*len);
    *pos += slen;
    cursor_to(*pos);
}

const char *readline_history_get(int n) {
    if (n < 1 || n > g_hist_count) return NULL;
    return g_history[(g_hist_head + g_hist_count - n) % RL_HIST_MAX];
}

int readline_history_count(void) { return g_hist_count; }

static void hist_save_entry(const char *l) {
    if (!g_hist_file_set) return;
    int fd = open(g_hist_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    int n = 0;
    while (l[n]) n++;
    write(fd, l, n);
    write(fd, "\n", 1);
    close(fd);
}

void readline_add_history(const char *l) {
    if (!l || !l[0]) return;
    if (g_hist_count > 0) {
        int last = (g_hist_head + g_hist_count - 1) % RL_HIST_MAX;
        if (strcmp(g_history[last], l) == 0) return;
    }
    int idx = (g_hist_head + g_hist_count) % RL_HIST_MAX;
    strncpy(g_history[idx], l, RL_LINE_MAX - 1);
    g_history[idx][RL_LINE_MAX - 1] = '\0';
    if (g_hist_count < RL_HIST_MAX) g_hist_count++;
    else g_hist_head = (g_hist_head + 1) % RL_HIST_MAX;
    hist_save_entry(l);
}

void readline_clear_history(void) {
    g_hist_count = 0;
    g_hist_head = 0;
    if (g_hist_file_set) {
        int fd = open(g_hist_path, O_WRONLY | O_TRUNC, 0600);
        if (fd >= 0) close(fd);
    }
}

void readline_set_history_file(const char *path) {
    if (!path) { g_hist_file_set = 0; return; }
    strncpy(g_hist_path, path, sizeof(g_hist_path) - 1);
    g_hist_path[sizeof(g_hist_path) - 1] = '\0';
    g_hist_file_set = 1;

    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return;
    char line[RL_LINE_MAX];
    int li = 0;
    char ch;
    while (read(fd, &ch, 1) > 0) {
        if (ch == '\n' || li >= RL_LINE_MAX - 1) {
            line[li] = '\0';
            if (li > 0) {
                int idx = (g_hist_head + g_hist_count) % RL_HIST_MAX;
                strncpy(g_history[idx], line, RL_LINE_MAX - 1);
                g_history[idx][RL_LINE_MAX - 1] = '\0';
                if (g_hist_count < RL_HIST_MAX) g_hist_count++;
                else g_hist_head = (g_hist_head + 1) % RL_HIST_MAX;
            }
            li = 0;
        } else {
            line[li++] = ch;
        }
    }
    close(fd);
}

void readline_set_completion(rl_complete_fn cb) { g_complete_cb = cb; }
void readline_set_suggest(rl_suggest_fn cb)     { g_suggest_cb = cb; }

void readline_set_input_color(const char *seq) {
    if (!seq) { g_input_color[0] = '\0'; return; }
    int i = 0;
    while (seq[i] && i < (int)sizeof(g_input_color) - 1) { g_input_color[i] = seq[i]; i++; }
    g_input_color[i] = '\0';
}

static int comp_common_prefix(rl_completions_t *c) {
    if (c->count == 0) return 0;
    int n = (int)strlen(c->items[0]);
    for (int i = 1; i < c->count; i++) {
        int j = 0;
        while (j < n && c->items[i][j] && c->items[0][j] == c->items[i][j]) j++;
        n = j;
    }
    return n;
}

static void comp_replace_word(char *buf, int *len, int *pos, int maxlen,
                              int word_start, const char *text, int add_slash) {
    int old_len = *len;
    int tail = *len - *pos;
    int tlen = (int)strlen(text);
    int newlen = word_start + tlen + (add_slash ? 1 : 0) + tail;
    if (newlen >= maxlen) return;

    char tailbuf[RL_LINE_MAX];
    if (tail > 0) memcpy(tailbuf, buf + *pos, tail);

    int w = word_start;
    for (int i = 0; i < tlen; i++) buf[w++] = text[i];
    if (add_slash) buf[w++] = '/';
    int new_pos = w;
    if (tail > 0) memcpy(buf + w, tailbuf, tail);
    w += tail;
    buf[w] = '\0';
    *len = w;
    *pos = new_pos;
    g_rl_buf = buf;
    redraw(buf, word_start, *len, old_len, *pos);
}

static int g_menu_rows = 0;

static int g_menu_base_row = 0;

static void menu_clear(void) {
    if (g_menu_rows <= 0) return;
    for (int r = 1; r <= g_menu_rows; r++) {
        int row = g_menu_base_row + r;
        if (row >= g_rows) break;
        vt_goto(row, 0);
        vt_eol();
    }
    g_menu_rows = 0;
}

static int g_menu_cols = 1;
static int g_menu_grid_rows = 1;
static int g_menu_colw = 4;

static void menu_layout(int len, rl_completions_t *c) {
    int last_input_row = last_row_of(len);
    int maxw = 0;
    for (int i = 0; i < c->count; i++) {
        int w = (int)strlen(c->items[i]) + (c->is_dir[i] ? 1 : 0);
        if (w > maxw) maxw = w;
    }
    int colw = maxw + 2;
    if (colw < 4) colw = 4;
    int cols = g_cols / colw;
    if (cols < 1) cols = 1;
    int rows = (c->count + cols - 1) / cols;
    int avail = g_rows - last_input_row - 1;
    if (avail < 1) avail = 1;
    if (rows > avail) rows = avail;
    g_menu_cols = cols;
    g_menu_grid_rows = rows;
    g_menu_colw = colw;
}

static void menu_draw(int len, int pos, rl_completions_t *c, int sel) {
    int last_input_row = last_row_of(len);
    g_menu_base_row = last_input_row;
    menu_layout(len, c);
    int cols = g_menu_cols, rows = g_menu_grid_rows, colw = g_menu_colw;

    for (int r = 0; r < rows; r++) {
        int row = last_input_row + 1 + r;
        if (row >= g_rows) break;
        vt_goto(row, 0);
        vt_eol();
        for (int col = 0; col < cols; col++) {
            int idx = col * rows + r;
            if (idx >= c->count) continue;
            const char *nm = c->items[idx];
            int is_sel = (idx == sel);
            if (is_sel) write(1, "\x1b[7m", 4);
            else if (c->is_dir[idx]) write(1, "\x1b[34m", 5);
            write(1, nm, strlen(nm));
            if (c->is_dir[idx]) write(1, "/", 1);
            write(1, "\x1b[0m", 4);
            int used = (int)strlen(nm) + (c->is_dir[idx] ? 1 : 0);
            for (int s = used; s < colw && (col + 1) < cols; s++) write(1, " ", 1);
        }
    }
    g_menu_rows = rows;
    if (g_input_color[0]) write(1, g_input_color, strlen(g_input_color));
    cursor_to(pos);
}

static void do_completion(char *buf, int *len, int *pos, int maxlen) {
    if (!g_complete_cb) return;

    static rl_completions_t c;
    c.count = 0; c.word_start = *pos;
    g_complete_cb(buf, *pos, &c);
    if (c.count == 0) return;

    int word_start = c.word_start;
    if (word_start < 0 || word_start > *pos) word_start = *pos;
    int cur_wlen = *pos - word_start;

    if (c.count == 1) {
        comp_replace_word(buf, len, pos, maxlen, word_start, c.items[0],
                          c.is_dir[0] ? 1 : 0);
        return;
    }

    int lcp = comp_common_prefix(&c);
    if (lcp > cur_wlen) {
        char pref[128];
        if (lcp > 127) lcp = 127;
        memcpy(pref, c.items[0], lcp);
        pref[lcp] = '\0';
        comp_replace_word(buf, len, pos, maxlen, word_start, pref, 0);
        return;
    }

    char orig[128];
    int ow = cur_wlen < 127 ? cur_wlen : 127;
    memcpy(orig, buf + word_start, ow);
    orig[ow] = '\0';

    int sel = 0;
    comp_replace_word(buf, len, pos, maxlen, word_start,
                      c.items[sel], c.is_dir[sel] ? 1 : 0);
    int sel_ws = word_start;
    menu_draw(*len, *pos, &c, sel);

    for (;;) {
        char ch;
        ssize_t rr = read(0, &ch, 1);
        if (rr <= 0) { cervus_nanosleep(10000000ULL); continue; }

        int accept = 0, cancel = 0, passthrough = 0;
        int dir = 0;
        char pc = 0;

        if (ch == '\t') dir = 1;
        else if (ch == '\n' || ch == '\r') accept = 1;
        else if (ch == 27) {
            char s0, s1;
            if (read(0, &s0, 1) <= 0) { cancel = 1; }
            else if (s0 != '[' && s0 != 'O') { cancel = 1; }
            else if (read(0, &s1, 1) <= 0) { cancel = 1; }
            else {
                if (s1 == 'B')      dir = 2;
                else if (s1 == 'A') dir = 3;
                else if (s1 == 'C') dir = 4;
                else if (s1 == 'D') dir = 5;
                else if (s1 == 'Z') dir = -1;
                else cancel = 1;
            }
        }
        else { passthrough = 1; pc = ch; }

        int rows = g_menu_grid_rows;
        if (dir == 1)       sel = (sel + 1) % c.count;
        else if (dir == -1) sel = (sel - 1 + c.count) % c.count;
        else if (dir == 2)  { sel = sel + 1; if (sel >= c.count) sel = c.count - 1; }
        else if (dir == 3)  { sel = sel - 1; if (sel < 0) sel = 0; }
        else if (dir == 4)  { if (sel + rows < c.count) sel += rows; }
        else if (dir == 5)  { if (sel - rows >= 0) sel -= rows; }

        if (accept) {
            menu_clear();
            cursor_to(*pos);
            return;
        }
        if (cancel) {
            menu_clear();
            comp_replace_word(buf, len, pos, maxlen, sel_ws, orig, 0);
            return;
        }
        if (passthrough) {
            menu_clear();
            cursor_to(*pos);
            if (pc == 8 || pc == 0x7F) {
                if (*pos > 0) {
                    int start = utf8_prev(buf, *pos);
                    int del = *pos - start;
                    int old = *len;
                    for (int i = start; i < *len - del; i++) buf[i] = buf[i + del];
                    *len -= del; *pos = start; buf[*len] = '\0';
                    redraw(buf, *pos, *len, old, *pos);
                }
                return;
            }
            if ((unsigned char)pc >= 0x20 && (unsigned char)pc != 0x7F) {
                char one[2] = { pc, 0 };
                insert_str(buf, len, pos, maxlen, one, 1);
            }
            return;
        }

        comp_replace_word(buf, len, pos, maxlen, sel_ws,
                          c.items[sel], c.is_dir[sel] ? 1 : 0);
        menu_draw(*len, *pos, &c, sel);
    }
}

static int readline_edit(char *buf, int maxlen) {
    g_rl_buf = buf;
    term_update_size();
    g_start_row = term_get_cursor_row() - g_prompt_len / g_cols;
    if (g_start_row < 0) g_start_row = 0;

    int len = 0, pos = 0, hidx = 0;
    int utf8_pending = 0;
    static char saved[RL_LINE_MAX];
    saved[0] = '\0'; buf[0] = '\0';

    for (;;) {
        if (g_suggest_cb) {
            const char *sug = NULL;
            cursor_to(len);
            vt_eol();
            if (pos == len && g_suggest_cb(buf, len, &sug) && sug && sug[0]) {
                write(1, "\x1b[90m", 5);
                write(1, sug, strlen(sug));
                write(1, "\x1b[0m", 4);
                if (g_input_color[0]) write(1, g_input_color, strlen(g_input_color));
            }
            cursor_to(pos);
        }
        char c;
        ssize_t rr = read(0, &c, 1);
        if (rr == 0) return -1;
        if (rr < 0) { cervus_nanosleep(10000000ULL); continue; }

        if (c == '\x1b') {
            char s[4];
            if (read(0, &s[0], 1) <= 0) continue;
            if (s[0] != '[') continue;
            if (read(0, &s[1], 1) <= 0) continue;
            if (s[1] == 'A') {
                if (hidx == 0) strncpy(saved, buf, RL_LINE_MAX - 1);
                if (hidx < g_hist_count) {
                    hidx++;
                    const char *h = readline_history_get(hidx);
                    if (h) {
                        int hl = (int)strlen(h);
                        if (hl > maxlen - 1) hl = maxlen - 1;
                        replace_line(buf, &len, &pos, h, hl);
                    }
                }
                continue;
            }
            if (s[1] == 'B') {
                if (hidx > 0) {
                    hidx--;
                    const char *h = hidx == 0 ? saved : readline_history_get(hidx);
                    if (!h) h = "";
                    int hl = (int)strlen(h);
                    if (hl > maxlen - 1) hl = maxlen - 1;
                    replace_line(buf, &len, &pos, h, hl);
                }
                continue;
            }
            if (s[1] == 'C') {
                if (pos < len) { pos = utf8_next(buf, len, pos); cursor_to(pos); }
                else if (g_suggest_cb) {
                    const char *sug = NULL;
                    if (g_suggest_cb(buf, len, &sug) && sug && sug[0]) {
                        int sl = (int)strlen(sug);
                        if (len + sl < maxlen - 1)
                            insert_str(buf, &len, &pos, maxlen, sug, sl);
                    }
                }
                continue;
            }
            if (s[1] == 'D') { if (pos > 0) { pos = utf8_prev(buf, pos); cursor_to(pos); } continue; }
            if (s[1] == 'H') { pos = 0; cursor_to(0); continue; }
            if (s[1] == 'F') { pos = len; cursor_to(len); continue; }
            if (s[1] >= '1' && s[1] <= '6') {
                char tilde; read(0, &tilde, 1);
                if (tilde != '~') continue;
                if (s[1] == '3' && pos < len) {
                    int old_len = len;
                    int nx = utf8_next(buf, len, pos);
                    int del = nx - pos;
                    for (int i = pos; i < len - del; i++) buf[i] = buf[i + del];
                    len -= del; buf[len] = '\0';
                    redraw(buf, pos, len, old_len, pos);
                } else if (s[1] == '1') { pos = 0; cursor_to(0); }
                else if (s[1] == '4') { pos = len; cursor_to(len); }
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            cursor_to(len); vt_eol();
            buf[len] = '\0';
            write(1, "\x1b[0m", 4);
            write(1, "\n", 1);
            return len;
        }
        if (c == 3) {
            cursor_to(len); vt_eol();
            write(1, "\x1b[0m^C\n", 7);
            buf[0] = '\0';
            return 0;
        }
        if (c == 4) {
            if (len == 0) return -1;
            if (pos < len) {
                int old_len = len;
                int nx = utf8_next(buf, len, pos);
                int del = nx - pos;
                for (int i = pos; i < len - del; i++) buf[i] = buf[i + del];
                len -= del; buf[len] = '\0';
                redraw(buf, pos, len, old_len, pos);
            }
            continue;
        }
        if (c == 1) { pos = 0; cursor_to(0); continue; }
        if (c == 5) { pos = len; cursor_to(len); continue; }
        if (c == 6) {
            if (pos == len && g_suggest_cb) {
                const char *sug = NULL;
                if (g_suggest_cb(buf, len, &sug) && sug && sug[0]) {
                    int sl = (int)strlen(sug);
                    if (len + sl < maxlen - 1) insert_str(buf, &len, &pos, maxlen, sug, sl);
                }
            } else if (pos < len) { pos = utf8_next(buf, len, pos); cursor_to(pos); }
            continue;
        }
        if (c == '\t') {
            do_completion(buf, &len, &pos, maxlen);
            continue;
        }
        if (c == 11) {
            if (pos < len) { int old_len = len; len = pos; buf[len] = '\0'; redraw(buf, pos, len, old_len, pos); }
            continue;
        }
        if (c == 21) {
            if (pos > 0) {
                int old_len = len, del = pos;
                for (int i = 0; i < len - del; i++) buf[i] = buf[i + del];
                len -= del; pos = 0; buf[len] = '\0';
                redraw(buf, 0, len, old_len, 0);
            }
            continue;
        }
        if (c == 23) {
            if (pos > 0) {
                int p = pos;
                while (p > 0 && buf[p - 1] == ' ') p--;
                while (p > 0 && buf[p - 1] != ' ') p--;
                int old_len = len, del = pos - p;
                for (int i = p; i < len - del; i++) buf[i] = buf[i + del];
                len -= del; pos = p; buf[len] = '\0';
                redraw(buf, p, len, old_len, p);
            }
            continue;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                int old_len = len;
                int start = utf8_prev(buf, pos);
                int del = pos - start;
                for (int i = start; i < len - del; i++) buf[i] = buf[i + del];
                len -= del; pos = start; buf[len] = '\0';
                redraw(buf, pos, len, old_len, pos);
            }
            continue;
        }
        if ((unsigned char)c >= 0x20 && (unsigned char)c != 0x7F) {
            if (len >= maxlen - 1) continue;
            for (int i = len; i > pos; i--) buf[i] = buf[i - 1];
            buf[pos] = c; len++; buf[len] = '\0';
            pos++;

            unsigned char uc = (unsigned char)c;
            if (uc >= 0xC0) {
                if (uc >= 0xF0)      utf8_pending = 3;
                else if (uc >= 0xE0) utf8_pending = 2;
                else                 utf8_pending = 1;
                continue;
            }
            if ((uc & 0xC0) == 0x80) {
                if (utf8_pending > 0) utf8_pending--;
                if (utf8_pending > 0) continue;
            }

            int start = pos - 1;
            while (start > 0 && ((unsigned char)buf[start] & 0xC0) == 0x80) start--;
            redraw(buf, start, len, len, pos);
        }
    }
}

static int prompt_visible_len(const char *p) {
    int n = 0;
    int esc = 0;
    for (const char *s = p; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (esc == 1) {
            if (ch == '[' || ch == ']') { esc = 2; continue; }
            esc = 0;
            continue;
        }
        if (esc == 2) {
            if (ch >= 0x40 && ch <= 0x7E) esc = 0;
            continue;
        }
        if (ch == 0x1B) { esc = 1; continue; }
        if (ch == '\r') { n = 0; continue; }
        if (ch < 0x20) continue;
        if (!utf8_is_cont(ch)) n++;
    }
    return n;
}

char *readline(const char *prompt) {
    struct termios orig, raw;
    int have_tio = (tcgetattr(0, &orig) == 0);
    if (have_tio) {
        raw = orig;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        tcsetattr(0, TCSANOW, &raw);
    }

    if (prompt && prompt[0]) {
        write(1, prompt, strlen(prompt));
        g_prompt_len = prompt_visible_len(prompt);
    } else {
        g_prompt_len = 0;
    }

    char *buf = malloc(RL_LINE_MAX);
    if (!buf) {
        if (have_tio) tcsetattr(0, TCSANOW, &orig);
        return NULL;
    }

    int r = readline_edit(buf, RL_LINE_MAX);

    if (have_tio) tcsetattr(0, TCSANOW, &orig);

    if (r < 0) { free(buf); return NULL; }
    return buf;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <cervus_util.h>

#define NEO_VERSION "0.1"
#define NEO_TABSTOP 4
#define NEO_QUIT_CONFIRM 1

#define KEY_NONE       0
#define KEY_ESC        0x1B
#define KEY_BACKSPACE  127
#define KEY_CTRL(k)    ((k) & 0x1f)

#define KEY_ARROW_UP    1000
#define KEY_ARROW_DOWN  1001
#define KEY_ARROW_LEFT  1002
#define KEY_ARROW_RIGHT 1003
#define KEY_HOME        1004
#define KEY_END         1005
#define KEY_DEL         1006
#define KEY_PAGE_UP     1007
#define KEY_PAGE_DOWN   1008

#define TIOCGWINSZ  0x5413

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } neo_winsize_t;

typedef struct {
    int    size;
    int    cap;
    char  *chars;
    int    rsize;
    char  *render;
} neo_row_t;

typedef struct {
    int        cx, cy;
    int        rx;
    int        rowoff;
    int        coloff;
    int        screenrows;
    int        screencols;
    int        numrows;
    int        rowscap;
    neo_row_t *row;
    int        dirty;
    char      *filename;
    char       statusmsg[256];
    int        statusmsg_visible;
    int        quit_pending;
    struct termios orig_termios;
} neo_t;

static neo_t E;
static const char *g_cwd = "/";

static void die(const char *msg)
{
    write(1, "\x1b[2J", 4);
    write(1, "\x1b[H", 3);
    if (msg) {
        write(2, "neo: ", 5);
        write(2, msg, strlen(msg));
        write(2, "\n", 1);
    }
    exit(1);
}

static void disable_raw_mode(void)
{
    tcsetattr(0, TCSAFLUSH, &E.orig_termios);
    write(1, "\x1b[?7h\x1b[?25h", 11);
}

static void enable_raw_mode(void)
{
    if (tcgetattr(0, &E.orig_termios) < 0) die("tcgetattr");
    atexit(disable_raw_mode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |=  (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &raw) < 0) die("tcsetattr");
    write(1, "\x1b[?7l", 5);
}

static int read_key(void)
{
    char c;
    ssize_t n;
    while ((n = read(0, &c, 1)) == 0) { }
    if (n < 0) return KEY_NONE;

    if (c != 0x1B) return (unsigned char)c;

    char seq[4];
    if (read(0, &seq[0], 1) != 1) return KEY_ESC;
    if (read(0, &seq[1], 1) != 1) return KEY_ESC;

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (read(0, &seq[2], 1) != 1) return KEY_ESC;
            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1':
                    case '7': return KEY_HOME;
                    case '3': return KEY_DEL;
                    case '4':
                    case '8': return KEY_END;
                    case '5': return KEY_PAGE_UP;
                    case '6': return KEY_PAGE_DOWN;
                }
            }
        } else {
            switch (seq[1]) {
                case 'A': return KEY_ARROW_UP;
                case 'B': return KEY_ARROW_DOWN;
                case 'C': return KEY_ARROW_RIGHT;
                case 'D': return KEY_ARROW_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
    } else if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
    }
    return KEY_ESC;
}

static void get_window_size(void)
{
    neo_winsize_t ws;
    if (syscall3(SYS_IOCTL, 1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        E.screencols = ws.ws_col;
        E.screenrows = ws.ws_row;
    } else {
        E.screencols = 80;
        E.screenrows = 24;
    }
    E.screenrows -= 2;
    if (E.screenrows < 1) E.screenrows = 1;
}

static int row_cx_to_rx(neo_row_t *row, int cx)
{
    int rx = 0;
    for (int j = 0; j < cx && j < row->size; j++) {
        if (row->chars[j] == '\t') rx += (NEO_TABSTOP - (rx % NEO_TABSTOP));
        else rx++;
    }
    return rx;
}

static int row_rx_to_cx(neo_row_t *row, int rx)
{
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t') cur_rx += (NEO_TABSTOP - (cur_rx % NEO_TABSTOP));
        else cur_rx++;
        if (cur_rx > rx) return cx;
    }
    return cx;
}

static void row_update(neo_row_t *row)
{
    int tabs = 0;
    for (int j = 0; j < row->size; j++) if (row->chars[j] == '\t') tabs++;
    free(row->render);
    row->render = malloc(row->size + tabs * (NEO_TABSTOP - 1) + 1);
    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % NEO_TABSTOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

static void rows_reserve(int want)
{
    if (want <= E.rowscap) return;
    int nc = E.rowscap ? E.rowscap * 2 : 32;
    while (nc < want) nc *= 2;
    neo_row_t *nr = malloc(sizeof(neo_row_t) * nc);
    if (!nr) die("out of memory");
    if (E.row) {
        memcpy(nr, E.row, sizeof(neo_row_t) * E.numrows);
    }
    for (int i = E.numrows; i < nc; i++) {
        nr[i].size = 0; nr[i].cap = 0; nr[i].chars = NULL;
        nr[i].rsize = 0; nr[i].render = NULL;
    }
    E.row = nr;
    E.rowscap = nc;
}

static void row_insert_at(int at, const char *s, int len)
{
    if (at < 0 || at > E.numrows) return;
    rows_reserve(E.numrows + 1);
    for (int i = E.numrows; i > at; i--) E.row[i] = E.row[i - 1];

    neo_row_t *r = &E.row[at];
    r->size = len;
    r->cap  = len + 1;
    r->chars = malloc(r->cap);
    if (!r->chars) die("out of memory");
    if (len > 0) memcpy(r->chars, s, len);
    r->chars[len] = '\0';
    r->render = NULL;
    r->rsize  = 0;
    row_update(r);
    E.numrows++;
    E.dirty = 1;
}

static void row_free(neo_row_t *r)
{
    free(r->chars);
    free(r->render);
    r->chars = NULL; r->render = NULL;
    r->size = 0; r->cap = 0; r->rsize = 0;
}

static void row_delete_at(int at)
{
    if (at < 0 || at >= E.numrows) return;
    row_free(&E.row[at]);
    for (int i = at; i < E.numrows - 1; i++) E.row[i] = E.row[i + 1];
    E.numrows--;
    E.dirty = 1;
}

static void row_reserve(neo_row_t *r, int want)
{
    if (want <= r->cap) return;
    int nc = r->cap ? r->cap * 2 : 16;
    while (nc < want) nc *= 2;
    char *nb = malloc(nc);
    if (!nb) die("out of memory");
    if (r->chars) memcpy(nb, r->chars, r->size);
    nb[r->size] = '\0';
    free(r->chars);
    r->chars = nb;
    r->cap = nc;
}

static void row_insert_char(neo_row_t *r, int at, int ch)
{
    if (at < 0 || at > r->size) at = r->size;
    row_reserve(r, r->size + 2);
    memmove(&r->chars[at + 1], &r->chars[at], r->size - at + 1);
    r->chars[at] = (char)ch;
    r->size++;
    row_update(r);
    E.dirty = 1;
}

static void row_append_string(neo_row_t *r, const char *s, int len)
{
    row_reserve(r, r->size + len + 1);
    memcpy(&r->chars[r->size], s, len);
    r->size += len;
    r->chars[r->size] = '\0';
    row_update(r);
    E.dirty = 1;
}

static void row_delete_char(neo_row_t *r, int at)
{
    if (at < 0 || at >= r->size) return;
    memmove(&r->chars[at], &r->chars[at + 1], r->size - at);
    r->size--;
    row_update(r);
    E.dirty = 1;
}

static void editor_insert_char(int ch)
{
    if (E.cy == E.numrows) row_insert_at(E.numrows, "", 0);
    row_insert_char(&E.row[E.cy], E.cx, ch);
    E.cx++;
}

static void editor_insert_newline(void)
{
    if (E.cx == 0) {
        row_insert_at(E.cy, "", 0);
    } else {
        neo_row_t *r = &E.row[E.cy];
        row_insert_at(E.cy + 1, &r->chars[E.cx], r->size - E.cx);
        r = &E.row[E.cy];
        r->size = E.cx;
        r->chars[r->size] = '\0';
        row_update(r);
    }
    E.cy++;
    E.cx = 0;
}

static void editor_delete_char(void)
{
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    neo_row_t *r = &E.row[E.cy];
    if (E.cx > 0) {
        row_delete_char(r, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        row_append_string(&E.row[E.cy - 1], r->chars, r->size);
        row_delete_at(E.cy);
        E.cy--;
    }
}

static char *rows_to_string(int *len)
{
    int total = 0;
    for (int j = 0; j < E.numrows; j++) total += E.row[j].size + 1;
    char *buf = malloc(total + 1);
    if (!buf) die("out of memory");
    char *p = buf;
    for (int j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p++ = '\n';
    }
    *p = '\0';
    *len = total;
    return buf;
}

static void set_status(const char *fmt, ...);

static void editor_open(const char *filename)
{
    free(E.filename);
    size_t fl = strlen(filename);
    E.filename = malloc(fl + 1);
    memcpy(E.filename, filename, fl + 1);

    char full[512];
    resolve_path(g_cwd, filename, full, sizeof(full));

    int fd = open(full, O_RDONLY, 0);
    if (fd < 0) {
        set_status("New file: %s", filename);
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return; }
    size_t sz = (size_t)st.st_size;
    char *buf = malloc(sz + 1);
    if (!buf) { close(fd); die("out of memory"); }

    size_t total = 0;
    while (total < sz) {
        ssize_t r = read(fd, buf + total, sz - total);
        if (r <= 0) break;
        total += (size_t)r;
    }
    close(fd);
    buf[total] = '\0';

    size_t i = 0;
    while (i < total) {
        size_t start = i;
        while (i < total && buf[i] != '\n' && buf[i] != '\r') i++;
        int len = (int)(i - start);
        row_insert_at(E.numrows, buf + start, len);
        if (i < total && buf[i] == '\r') i++;
        if (i < total && buf[i] == '\n') i++;
    }
    free(buf);
    E.dirty = 0;
}

static char *prompt(const char *prompt_fmt);

static int editor_save(void)
{
    if (!E.filename) {
        char *name = prompt("Save as (ESC to cancel): %s");
        if (!name) {
            set_status("Save cancelled");
            return -1;
        }
        if (name[0] == '\0') {
            free(name);
            set_status("Save cancelled (empty filename)");
            return -1;
        }
        E.filename = name;
    }
    int len = 0;
    char *buf = rows_to_string(&len);

    char full[512];
    resolve_path(g_cwd, E.filename, full, sizeof(full));

    int fd = open(full, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(buf);
        set_status("Save failed: errno=%d (%s)", errno, full);
        return -1;
    }
    ssize_t written = 0;
    while (written < len) {
        ssize_t w = write(fd, buf + written, len - written);
        if (w <= 0) { close(fd); free(buf); set_status("Save failed (write err)"); return -1; }
        written += w;
    }
    close(fd);
    free(buf);
    E.dirty = 0;
    set_status("Saved %d bytes to %s", len, E.filename);
    return 0;
}

typedef struct { char *b; int len; int cap; } abuf_t;

static void ab_append(abuf_t *ab, const char *s, int len)
{
    if (ab->len + len > ab->cap) {
        int nc = ab->cap ? ab->cap * 2 : 1024;
        while (nc < ab->len + len) nc *= 2;
        char *nb = malloc(nc);
        if (!nb) die("out of memory");
        if (ab->b) memcpy(nb, ab->b, ab->len);
        free(ab->b);
        ab->b = nb;
        ab->cap = nc;
    }
    memcpy(ab->b + ab->len, s, len);
    ab->len += len;
}
static void ab_free(abuf_t *ab) { free(ab->b); ab->b = NULL; ab->len = 0; ab->cap = 0; }

static void scroll(void)
{
    E.rx = 0;
    if (E.cy < E.numrows) E.rx = row_cx_to_rx(&E.row[E.cy], E.cx);

    if (E.cy < E.rowoff) E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;
    if (E.rx < E.coloff) E.coloff = E.rx;
    if (E.rx >= E.coloff + E.screencols) E.coloff = E.rx - E.screencols + 1;
}

static void draw_rows(abuf_t *ab)
{
    char pos[16];
    int limit = E.screencols - 1;
    if (limit < 1) limit = 1;
    for (int y = 0; y < E.screenrows; y++) {
        int n = snprintf(pos, sizeof(pos), "\x1b[%d;1H", y + 1);
        ab_append(ab, pos, n);

        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int wl = snprintf(welcome, sizeof(welcome),
                    "neo editor -- version %s -- press ESC to exit", NEO_VERSION);
                if (wl > limit) wl = limit;
                int padding = (limit - wl) / 2;
                if (padding > 0) { ab_append(ab, "~", 1); padding--; }
                while (padding-- > 0) ab_append(ab, " ", 1);
                ab_append(ab, welcome, wl);
            } else {
                ab_append(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > limit) len = limit;
            ab_append(ab, E.row[filerow].render + E.coloff, len);
        }
        ab_append(ab, "\x1b[K", 3);
    }
}

static void draw_status(abuf_t *ab)
{
    char pos[16];
    int n = snprintf(pos, sizeof(pos), "\x1b[%d;1H", E.screenrows + 1);
    ab_append(ab, pos, n);
    ab_append(ab, "\x1b[7m", 4);
    char status[256], rstatus[80];
    int len = snprintf(status, sizeof(status), " %.40s%s ",
        E.filename ? E.filename : "[No Name]",
        E.dirty ? " [modified]" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d ",
        E.cy + 1, E.numrows);

    int limit = E.screencols - 1;
    if (limit < 1) limit = 1;

    if (len > limit) len = limit;
    ab_append(ab, status, len);
    while (len < limit) {
        if (limit - len == rlen) { ab_append(ab, rstatus, rlen); break; }
        ab_append(ab, " ", 1);
        len++;
    }
    ab_append(ab, "\x1b[m", 3);
    ab_append(ab, "\x1b[K", 3);
}

static void draw_message(abuf_t *ab)
{
    char pos[16];
    int n = snprintf(pos, sizeof(pos), "\x1b[%d;1H", E.screenrows + 2);
    ab_append(ab, pos, n);
    ab_append(ab, "\x1b[K", 3);
    if (E.statusmsg_visible) {
        int mlen = strlen(E.statusmsg);
        if (mlen > E.screencols) mlen = E.screencols;
        ab_append(ab, E.statusmsg, mlen);
    } else {
        const char *hint = " ESC=exit  Ctrl-S=save  Ctrl-Q=quit  Ctrl-G=goto";
        int mlen = strlen(hint);
        if (mlen > E.screencols) mlen = E.screencols;
        ab_append(ab, hint, mlen);
    }
}

static void refresh_screen(void)
{
    scroll();
    abuf_t ab = {0};
    ab_append(&ab, "\x1b[?25l", 6);
    draw_rows(&ab);
    draw_status(&ab);
    draw_message(&ab);

    int cursor_row = (E.cy - E.rowoff) + 1;
    int cursor_col = (E.rx - E.coloff) + 1;

    char curbuf[32];
    int n = snprintf(curbuf, sizeof(curbuf), "\x1b[%d;%dH", cursor_row, cursor_col);
    ab_append(&ab, curbuf, n);

    ab_append(&ab, "\x1b[?25h", 6);

    write(1, ab.b, ab.len);
    ab_free(&ab);
}

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_visible = 1;
}

static char *prompt(const char *prompt_fmt)
{
    size_t bufcap = 128;
    size_t buflen = 0;
    char *buf = malloc(bufcap);
    buf[0] = '\0';

    for (;;) {
        set_status(prompt_fmt, buf);
        refresh_screen();
        int c = read_key();
        if (c == KEY_DEL || c == KEY_CTRL('h') || c == KEY_BACKSPACE) {
            if (buflen > 0) buf[--buflen] = '\0';
        } else if (c == KEY_ESC) {
            set_status("");
            E.statusmsg_visible = 0;
            free(buf);
            return NULL;
        } else if (c == '\r' || c == '\n') {
            if (buflen != 0) {
                set_status("");
                E.statusmsg_visible = 0;
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen + 1 >= bufcap) {
                bufcap *= 2;
                char *nb = malloc(bufcap);
                memcpy(nb, buf, buflen);
                free(buf);
                buf = nb;
            }
            buf[buflen++] = (char)c;
            buf[buflen] = '\0';
        }
    }
}

static void move_cursor(int key)
{
    neo_row_t *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) {
        case KEY_ARROW_LEFT:
            if (E.cx > 0) E.cx--;
            else if (E.cy > 0) { E.cy--; E.cx = E.row[E.cy].size; }
            break;
        case KEY_ARROW_RIGHT:
            if (row && E.cx < row->size) E.cx++;
            else if (row && E.cx == row->size) { E.cy++; E.cx = 0; }
            break;
        case KEY_ARROW_UP:
            if (E.cy > 0) E.cy--;
            break;
        case KEY_ARROW_DOWN:
            if (E.cy < E.numrows) E.cy++;
            break;
    }
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) E.cx = rowlen;
}

static void goto_line(void)
{
    char *p = prompt("Go to line: %s (ESC cancels)");
    if (!p) return;
    int n = atoi(p);
    free(p);
    if (n < 1) n = 1;
    if (n > E.numrows) n = E.numrows == 0 ? 1 : E.numrows;
    E.cy = n - 1;
    E.cx = 0;
}

static int process_key(void)
{
    int c = read_key();

    switch (c) {
        case '\r':
        case '\n':
            editor_insert_newline();
            break;

        case KEY_ESC:
            if (E.dirty && NEO_QUIT_CONFIRM && !E.quit_pending) {
                set_status("Unsaved changes. ESC again to exit without saving, Ctrl-S to save.");
                E.quit_pending = 1;
                return 1;
            }
            return 0;

        case KEY_CTRL('s'):
            editor_save();
            E.quit_pending = 0;
            break;

        case KEY_CTRL('q'):
            if (E.dirty && NEO_QUIT_CONFIRM && !E.quit_pending) {
                set_status("Unsaved changes. Ctrl-Q again to force quit.");
                E.quit_pending = 1;
                return 1;
            }
            return 0;

        case KEY_CTRL('g'):
            goto_line();
            break;

        case KEY_HOME:
            E.cx = 0;
            break;

        case KEY_END:
            if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
            break;

        case KEY_BACKSPACE:
        case KEY_CTRL('h'):
            editor_delete_char();
            break;

        case KEY_DEL:
            move_cursor(KEY_ARROW_RIGHT);
            editor_delete_char();
            break;

        case KEY_PAGE_UP:
        case KEY_PAGE_DOWN: {
            if (c == KEY_PAGE_UP) {
                E.cy = E.rowoff;
            } else {
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows) E.cy = E.numrows;
            }
            int times = E.screenrows;
            while (times--) move_cursor(c == KEY_PAGE_UP ? KEY_ARROW_UP : KEY_ARROW_DOWN);
            break;
        }

        case KEY_ARROW_UP:
        case KEY_ARROW_DOWN:
        case KEY_ARROW_LEFT:
        case KEY_ARROW_RIGHT:
            move_cursor(c);
            break;

        case KEY_CTRL('l'):
        case 0:
            break;

        default:
            if (c >= 32 && c < 127) editor_insert_char(c);
            else if (c == '\t')     editor_insert_char('\t');
            break;
    }
    E.quit_pending = 0;
    return 1;
}

static void init_editor(void)
{
    E.cx = 0; E.cy = 0; E.rx = 0;
    E.rowoff = 0; E.coloff = 0;
    E.numrows = 0; E.rowscap = 0; E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_visible = 0;
    E.quit_pending = 0;
    get_window_size();
}

int main(int argc, char **argv)
{
    g_cwd = get_cwd_flag(argc, argv);

    init_editor();
    enable_raw_mode();

    const char *file_to_open = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        file_to_open = argv[i];
        break;
    }

    if (file_to_open) editor_open(file_to_open);

    write(1, "\x1b[2J", 4);
    write(1, "\x1b[H", 3);

    refresh_screen();
    while (process_key()) {
        refresh_screen();
    }

    write(1, "\x1b[2J", 4);
    write(1, "\x1b[H", 3);
    disable_raw_mode();
    return 0;
}
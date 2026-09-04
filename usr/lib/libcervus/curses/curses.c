#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>
#include <sys/ioctl.h>

#define TIOCGWINSZ 0x5413

typedef struct { unsigned short ws_row, ws_col, ws_x, ws_y; } cw_winsize_t;

WINDOW *stdscr = NULL;
int LINES = 24, COLS = 80;

static chtype *g_virt;
static chtype *g_phys;
static int     g_rows, g_cols;
static int     g_started;
static int     g_ended;
static int     g_has_color;
static int     g_cursor_vis = 1;
static int     g_echo = 1;
static int     g_raw_mode;
static struct termios g_saved_tio;
static int     g_have_tio;

static short g_pair_fg[COLOR_PAIRS];
static short g_pair_bg[COLOR_PAIRS];
static int   g_pair_set[COLOR_PAIRS];

static int g_ungot = -1;

chtype COLOR_PAIR(int n) {
    return ((chtype)(n & CURSES_PAIR_MASK)) << CURSES_PAIR_SHIFT;
}

int PAIR_NUMBER(chtype a) {
    return (int)((a >> CURSES_PAIR_SHIFT) & CURSES_PAIR_MASK);
}

static chtype cell_of(chtype ch, attr_t at) {
    return (ch & A_CHARTEXT) | (at & ~A_CHARTEXT);
}

static void query_size(void) {
    cw_winsize_t ws;
    if (ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        LINES = ws.ws_row;
        COLS  = ws.ws_col;
    }
}

static void tio_apply(void) {
    if (!g_have_tio) return;
    struct termios t = g_saved_tio;
    if (g_raw_mode) t.c_lflag &= ~(ICANON | ISIG);
    else            t.c_lflag |= ICANON;
    if (g_echo) t.c_lflag |= ECHO;
    else        t.c_lflag &= ~ECHO;
    tcsetattr(0, TCSAFLUSH, &t);
}

WINDOW *newwin(int h, int w, int y, int x) {
    if (h <= 0) h = LINES - y;
    if (w <= 0) w = COLS - x;
    if (h <= 0 || w <= 0) return NULL;

    WINDOW *win = calloc(1, sizeof *win);
    if (!win) return NULL;
    win->cells = malloc((size_t)h * w * sizeof(chtype));
    if (!win->cells) { free(win); return NULL; }

    win->y = y; win->x = x;
    win->h = h; win->w = w;
    win->attr = A_NORMAL;
    win->bkgd = ' ';
    win->delay = -1;
    for (int i = 0; i < h * w; i++) win->cells[i] = cell_of(' ', A_NORMAL);
    return win;
}

int delwin(WINDOW *w) {
    if (!w) return ERR;
    free(w->cells);
    free(w);
    return OK;
}

int mvwin(WINDOW *w, int y, int x) {
    if (!w) return ERR;
    w->y = y; w->x = x;
    return OK;
}

WINDOW *initscr(void) {
    if (g_started) return stdscr;

    if (tcgetattr(0, &g_saved_tio) == 0) g_have_tio = 1;
    query_size();

    g_rows = LINES;
    g_cols = COLS;
    g_virt = malloc((size_t)g_rows * g_cols * sizeof(chtype));
    g_phys = malloc((size_t)g_rows * g_cols * sizeof(chtype));
    if (!g_virt || !g_phys) { free(g_virt); free(g_phys); return NULL; }
    for (int i = 0; i < g_rows * g_cols; i++) {
        g_virt[i] = cell_of(' ', A_NORMAL);
        g_phys[i] = cell_of(0, CURSES_ATTR_MASK);
    }

    stdscr = newwin(g_rows, g_cols, 0, 0);
    if (!stdscr) return NULL;

    g_raw_mode = 1;
    g_echo = 0;
    tio_apply();

    fputs("\x1b[?1049h\x1b[2J\x1b[H", stdout);
    fflush(stdout);
    g_started = 1;
    g_ended = 0;
    return stdscr;
}

int endwin(void) {
    if (!g_started || g_ended) return ERR;
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    if (g_have_tio) tcsetattr(0, TCSAFLUSH, &g_saved_tio);
    g_ended = 1;
    return OK;
}

int isendwin(void) { return g_ended; }

const char *curses_version(void) { return "cervus-curses 1.0"; }

int cbreak(void)   { g_raw_mode = 1; tio_apply(); return OK; }
int nocbreak(void) { g_raw_mode = 0; tio_apply(); return OK; }
int raw(void)      { return cbreak(); }
int noraw(void)    { return nocbreak(); }
int echo(void)     { g_echo = 1; tio_apply(); return OK; }
int noecho(void)   { g_echo = 0; tio_apply(); return OK; }
int nl(void)       { return OK; }
int nonl(void)     { return OK; }

int curs_set(int v) {
    int old = g_cursor_vis;
    g_cursor_vis = v;
    fputs(v ? "\x1b[?25h" : "\x1b[?25l", stdout);
    fflush(stdout);
    return old;
}

int keypad(WINDOW *w, bool_t on)  { if (!w) return ERR; w->keypad = on; return OK; }
int scrollok(WINDOW *w, bool_t on){ if (!w) return ERR; w->scroll = on; return OK; }
int clearok(WINDOW *w, bool_t on) { if (!w) return ERR; w->clearok = on; return OK; }
int nodelay(WINDOW *w, bool_t on) { if (!w) return ERR; w->delay = on ? 0 : -1; return OK; }
int wtimeout(WINDOW *w, int d)    { if (!w) return ERR; w->delay = d; return OK; }
int timeout(int d)                { return wtimeout(stdscr, d); }
int halfdelay(int tenths)         { return wtimeout(stdscr, tenths * 100); }

int start_color(void) { g_has_color = 1; return OK; }
int has_colors(void)  { return 1; }

int init_pair(short pair, short fg, short bg) {
    if (pair < 1 || pair >= COLOR_PAIRS) return ERR;
    g_pair_fg[pair] = fg;
    g_pair_bg[pair] = bg;
    g_pair_set[pair] = 1;
    return OK;
}

int wmove(WINDOW *w, int y, int x) {
    if (!w || y < 0 || x < 0 || y >= w->h || x >= w->w) return ERR;
    w->cury = y; w->curx = x;
    return OK;
}
int move(int y, int x) { return wmove(stdscr, y, x); }

int getcury(WINDOW *w) { return w ? w->cury : ERR; }
int getcurx(WINDOW *w) { return w ? w->curx : ERR; }
int getmaxy(WINDOW *w) { return w ? w->h : ERR; }
int getmaxx(WINDOW *w) { return w ? w->w : ERR; }
int getbegy(WINDOW *w) { return w ? w->y : ERR; }
int getbegx(WINDOW *w) { return w ? w->x : ERR; }

int wattron(WINDOW *w, attr_t a)  { if (!w) return ERR; w->attr |= (a & ~A_CHARTEXT); return OK; }
int wattroff(WINDOW *w, attr_t a) { if (!w) return ERR; w->attr &= ~(a & ~A_CHARTEXT); return OK; }
int wattrset(WINDOW *w, attr_t a) { if (!w) return ERR; w->attr = a & ~A_CHARTEXT; return OK; }
int attron(attr_t a)  { return wattron(stdscr, a); }
int attroff(attr_t a) { return wattroff(stdscr, a); }
int attrset(attr_t a) { return wattrset(stdscr, a); }

int wcolor_set(WINDOW *w, short pair, void *opts) {
    (void)opts;
    if (!w) return ERR;
    w->attr = (w->attr & ~(CURSES_PAIR_MASK << CURSES_PAIR_SHIFT)) | COLOR_PAIR(pair);
    return OK;
}
int color_set(short pair, void *opts) { return wcolor_set(stdscr, pair, opts); }

int wbkgd(WINDOW *w, chtype c) {
    if (!w) return ERR;
    w->bkgd = c;
    for (int i = 0; i < w->h * w->w; i++) {
        chtype ch = w->cells[i] & A_CHARTEXT;
        if (ch == ' ') w->cells[i] = cell_of(c & A_CHARTEXT ? (c & A_CHARTEXT) : ' ', c);
        else           w->cells[i] = cell_of(ch, (w->cells[i] & ~A_CHARTEXT) | (c & ~A_CHARTEXT));
    }
    return OK;
}
int bkgd(chtype c) { return wbkgd(stdscr, c); }

static void scroll_up(WINDOW *w) {
    memmove(w->cells, w->cells + w->w, (size_t)(w->h - 1) * w->w * sizeof(chtype));
    chtype fill = cell_of(' ', w->attr);
    for (int i = 0; i < w->w; i++) w->cells[(w->h - 1) * w->w + i] = fill;
}

static void advance(WINDOW *w) {
    if (++w->curx < w->w) return;
    w->curx = 0;
    if (++w->cury < w->h) return;
    if (w->scroll) { scroll_up(w); w->cury = w->h - 1; }
    else           w->cury = w->h - 1;
}

int waddch(WINDOW *w, chtype c) {
    if (!w) return ERR;
    unsigned ch = c & A_CHARTEXT;
    attr_t at = (c & ~A_CHARTEXT) | w->attr;

    if (ch == '\n') {
        w->curx = 0;
        if (++w->cury >= w->h) {
            if (w->scroll) { scroll_up(w); w->cury = w->h - 1; }
            else           w->cury = w->h - 1;
        }
        return OK;
    }
    if (ch == '\r') { w->curx = 0; return OK; }
    if (ch == '\t') {
        do { waddch(w, ' '); } while (w->curx % 8);
        return OK;
    }
    if (ch < 32) ch = ' ';

    w->cells[w->cury * w->w + w->curx] = cell_of(ch, at);
    advance(w);
    return OK;
}
int addch(chtype c) { return waddch(stdscr, c); }
int mvwaddch(WINDOW *w, int y, int x, chtype c) {
    if (wmove(w, y, x) == ERR) return ERR;
    return waddch(w, c);
}
int mvaddch(int y, int x, chtype c) { return mvwaddch(stdscr, y, x, c); }

int waddnstr(WINDOW *w, const char *s, int n) {
    if (!w || !s) return ERR;
    for (int i = 0; s[i] && (n < 0 || i < n); i++) waddch(w, (unsigned char)s[i]);
    return OK;
}
int waddstr(WINDOW *w, const char *s) { return waddnstr(w, s, -1); }
int addstr(const char *s)             { return waddnstr(stdscr, s, -1); }
int addnstr(const char *s, int n)     { return waddnstr(stdscr, s, n); }
int mvwaddstr(WINDOW *w, int y, int x, const char *s) {
    if (wmove(w, y, x) == ERR) return ERR;
    return waddnstr(w, s, -1);
}
int mvwaddnstr(WINDOW *w, int y, int x, const char *s, int n) {
    if (wmove(w, y, x) == ERR) return ERR;
    return waddnstr(w, s, n);
}
int mvaddstr(int y, int x, const char *s) { return mvwaddstr(stdscr, y, x, s); }

int vw_printw(WINDOW *w, const char *fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    waddnstr(w, buf, -1);
    return n;
}
int wprintw(WINDOW *w, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vw_printw(w, fmt, ap);
    va_end(ap); return n;
}
int printw(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vw_printw(stdscr, fmt, ap);
    va_end(ap); return n;
}
int mvwprintw(WINDOW *w, int y, int x, const char *fmt, ...) {
    if (wmove(w, y, x) == ERR) return ERR;
    va_list ap; va_start(ap, fmt);
    int n = vw_printw(w, fmt, ap);
    va_end(ap); return n;
}
int mvprintw(int y, int x, const char *fmt, ...) {
    if (wmove(stdscr, y, x) == ERR) return ERR;
    va_list ap; va_start(ap, fmt);
    int n = vw_printw(stdscr, fmt, ap);
    va_end(ap); return n;
}

int werase(WINDOW *w) {
    if (!w) return ERR;
    chtype fill = cell_of(' ', w->bkgd & ~A_CHARTEXT);
    for (int i = 0; i < w->h * w->w; i++) w->cells[i] = fill;
    w->cury = w->curx = 0;
    return OK;
}
int erase(void) { return werase(stdscr); }
int wclear(WINDOW *w) { if (!w) return ERR; w->clearok = 1; return werase(w); }
int clear(void) { return wclear(stdscr); }

int wclrtoeol(WINDOW *w) {
    if (!w) return ERR;
    chtype fill = cell_of(' ', w->bkgd & ~A_CHARTEXT);
    for (int x = w->curx; x < w->w; x++) w->cells[w->cury * w->w + x] = fill;
    return OK;
}
int clrtoeol(void) { return wclrtoeol(stdscr); }

int wclrtobot(WINDOW *w) {
    if (!w) return ERR;
    chtype fill = cell_of(' ', w->bkgd & ~A_CHARTEXT);
    for (int x = w->curx; x < w->w; x++) w->cells[w->cury * w->w + x] = fill;
    for (int y = w->cury + 1; y < w->h; y++)
        for (int x = 0; x < w->w; x++) w->cells[y * w->w + x] = fill;
    return OK;
}
int clrtobot(void) { return wclrtobot(stdscr); }

int wborder(WINDOW *w, chtype ls, chtype rs, chtype ts, chtype bs,
            chtype tl, chtype tr, chtype bl, chtype br) {
    if (!w || w->h < 2 || w->w < 2) return ERR;
    if (!ls) ls = ACS_VLINE;
    if (!rs) rs = ACS_VLINE;
    if (!ts) ts = ACS_HLINE;
    if (!bs) bs = ACS_HLINE;
    if (!tl) tl = ACS_ULCORNER;
    if (!tr) tr = ACS_URCORNER;
    if (!bl) bl = ACS_LLCORNER;
    if (!br) br = ACS_LRCORNER;

    for (int x = 1; x < w->w - 1; x++) {
        mvwaddch(w, 0, x, ts);
        mvwaddch(w, w->h - 1, x, bs);
    }
    for (int y = 1; y < w->h - 1; y++) {
        mvwaddch(w, y, 0, ls);
        mvwaddch(w, y, w->w - 1, rs);
    }
    mvwaddch(w, 0, 0, tl);
    mvwaddch(w, 0, w->w - 1, tr);
    mvwaddch(w, w->h - 1, 0, bl);
    mvwaddch(w, w->h - 1, w->w - 1, br);
    return OK;
}
int box(WINDOW *w, chtype v, chtype h) {
    return wborder(w, v, v, h, h, 0, 0, 0, 0);
}

int whline(WINDOW *w, chtype c, int n) {
    if (!w) return ERR;
    if (!c) c = ACS_HLINE;
    int y = w->cury, x0 = w->curx;
    for (int i = 0; i < n && x0 + i < w->w; i++)
        w->cells[y * w->w + x0 + i] = cell_of(c & A_CHARTEXT, (c & ~A_CHARTEXT) | w->attr);
    return OK;
}
int wvline(WINDOW *w, chtype c, int n) {
    if (!w) return ERR;
    if (!c) c = ACS_VLINE;
    int y0 = w->cury, x = w->curx;
    for (int i = 0; i < n && y0 + i < w->h; i++)
        w->cells[(y0 + i) * w->w + x] = cell_of(c & A_CHARTEXT, (c & ~A_CHARTEXT) | w->attr);
    return OK;
}
int hline(chtype c, int n) { return whline(stdscr, c, n); }
int vline(chtype c, int n) { return wvline(stdscr, c, n); }
int mvwhline(WINDOW *w, int y, int x, chtype c, int n) {
    if (wmove(w, y, x) == ERR) return ERR;
    return whline(w, c, n);
}
int mvwvline(WINDOW *w, int y, int x, chtype c, int n) {
    if (wmove(w, y, x) == ERR) return ERR;
    return wvline(w, c, n);
}

int wnoutrefresh(WINDOW *w) {
    if (!w || !g_virt) return ERR;
    for (int y = 0; y < w->h; y++) {
        int sy = w->y + y;
        if (sy < 0 || sy >= g_rows) continue;
        for (int x = 0; x < w->w; x++) {
            int sx = w->x + x;
            if (sx < 0 || sx >= g_cols) continue;
            g_virt[sy * g_cols + sx] = w->cells[y * w->w + x];
        }
    }
    if (w->clearok) {
        for (int i = 0; i < g_rows * g_cols; i++) g_phys[i] = cell_of(0, CURSES_ATTR_MASK);
        w->clearok = 0;
    }
    return OK;
}

static void emit_attr(attr_t a) {
    char seq[64];
    int n = 0;
    n += snprintf(seq + n, sizeof seq - n, "\x1b[0");
    if (a & A_BOLD)      n += snprintf(seq + n, sizeof seq - n, ";1");
    if (a & A_DIM)       n += snprintf(seq + n, sizeof seq - n, ";2");
    if (a & A_UNDERLINE) n += snprintf(seq + n, sizeof seq - n, ";4");
    if (a & A_BLINK)     n += snprintf(seq + n, sizeof seq - n, ";5");
    if (a & A_REVERSE)   n += snprintf(seq + n, sizeof seq - n, ";7");

    int p = PAIR_NUMBER(a);
    if (g_has_color && p > 0 && p < COLOR_PAIRS && g_pair_set[p]) {
        n += snprintf(seq + n, sizeof seq - n, ";%d;%d",
                      30 + (g_pair_fg[p] & 7), 40 + (g_pair_bg[p] & 7));
    }
    snprintf(seq + n, sizeof seq - n, "m");
    fputs(seq, stdout);
}

int doupdate(void) {
    if (!g_virt) return ERR;
    attr_t cur = 0xFFFFFFFFu;
    int cy = -1, cx = -1;

    fputs("\x1b[?25l", stdout);
    for (int y = 0; y < g_rows; y++) {
        for (int x = 0; x < g_cols; x++) {
            int i = y * g_cols + x;
            if (g_virt[i] == g_phys[i]) continue;
            if (y == g_rows - 1 && x == g_cols - 1) { g_phys[i] = g_virt[i]; continue; }

            if (cy != y || cx != x) {
                printf("\x1b[%d;%dH", y + 1, x + 1);
                cy = y; cx = x;
            }
            attr_t a = g_virt[i] & ~A_CHARTEXT;
            if (a != cur) { emit_attr(a); cur = a; }

            unsigned ch = g_virt[i] & A_CHARTEXT;
            fputc(ch ? (int)ch : ' ', stdout);
            g_phys[i] = g_virt[i];
            cx++;
        }
    }
    fputs("\x1b[0m", stdout);
    if (stdscr) printf("\x1b[%d;%dH", stdscr->y + stdscr->cury + 1,
                                      stdscr->x + stdscr->curx + 1);
    if (g_cursor_vis) fputs("\x1b[?25h", stdout);
    fflush(stdout);
    return OK;
}

int wrefresh(WINDOW *w) {
    if (wnoutrefresh(w) == ERR) return ERR;
    return doupdate();
}
int refresh(void) { return wrefresh(stdscr); }

int redrawwin(WINDOW *w) {
    if (!w) return ERR;
    w->clearok = 1;
    return OK;
}

static unsigned char g_ib[64];
static int g_iblen, g_ibpos;
static int g_intr;

static int fill(int delay) {
    if (g_ibpos < g_iblen) return 1;
    if (delay >= 0) {
        struct pollfd p = { 0, POLLIN, 0 };
        int r = poll(&p, 1, delay);
        if (r == 0) return 0;
        if (r < 0) { if (errno == EINTR) g_intr = 1; return 0; }
    }
    ssize_t n = read(0, g_ib, sizeof g_ib);
    if (n <= 0) {
        if (n < 0 && errno == EINTR) g_intr = 1;
        g_iblen = g_ibpos = 0;
        return 0;
    }
    g_iblen = (int)n; g_ibpos = 0;
    return 1;
}

static int nextc(int delay) {
    if (!fill(delay)) return -1;
    return g_ib[g_ibpos++];
}

static int peek_ready(void) {
    if (g_ibpos < g_iblen) return 1;
    struct pollfd p = { 0, POLLIN, 0 };
    return poll(&p, 1, 25) > 0;
}

int ungetch(int c) { g_ungot = c; return OK; }

int wgetch(WINDOW *w) {
    if (g_ungot >= 0) { int c = g_ungot; g_ungot = -1; return c; }

    int delay = w ? w->delay : -1;
    g_intr = 0;
    int c = nextc(delay);
    if (c < 0) return g_intr ? KEY_RESIZE : ERR;

    if (c == '\n' || c == '\r') return (w && w->keypad) ? KEY_ENTER : '\n';
    if (c == 8 || c == 127)     return (w && w->keypad) ? KEY_BACKSPACE : c;
    if (c != 0x1B)              return c;

    if (!peek_ready()) return 27;
    int s0 = nextc(0);
    if (s0 < 0) return 27;
    if (s0 != '[' && s0 != 'O') return 27;
    if (!peek_ready()) return 27;
    int s1 = nextc(0);
    if (s1 < 0) return 27;

    if (s0 == 'O') {
        switch (s1) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
            case 'P': return KEY_F(1);
            case 'Q': return KEY_F(2);
            case 'R': return KEY_F(3);
            case 'S': return KEY_F(4);
            default:  return 27;
        }
    }

    if (s1 >= '0' && s1 <= '9') {
        int num = s1 - '0';
        for (;;) {
            if (!peek_ready()) return 27;
            int d = nextc(0);
            if (d < 0) return 27;
            if (d >= '0' && d <= '9') { num = num * 10 + (d - '0'); continue; }
            if (d == '~') break;
            return 27;
        }
        switch (num) {
            case 1: case 7:  return KEY_HOME;
            case 2:          return KEY_IC;
            case 3:          return KEY_DC;
            case 4: case 8:  return KEY_END;
            case 5:          return KEY_PPAGE;
            case 6:          return KEY_NPAGE;
            case 11: return KEY_F(1);  case 12: return KEY_F(2);
            case 13: return KEY_F(3);  case 14: return KEY_F(4);
            case 15: return KEY_F(5);  case 17: return KEY_F(6);
            case 18: return KEY_F(7);  case 19: return KEY_F(8);
            case 20: return KEY_F(9);  case 21: return KEY_F(10);
            case 23: return KEY_F(11); case 24: return KEY_F(12);
            default: return 27;
        }
    }

    switch (s1) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        default:  return 27;
    }
}
int getch(void) { return wgetch(stdscr); }

int wgetnstr(WINDOW *w, char *s, int n) {
    if (!w || !s || n <= 0) return ERR;
    int len = 0;
    int save_delay = w->delay;
    int y = w->cury, x = w->curx;
    w->delay = -1;
    s[0] = 0;
    for (;;) {
        mvwaddnstr(w, y, x, s, -1);
        for (int i = len; i < n - 1; i++) waddch(w, ' ');
        wmove(w, y, x + len);
        wrefresh(w);

        int c = wgetch(w);
        if (c == KEY_ENTER || c == '\n') break;
        if (c == KEY_BACKSPACE || c == 8 || c == 127) {
            if (len > 0) s[--len] = 0;
            continue;
        }
        if (c == 27) { w->delay = save_delay; return ERR; }
        if (c >= 32 && c < 127 && len < n - 1) { s[len++] = (char)c; s[len] = 0; }
    }
    w->delay = save_delay;
    return OK;
}
int getnstr(char *s, int n) { return wgetnstr(stdscr, s, n); }
int mvwgetnstr(WINDOW *w, int y, int x, char *s, int n) {
    if (wmove(w, y, x) == ERR) return ERR;
    return wgetnstr(w, s, n);
}

int napms(int ms) { usleep((unsigned)ms * 1000u); return OK; }
int beep(void)  { fputc('\a', stdout); fflush(stdout); return OK; }
int flash(void) { return beep(); }

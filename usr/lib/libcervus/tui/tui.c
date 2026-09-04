#include <tui.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <termios.h>
#include <poll.h>
#include <sys/ioctl.h>

static struct termios g_saved;
static int g_raw = 0;

void tui_begin(void) {
    struct termios raw;
    if (tcgetattr(0, &g_saved) == 0) {
        raw = g_saved;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |=  (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSAFLUSH, &raw);
        g_raw = 1;
    }
    fputs("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

void tui_end(void) {
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    if (g_raw) {
        tcsetattr(0, TCSAFLUSH, &g_saved);
        g_raw = 0;
    }
}

void tui_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        if (rows) *rows = ws.ws_row;
        if (cols) *cols = ws.ws_col;
        return;
    }
    if (rows) *rows = 24;
    if (cols) *cols = 80;
}

void tui_clear(void)       { fputs("\x1b[2J\x1b[H", stdout); }
void tui_home(void)        { fputs("\x1b[H", stdout); }
void tui_hide_cursor(void) { fputs("\x1b[?25l", stdout); }
void tui_show_cursor(void) { fputs("\x1b[?25h", stdout); }
void tui_puts(const char *s) { if (s) fputs(s, stdout); }

void tui_move(int row, int col) {
    printf("\x1b[%d;%dH", row, col);
}

static unsigned char g_ib[256];
static int g_iblen = 0, g_ibpos = 0;

static int g_intr = 0;

static int ib_fill(void) {
    ssize_t n = read(0, g_ib, sizeof g_ib);
    if (n <= 0) {
        g_iblen = 0;
        g_ibpos = 0;
        if (n < 0 && errno == EINTR) g_intr = 1;
        return 0;
    }
    g_iblen = (int)n;
    g_ibpos = 0;
    return (int)n;
}

static int ib_getc(void) {
    while (g_ibpos >= g_iblen) if (!ib_fill()) return -1;
    return g_ib[g_ibpos++];
}

static int ib_more(void) {
    if (g_ibpos < g_iblen) return 1;
    struct pollfd p = { 0, POLLIN, 0 };
    if (poll(&p, 1, 30) > 0 && (p.revents & POLLIN)) return ib_fill() > 0;
    return 0;
}

int tui_read_key(void) {
    g_intr = 0;
    int c = ib_getc();
    if (c < 0) return g_intr ? TK_RESIZE : -1;
    if (c != 0x1B) {
        if (c == '\n' || c == '\r') return TK_ENTER;
        if (c == 8 || c == 127)     return TK_BACKSP;
        if (c == 12)                return TK_RESIZE;
        return c;
    }

    if (!ib_more()) return TK_ESC;
    int s0 = ib_getc();
    if (s0 != '[' && s0 != 'O') return TK_ESC;
    if (!ib_more()) return TK_ESC;
    int s1 = ib_getc();

    if (s0 == '[') {
        if (s1 >= '0' && s1 <= '9') {
            if (!ib_more()) return TK_ESC;
            int s2 = ib_getc();
            if (s2 == '~') {
                switch (s1) {
                    case '1': case '7': return TK_HOME;
                    case '3':           return TK_DEL;
                    case '4': case '8': return TK_END;
                    case '5':           return TK_PGUP;
                    case '6':           return TK_PGDN;
                }
            }
            return TK_ESC;
        }
        switch (s1) {
            case 'A': return TK_UP;
            case 'B': return TK_DOWN;
            case 'C': return TK_RIGHT;
            case 'D': return TK_LEFT;
            case 'H': return TK_HOME;
            case 'F': return TK_END;
        }
        return TK_ESC;
    }
    switch (s1) {
        case 'H': return TK_HOME;
        case 'F': return TK_END;
    }
    return TK_ESC;
}

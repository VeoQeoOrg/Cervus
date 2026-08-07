#include <tui.h>
#include <unistd.h>
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

static int tui_esc_has_more(void) {
    struct pollfd p = { 0, POLLIN, 0 };
    return poll(&p, 1, 40) > 0 && (p.revents & POLLIN);
}

int tui_read_key(void) {
    char c;
    ssize_t n;
    while ((n = read(0, &c, 1)) == 0) { }
    if (n < 0) return -1;
    if (c != 0x1B) {
        unsigned char uc = (unsigned char)c;
        if (uc == '\n' || uc == '\r') return TK_ENTER;
        if (uc == 8 || uc == 127)     return TK_BACKSP;
        return uc;
    }

    char seq[3];
    if (!tui_esc_has_more() || read(0, &seq[0], 1) != 1) return TK_ESC;
    if (!tui_esc_has_more() || read(0, &seq[1], 1) != 1) return TK_ESC;

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (!tui_esc_has_more() || read(0, &seq[2], 1) != 1) return TK_ESC;
            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': case '7': return TK_HOME;
                    case '3':           return TK_DEL;
                    case '4': case '8': return TK_END;
                    case '5':           return TK_PGUP;
                    case '6':           return TK_PGDN;
                }
            }
        } else {
            switch (seq[1]) {
                case 'A': return TK_UP;
                case 'B': return TK_DOWN;
                case 'C': return TK_RIGHT;
                case 'D': return TK_LEFT;
                case 'H': return TK_HOME;
                case 'F': return TK_END;
            }
        }
    } else if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return TK_HOME;
            case 'F': return TK_END;
        }
    }
    return TK_ESC;
}

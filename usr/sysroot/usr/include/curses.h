#ifndef _CURSES_H
#define _CURSES_H

#include <stdint.h>
#include <stdarg.h>

typedef unsigned int chtype;
typedef unsigned int attr_t;
typedef int bool_t;

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#define ERR (-1)
#define OK  (0)

#define A_NORMAL     0x00000000u
#define A_BOLD       0x00010000u
#define A_DIM        0x00020000u
#define A_UNDERLINE  0x00040000u
#define A_REVERSE    0x00080000u
#define A_BLINK      0x00100000u
#define A_STANDOUT   A_REVERSE
#define A_COLOR      0x0000FF00u
#define A_ATTRIBUTES 0xFFFFFF00u
#define A_CHARTEXT   0x000000FFu

#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7

#define CURSES_PAIR_SHIFT 8
#define CURSES_PAIR_MASK  0x000000FFu
#define CURSES_ATTR_MASK  0xFFFFFF00u
#define COLOR_PAIRS 64

chtype COLOR_PAIR(int n);
int    PAIR_NUMBER(chtype a);

#define ACS_HLINE    '-'
#define ACS_VLINE    '|'
#define ACS_ULCORNER '+'
#define ACS_URCORNER '+'
#define ACS_LLCORNER '+'
#define ACS_LRCORNER '+'
#define ACS_LTEE     '+'
#define ACS_RTEE     '+'
#define ACS_TTEE     '+'
#define ACS_BTEE     '+'
#define ACS_PLUS     '+'
#define ACS_CKBOARD  '#'
#define ACS_BLOCK    '#'
#define ACS_DIAMOND  '*'
#define ACS_BULLET   'o'
#define ACS_LARROW   '<'
#define ACS_RARROW   '>'
#define ACS_UARROW   '^'
#define ACS_DARROW   'v'

#define KEY_CODE_YES 0x100
#define KEY_DOWN     0x102
#define KEY_UP       0x103
#define KEY_LEFT     0x104
#define KEY_RIGHT    0x105
#define KEY_HOME     0x106
#define KEY_BACKSPACE 0x107
#define KEY_F0       0x108
#define KEY_F(n)     (KEY_F0 + (n))
#define KEY_DC       0x14A
#define KEY_IC       0x14B
#define KEY_NPAGE    0x152
#define KEY_PPAGE    0x153
#define KEY_ENTER    0x157
#define KEY_END      0x168
#define KEY_RESIZE   0x19A

typedef struct _win {
    int     y, x;
    int     h, w;
    int     cury, curx;
    attr_t  attr;
    chtype  bkgd;
    int     scroll;
    int     keypad;
    int     delay;
    int     clearok;
    chtype *cells;
} WINDOW;

extern WINDOW *stdscr;
extern int LINES, COLS;

WINDOW *initscr(void);
int     endwin(void);
int     isendwin(void);
const char *curses_version(void);

int  cbreak(void);
int  nocbreak(void);
int  raw(void);
int  noraw(void);
int  echo(void);
int  noecho(void);
int  nl(void);
int  nonl(void);
int  curs_set(int visibility);
int  keypad(WINDOW *w, bool_t on);
int  nodelay(WINDOW *w, bool_t on);
int  wtimeout(WINDOW *w, int delay);
int  timeout(int delay);
int  halfdelay(int tenths);
int  scrollok(WINDOW *w, bool_t on);
int  clearok(WINDOW *w, bool_t on);

int  start_color(void);
int  has_colors(void);
int  init_pair(short pair, short fg, short bg);

WINDOW *newwin(int h, int w, int y, int x);
int     delwin(WINDOW *w);
int     mvwin(WINDOW *w, int y, int x);

int refresh(void);
int wrefresh(WINDOW *w);
int wnoutrefresh(WINDOW *w);
int doupdate(void);
int redrawwin(WINDOW *w);

int move(int y, int x);
int wmove(WINDOW *w, int y, int x);
int getcury(WINDOW *w);
int getcurx(WINDOW *w);
int getmaxy(WINDOW *w);
int getmaxx(WINDOW *w);
int getbegy(WINDOW *w);
int getbegx(WINDOW *w);

int addch(chtype c);
int waddch(WINDOW *w, chtype c);
int mvaddch(int y, int x, chtype c);
int mvwaddch(WINDOW *w, int y, int x, chtype c);

int addstr(const char *s);
int addnstr(const char *s, int n);
int waddstr(WINDOW *w, const char *s);
int waddnstr(WINDOW *w, const char *s, int n);
int mvaddstr(int y, int x, const char *s);
int mvwaddstr(WINDOW *w, int y, int x, const char *s);
int mvwaddnstr(WINDOW *w, int y, int x, const char *s, int n);

int printw(const char *fmt, ...);
int wprintw(WINDOW *w, const char *fmt, ...);
int mvprintw(int y, int x, const char *fmt, ...);
int mvwprintw(WINDOW *w, int y, int x, const char *fmt, ...);
int vw_printw(WINDOW *w, const char *fmt, va_list ap);

int attron(attr_t a);
int attroff(attr_t a);
int attrset(attr_t a);
int wattron(WINDOW *w, attr_t a);
int wattroff(WINDOW *w, attr_t a);
int wattrset(WINDOW *w, attr_t a);
int wcolor_set(WINDOW *w, short pair, void *opts);
int color_set(short pair, void *opts);
int bkgd(chtype c);
int wbkgd(WINDOW *w, chtype c);

int erase(void);
int werase(WINDOW *w);
int clear(void);
int wclear(WINDOW *w);
int clrtoeol(void);
int wclrtoeol(WINDOW *w);
int clrtobot(void);
int wclrtobot(WINDOW *w);

int box(WINDOW *w, chtype v, chtype h);
int wborder(WINDOW *w, chtype ls, chtype rs, chtype ts, chtype bs,
            chtype tl, chtype tr, chtype bl, chtype br);
int whline(WINDOW *w, chtype c, int n);
int wvline(WINDOW *w, chtype c, int n);
int hline(chtype c, int n);
int vline(chtype c, int n);
int mvwhline(WINDOW *w, int y, int x, chtype c, int n);
int mvwvline(WINDOW *w, int y, int x, chtype c, int n);

int getch(void);
int wgetch(WINDOW *w);
int ungetch(int c);
int getnstr(char *s, int n);
int wgetnstr(WINDOW *w, char *s, int n);
int mvwgetnstr(WINDOW *w, int y, int x, char *s, int n);

int napms(int ms);
int beep(void);
int flash(void);

#endif

#ifndef TUI_H
#define TUI_H

#define TK_UP     1000
#define TK_DOWN   1001
#define TK_LEFT   1002
#define TK_RIGHT  1003
#define TK_HOME   1004
#define TK_END    1005
#define TK_PGUP   1006
#define TK_PGDN   1007
#define TK_DEL    1008
#define TK_ENTER  '\r'
#define TK_ESC    27
#define TK_BACKSP 127
#define TK_TAB    '\t'

void tui_begin(void);
void tui_end(void);
void tui_size(int *rows, int *cols);
void tui_clear(void);
void tui_home(void);
void tui_move(int row, int col);
void tui_hide_cursor(void);
void tui_show_cursor(void);
void tui_puts(const char *s);
int  tui_read_key(void);

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <termios.h>
#include <dirent.h>
#include <poll.h>
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
    int            size;
    int            cap;
    char          *chars;
    int            rsize;
    char          *render;
    unsigned char *hl;
    int            hl_open_comment;
} neo_row_t;

enum {
    HL_NORMAL = 0, HL_COMMENT, HL_MLCOMMENT, HL_KEYWORD1,
    HL_KEYWORD2, HL_STRING, HL_NUMBER, HL_PREPROC,
    HL_ESCAPE, HL_TAG, HL_ATTR
};

enum { LANG_NONE = 0, LANG_C, LANG_JS, LANG_CSS, LANG_HTML, LANG_ASM, LANG_COUNT };

static const char *LANG_NAMES[LANG_COUNT] = {
    "Plain", "C", "JavaScript", "CSS", "HTML", "Assembly"
};

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
    long       disk_size;
    int        show_lineno;
    int        lineno_width;
    char      *clipboard;
    int        clipboard_len;
    int        clipboard_was_line;
    int        syntax;
    int        autopairs;
    int        tabstop;
    int        quit_confirm;
    struct termios orig_termios;
} neo_t;

static neo_t E;

static void die(const char *msg)
{
    write(1, "\x1b[?25h\x1b[?1049l", 14);
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
    write(1, "\x1b[?7h\x1b[?25h\x1b[?1049l", 19);
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
    write(1, "\x1b[?1049h\x1b[?7l", 13);
}

static int esc_has_more(void)
{
    struct pollfd p = { 0, POLLIN, 0 };
    return poll(&p, 1, 40) > 0 && (p.revents & POLLIN);
}

static int read_key(void)
{
    char c;
    ssize_t n;
    while ((n = read(0, &c, 1)) == 0) { }
    if (n < 0) return KEY_NONE;

    if (c != 0x1B) return (unsigned char)c;

    char seq[4];
    if (!esc_has_more() || read(0, &seq[0], 1) != 1) return KEY_ESC;
    if (!esc_has_more() || read(0, &seq[1], 1) != 1) return KEY_ESC;

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (!esc_has_more() || read(0, &seq[2], 1) != 1) return KEY_ESC;
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

static int utf8_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

static int row_cx_to_rx(neo_row_t *row, int cx)
{
    int rx = 0;
    for (int j = 0; j < cx && j < row->size; j++) {
        if (utf8_cont((unsigned char)row->chars[j])) continue;
        if (row->chars[j] == '\t') rx += (E.tabstop - (rx % E.tabstop));
        else rx++;
    }
    return rx;
}

static int row_rx_to_cx(neo_row_t *row, int rx)
{
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (utf8_cont((unsigned char)row->chars[cx])) continue;
        if (row->chars[cx] == '\t') cur_rx += (E.tabstop - (cur_rx % E.tabstop));
        else cur_rx++;
        if (cur_rx > rx) return cx;
    }
    return cx;
}

static const char *C_KEYWORDS[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "case", "default", "do",
    "goto", "sizeof", "const", "extern", "volatile", "inline", "register",
    "#include", "#define", "#ifdef", "#ifndef", "#endif", "#if", "#else",
    "#elif", "#pragma", "#undef",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "short|", "auto|", "bool|", "size_t|", "ssize_t|", "uintptr_t|",
    "uint8_t|", "uint16_t|", "uint32_t|", "uint64_t|",
    "int8_t|", "int16_t|", "int32_t|", "int64_t|", NULL
};

static const char *JS_KEYWORDS[] = {
    "function", "return", "if", "else", "for", "while", "do", "switch", "case",
    "break", "continue", "new", "delete", "typeof", "instanceof", "in", "of",
    "class", "extends", "super", "import", "export", "from", "as", "default",
    "try", "catch", "finally", "throw", "async", "await", "yield", "static",
    "get", "set", "void",
    "var|", "let|", "const|", "this|", "true|", "false|", "null|", "undefined|",
    "NaN|", "Infinity|", NULL
};

static const char *CSS_KEYWORDS[] = {
    "@media", "@import", "@keyframes", "@font-face", "@supports", "@charset",
    "important", "inherit", "initial", "unset", "none", "auto", "block",
    "inline", "flex", "grid", "absolute", "relative", "fixed", "static",
    "hidden", "visible", "bold", "italic", "solid", "dashed", "dotted",
    "center", "left", "right", "top", "bottom", "middle",
    "px|", "em|", "rem|", "vh|", "vw|", "pt|", "deg|", NULL
};

static const char *ASM_KEYWORDS[] = {
    "mov", "movq", "movl", "movw", "movb", "lea", "push", "pop", "call", "ret",
    "jmp", "je", "jne", "jz", "jnz", "jg", "jge", "jl", "jle", "ja", "jb",
    "add", "sub", "mul", "imul", "div", "idiv", "inc", "dec", "and", "or",
    "xor", "not", "neg", "shl", "shr", "sar", "sal", "cmp", "test", "int",
    "syscall", "sysret", "nop", "hlt", "cli", "sti", "iret", "iretq", "leave",
    "cpuid", "rdmsr", "wrmsr", "in", "out", "loop", "enter",
    "rax|", "rbx|", "rcx|", "rdx|", "rsi|", "rdi|", "rbp|", "rsp|",
    "r8|", "r9|", "r10|", "r11|", "r12|", "r13|", "r14|", "r15|",
    "eax|", "ebx|", "ecx|", "edx|", "esi|", "edi|", "ebp|", "esp|",
    "ax|", "bx|", "cx|", "dx|", "al|", "bl|", "cl|", "dl|", "rip|",
    ".text", ".data", ".bss", ".global", ".globl", ".section", ".byte",
    ".word", ".long", ".quad", ".ascii", ".asciz", ".align", ".equ", ".extern",
    "section", "global", "extern", "db", "dw", "dd", "dq", "resb", "equ", NULL
};

typedef struct {
    const char **kw;
    const char  *linec;
    const char  *blockc0;
    const char  *blockc1;
    int          sq;
    int          bt;
} syntax_def;

static const syntax_def SYNTAX_DEFS[LANG_COUNT] = {
    { NULL,         NULL, NULL,   NULL,  0, 0 },
    { C_KEYWORDS,   "//", "/*",   "*/",  1, 0 },
    { JS_KEYWORDS,  "//", "/*",   "*/",  1, 1 },
    { CSS_KEYWORDS, NULL, "/*",   "*/",  1, 0 },
    { NULL,         NULL, NULL,   NULL,  0, 0 },
    { ASM_KEYWORDS, ";",  NULL,   NULL,  1, 0 },
};

static int lang_from_ext(const char *filename) {
    const char *dot = NULL;
    for (const char *p = filename; *p; p++) if (*p == '.') dot = p;
    if (!dot) return LANG_NONE;
    if (!strcmp(dot, ".c") || !strcmp(dot, ".h") || !strcmp(dot, ".cpp") ||
        !strcmp(dot, ".cc") || !strcmp(dot, ".hpp") || !strcmp(dot, ".cxx")) return LANG_C;
    if (!strcmp(dot, ".js") || !strcmp(dot, ".ts") || !strcmp(dot, ".jsx") ||
        !strcmp(dot, ".json") || !strcmp(dot, ".mjs")) return LANG_JS;
    if (!strcmp(dot, ".css")) return LANG_CSS;
    if (!strcmp(dot, ".html") || !strcmp(dot, ".htm") || !strcmp(dot, ".xml") ||
        !strcmp(dot, ".svg")) return LANG_HTML;
    if (!strcmp(dot, ".s") || !strcmp(dot, ".S") || !strcmp(dot, ".asm") ||
        !strcmp(dot, ".nasm")) return LANG_ASM;
    return LANG_NONE;
}

static int hl_is_space(int c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}
static int hl_is_digit(int c) { return c >= '0' && c <= '9'; }
static int hl_is_sep(int c) {
    return c == '\0' || hl_is_space(c) ||
           strchr(",.()+-/*=~%<>[]{};:&|!?^", c) != NULL;
}
static const char *hl_sgr(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT: return "\x1b[38;2;106;115;125m";
        case HL_KEYWORD1:  return "\x1b[38;2;198;120;221m";
        case HL_KEYWORD2:  return "\x1b[38;2;229;192;123m";
        case HL_STRING:    return "\x1b[38;2;152;195;121m";
        case HL_NUMBER:    return "\x1b[38;2;209;154;102m";
        case HL_PREPROC:   return "\x1b[38;2;86;182;194m";
        case HL_ESCAPE:    return "\x1b[38;2;97;175;239m";
        case HL_TAG:       return "\x1b[38;2;224;108;117m";
        case HL_ATTR:      return "\x1b[38;2;209;154;102m";
        default:           return "\x1b[38;2;200;204;212m";
    }
}

static void hl_generic(neo_row_t *row, int at, const syntax_def *def)
{
    char *s = row->render;
    int i = 0, prev_sep = 1, in_string = 0;
    int in_comment = (at > 0 && E.row[at - 1].hl_open_comment == 1);
    int lclen = def->linec ? (int)strlen(def->linec) : 0;
    int b0len = def->blockc0 ? (int)strlen(def->blockc0) : 0;
    int b1len = def->blockc1 ? (int)strlen(def->blockc1) : 0;

    while (i < row->rsize) {
        char c = s[i];

        if (!in_string && !in_comment && lclen &&
            i + lclen <= row->rsize && strncmp(&s[i], def->linec, lclen) == 0) {
            memset(&row->hl[i], HL_COMMENT, row->rsize - i);
            break;
        }
        if (!in_string && b0len) {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (b1len && i + b1len <= row->rsize && strncmp(&s[i], def->blockc1, b1len) == 0) {
                    memset(&row->hl[i], HL_MLCOMMENT, b1len);
                    i += b1len; in_comment = 0; prev_sep = 1; continue;
                }
                i++; continue;
            } else if (i + b0len <= row->rsize && strncmp(&s[i], def->blockc0, b0len) == 0) {
                memset(&row->hl[i], HL_MLCOMMENT, b0len);
                i += b0len; in_comment = 1; continue;
            }
        }
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < row->rsize) {
                row->hl[i] = HL_ESCAPE; row->hl[i + 1] = HL_ESCAPE; i += 2; continue;
            }
            if (c == in_string) in_string = 0;
            i++; prev_sep = 1; continue;
        } else if (c == '"' || (def->sq && c == '\'') || (def->bt && c == '`')) {
            in_string = c; row->hl[i] = HL_STRING; i++; continue;
        }
        if (hl_is_digit((unsigned char)c) && prev_sep) {
            row->hl[i++] = HL_NUMBER;
            while (i < row->rsize) {
                char d = s[i]; char dl = (char)(d | 32);
                if (hl_is_digit((unsigned char)d) || d == '.' || d == 'x' || d == 'X' ||
                    (dl >= 'a' && dl <= 'f') || d == 'u' || d == 'l' || d == 'f') {
                    row->hl[i++] = HL_NUMBER;
                } else break;
            }
            prev_sep = 0; continue;
        }
        if (prev_sep && def->kw) {
            int j;
            for (j = 0; def->kw[j]; j++) {
                int klen = (int)strlen(def->kw[j]);
                int kw2 = def->kw[j][klen - 1] == '|';
                if (kw2) klen--;
                int pp = (def->kw[j][0] == '#' || def->kw[j][0] == '.' || def->kw[j][0] == '@');
                if (i + klen <= row->rsize &&
                    strncmp(&s[i], def->kw[j], klen) == 0 &&
                    hl_is_sep(i + klen < row->rsize ? s[i + klen] : '\0')) {
                    memset(&row->hl[i], pp ? HL_PREPROC : (kw2 ? HL_KEYWORD2 : HL_KEYWORD1), klen);
                    i += klen; break;
                }
            }
            if (def->kw[j] != NULL) { prev_sep = 0; continue; }
        }
        prev_sep = hl_is_sep((unsigned char)c);
        i++;
    }
    row->hl_open_comment = in_comment ? 1 : 0;
}

static void hl_html(neo_row_t *row, int at)
{
    char *s = row->render;
    int i = 0, in_string = 0;
    int state = (at > 0) ? E.row[at - 1].hl_open_comment : 0;

    while (i < row->rsize) {
        char c = s[i];
        if (state == 1) {
            row->hl[i] = HL_MLCOMMENT;
            if (c == '-' && i + 3 <= row->rsize && strncmp(&s[i], "-->", 3) == 0) {
                memset(&row->hl[i], HL_MLCOMMENT, 3); i += 3; state = 0; continue;
            }
            i++; continue;
        }
        if (state == 0) {
            if (c == '<' && i + 4 <= row->rsize && strncmp(&s[i], "<!--", 4) == 0) {
                memset(&row->hl[i], HL_MLCOMMENT, 4); i += 4; state = 1; continue;
            }
            if (c == '<') {
                row->hl[i++] = HL_TAG; state = 2;
                while (i < row->rsize && (isalnum((unsigned char)s[i]) || s[i] == '/' || s[i] == '!')) {
                    row->hl[i++] = HL_TAG;
                }
                continue;
            }
            if (c == '&') {
                int j = i + 1;
                while (j < row->rsize && s[j] != ';' && s[j] != ' ' && j - i < 12) j++;
                if (j < row->rsize && s[j] == ';') { memset(&row->hl[i], HL_PREPROC, j - i + 1); i = j + 1; continue; }
            }
            i++; continue;
        }
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (c == in_string) in_string = 0;
            i++; continue;
        }
        if (c == '"' || c == '\'') { in_string = c; row->hl[i] = HL_STRING; i++; continue; }
        if (c == '>') { row->hl[i] = HL_TAG; i++; state = 0; continue; }
        if (isalpha((unsigned char)c)) {
            while (i < row->rsize && (isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == ':')) {
                row->hl[i++] = HL_ATTR;
            }
            continue;
        }
        i++;
    }
    row->hl_open_comment = state;
}

static void editor_update_syntax(int at)
{
    neo_row_t *row = &E.row[at];
    free(row->hl);
    row->hl = malloc(row->rsize ? row->rsize : 1);
    if (!row->hl) return;
    memset(row->hl, HL_NORMAL, row->rsize ? row->rsize : 1);

    int old_open = row->hl_open_comment;

    if (E.syntax == LANG_NONE) {
        row->hl_open_comment = 0;
    } else if (E.syntax == LANG_HTML) {
        hl_html(row, at);
    } else {
        hl_generic(row, at, &SYNTAX_DEFS[E.syntax]);
    }

    if (row->hl_open_comment != old_open && at + 1 < E.numrows)
        editor_update_syntax(at + 1);
}

static void row_update(neo_row_t *row)
{
    int tabs = 0;
    for (int j = 0; j < row->size; j++) if (row->chars[j] == '\t') tabs++;
    free(row->render);
    row->render = malloc(row->size + tabs * (E.tabstop - 1) + 1);
    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % E.tabstop != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editor_update_syntax((int)(row - E.row));
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
        nr[i].hl = NULL; nr[i].hl_open_comment = 0;
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
    r->hl = NULL;
    r->hl_open_comment = 0;
    row_update(r);
    E.numrows++;
    E.dirty = 1;
}

static void row_free(neo_row_t *r)
{
    free(r->chars);
    free(r->render);
    free(r->hl);
    r->chars = NULL; r->render = NULL; r->hl = NULL;
    r->size = 0; r->cap = 0; r->rsize = 0; r->hl_open_comment = 0;
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

static int ap_closer(int c) { return c == '(' ? ')' : c == '[' ? ']' : c == '{' ? '}' : 0; }
static int ap_is_quote(int c) { return c == '"' || c == '\'' || c == '`'; }
static int ap_is_word(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

static int ap_next(void) {
    if (E.cy >= E.numrows) return -1;
    neo_row_t *r = &E.row[E.cy];
    return E.cx < r->size ? (unsigned char)r->chars[E.cx] : -1;
}
static int ap_prev(void) {
    if (E.cy >= E.numrows) return -1;
    neo_row_t *r = &E.row[E.cy];
    return E.cx > 0 ? (unsigned char)r->chars[E.cx - 1] : -1;
}

static void editor_insert_char(int ch)
{
    if (E.cy == E.numrows) row_insert_at(E.numrows, "", 0);
    row_insert_char(&E.row[E.cy], E.cx, ch);
    E.cx++;
}

static int editor_autopair(int ch)
{
    if (!E.autopairs) return 0;
    int nx = ap_next();
    int cl = ap_closer(ch);
    if (cl) {
        editor_insert_char(ch);
        editor_insert_char(cl);
        E.cx--;
        return 1;
    }
    if ((ch == ')' || ch == ']' || ch == '}') && nx == ch) { E.cx++; return 1; }
    if (ap_is_quote(ch)) {
        if (nx == ch) { E.cx++; return 1; }
        int pv = ap_prev();
        if (!ap_is_word(pv) && !ap_is_word(nx)) {
            editor_insert_char(ch);
            editor_insert_char(ch);
            E.cx--;
            return 1;
        }
    }
    return 0;
}

static void editor_insert_newline(void)
{
    if (E.cx == 0) {
        row_insert_at(E.cy, "", 0);
        E.cy++;
        E.cx = 0;
        return;
    }

    neo_row_t *r = &E.row[E.cy];
    int indent = 0;
    while (indent < E.cx && indent < r->size &&
           (r->chars[indent] == ' ' || r->chars[indent] == '\t'))
        indent++;

    int tail_len = r->size - E.cx;
    int new_len = indent + tail_len;
    char *nl = malloc(new_len > 0 ? new_len : 1);
    if (nl) {
        if (indent > 0)   memcpy(nl, r->chars, indent);
        if (tail_len > 0) memcpy(nl + indent, &r->chars[E.cx], tail_len);
    }
    row_insert_at(E.cy + 1, nl ? nl : "", nl ? new_len : 0);
    free(nl);

    r = &E.row[E.cy];
    r->size = E.cx;
    r->chars[r->size] = '\0';
    row_update(r);

    E.cy++;
    E.cx = indent;
}

static void editor_delete_char(void)
{
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    neo_row_t *r = &E.row[E.cy];
    if (E.autopairs && E.cx > 0 && E.cx < r->size) {
        int p = (unsigned char)r->chars[E.cx - 1];
        int n = (unsigned char)r->chars[E.cx];
        if ((p == '(' && n == ')') || (p == '[' && n == ']') ||
            (p == '{' && n == '}') || (p == '"' && n == '"') ||
            (p == '\'' && n == '\'') || (p == '`' && n == '`')) {
            row_delete_char(r, E.cx);
            row_delete_char(r, E.cx - 1);
            E.cx--;
            return;
        }
    }
    if (E.cx > 0) {
        int start = E.cx - 1;
        while (start > 0 && utf8_cont((unsigned char)r->chars[start])) start--;
        while (E.cx > start) { row_delete_char(r, E.cx - 1); E.cx--; }
    } else {
        E.cx = E.row[E.cy - 1].size;
        row_append_string(&E.row[E.cy - 1], r->chars, r->size);
        row_delete_at(E.cy);
        E.cy--;
    }
}

static void editor_delete_char_forward(void)
{
    if (E.cy == E.numrows) return;
    neo_row_t *r = &E.row[E.cy];
    if (E.cx < r->size) {
        row_delete_char(r, E.cx);
        while (E.cx < r->size && utf8_cont((unsigned char)r->chars[E.cx]))
            row_delete_char(r, E.cx);
    } else if (E.cy + 1 < E.numrows) {
        neo_row_t *nx = &E.row[E.cy + 1];
        row_append_string(r, nx->chars, nx->size);
        row_delete_at(E.cy + 1);
    }
}

static void editor_copy_line(void)
{
    if (E.cy >= E.numrows) return;
    neo_row_t *r = &E.row[E.cy];
    free(E.clipboard);
    E.clipboard = malloc(r->size + 1);
    if (!E.clipboard) { E.clipboard_len = 0; return; }
    memcpy(E.clipboard, r->chars, r->size);
    E.clipboard[r->size] = '\0';
    E.clipboard_len = r->size;
    E.clipboard_was_line = 1;
}

static void editor_cut_line(void)
{
    if (E.cy >= E.numrows) return;
    editor_copy_line();
    row_delete_at(E.cy);
    if (E.cy >= E.numrows && E.cy > 0) E.cy--;
    E.cx = 0;
}

static void editor_paste(void)
{
    if (!E.clipboard || E.clipboard_len == 0) return;
    if (E.clipboard_was_line) {
        row_insert_at(E.cy, E.clipboard, E.clipboard_len);
        E.cy++;
        E.cx = 0;
    } else {
        if (E.cy == E.numrows) row_insert_at(E.numrows, "", 0);
        neo_row_t *r = &E.row[E.cy];
        row_reserve(r, r->size + E.clipboard_len + 1);
        memmove(&r->chars[E.cx + E.clipboard_len], &r->chars[E.cx], r->size - E.cx + 1);
        memcpy(&r->chars[E.cx], E.clipboard, E.clipboard_len);
        r->size += E.clipboard_len;
        row_update(r);
        E.cx += E.clipboard_len;
        E.dirty = 1;
    }
}

static void editor_duplicate_line(void)
{
    if (E.cy >= E.numrows) return;
    neo_row_t *r = &E.row[E.cy];
    char *copy = malloc(r->size + 1);
    if (!copy) return;
    memcpy(copy, r->chars, r->size);
    copy[r->size] = '\0';
    row_insert_at(E.cy + 1, copy, r->size);
    free(copy);
    E.cy++;
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

    E.syntax = lang_from_ext(filename);

    char full[512];
    snprintf(full, sizeof(full), "%s", filename);

    int fd = open(full, O_RDONLY, 0);
    if (fd < 0) {
        E.disk_size = -1;
        set_status("New file: %s", filename);
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); E.disk_size = -1; return; }
    size_t sz = (size_t)st.st_size;
    E.disk_size = (long)sz;
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
        E.disk_size = -1;
    }

    char full[512];
    snprintf(full, sizeof(full), "%s", E.filename);

    if (E.disk_size >= 0) {
        struct stat st;
        if (stat(full, &st) == 0 && (long)st.st_size != E.disk_size) {
            char *ans = prompt("File changed on disk! Overwrite? [y/N]: %s");
            if (!ans) { set_status("Save cancelled"); return -1; }
            int yes = (ans[0] == 'y' || ans[0] == 'Y');
            free(ans);
            if (!yes) { set_status("Save cancelled"); return -1; }
        }
    }

    int len = 0;
    char *buf = rows_to_string(&len);

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
    E.disk_size = len;
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

static void recompute_lineno_width(void)
{
    if (!E.show_lineno) { E.lineno_width = 0; return; }
    int max = E.numrows > 0 ? E.numrows : 1;
    int d = 1;
    while (max >= 10) { max /= 10; d++; }
    if (d < 3) d = 3;
    E.lineno_width = d + 1;
}

static void scroll(void)
{
    E.rx = 0;
    if (E.cy < E.numrows) E.rx = row_cx_to_rx(&E.row[E.cy], E.cx);

    int text_cols = E.screencols - E.lineno_width;
    if (text_cols < 1) text_cols = 1;

    if (E.cy < E.rowoff) E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;
    if (E.rx < E.coloff) E.coloff = E.rx;
    if (E.rx >= E.coloff + text_cols) E.coloff = E.rx - text_cols + 1;
}

static void draw_rows(abuf_t *ab)
{
    char pos[16];
    int text_cols = E.screencols - E.lineno_width;
    if (text_cols < 1) text_cols = 1;
    int limit = text_cols - 1;
    if (limit < 1) limit = 1;
    for (int y = 0; y < E.screenrows; y++) {
        int n = snprintf(pos, sizeof(pos), "\x1b[%d;1H", y + 1);
        ab_append(ab, pos, n);

        int filerow = y + E.rowoff;

        if (E.show_lineno) {
            char lnbuf[16];
            int ln;
            if (filerow < E.numrows)
                ln = snprintf(lnbuf, sizeof(lnbuf), "\x1b[90m%*d \x1b[m",
                              E.lineno_width - 1, filerow + 1);
            else
                ln = snprintf(lnbuf, sizeof(lnbuf), "\x1b[90m%*s \x1b[m",
                              E.lineno_width - 1, "~");
            ab_append(ab, lnbuf, ln);
        }

        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3 && !E.show_lineno) {
                char welcome[80];
                int wl = snprintf(welcome, sizeof(welcome),
                    "neo editor -- version %s -- press ESC to exit", NEO_VERSION);
                if (wl > limit) wl = limit;
                int padding = (limit - wl) / 2;
                if (padding > 0) { ab_append(ab, "~", 1); padding--; }
                while (padding-- > 0) ab_append(ab, " ", 1);
                ab_append(ab, welcome, wl);
            } else if (!E.show_lineno) {
                ab_append(ab, "~", 1);
            }
        } else {
            const char *rnd = E.row[filerow].render;
            int rsz = E.row[filerow].rsize;
            int byte_off = 0, vis = 0;
            while (byte_off < rsz && vis < E.coloff) {
                if (!utf8_cont((unsigned char)rnd[byte_off])) vis++;
                byte_off++;
            }
            while (byte_off < rsz && utf8_cont((unsigned char)rnd[byte_off])) byte_off++;
            int len = 0, cols = 0;
            while (byte_off + len < rsz && cols < limit) {
                if (!utf8_cont((unsigned char)rnd[byte_off + len])) cols++;
                len++;
            }
            while (byte_off + len < rsz && utf8_cont((unsigned char)rnd[byte_off + len])) len++;

            unsigned char *hl = E.row[filerow].hl;
            int cur_hl = -1;
            int k = 0;
            while (k < len) {
                int hlc = hl ? hl[byte_off + k] : HL_NORMAL;
                if (hlc != cur_hl) {
                    const char *sgr = hl_sgr(hlc);
                    ab_append(ab, sgr, strlen(sgr));
                    cur_hl = hlc;
                }
                ab_append(ab, &rnd[byte_off + k], 1);
                k++;
                while (k < len && utf8_cont((unsigned char)rnd[byte_off + k])) {
                    ab_append(ab, &rnd[byte_off + k], 1);
                    k++;
                }
            }
            if (cur_hl != -1) ab_append(ab, "\x1b[39m", 5);
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
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | Ln %d, Col %d / %d lines ",
        LANG_NAMES[E.syntax], E.cy + 1, E.cx + 1, E.numrows);

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
        const char *hint = " ^S save  ^Q quit  ^X cut  ^C copy  ^V paste  ^F find  ^G goto  ^L syntax  ^P settings  ^T tree";
        int mlen = strlen(hint);
        if (mlen > E.screencols) mlen = E.screencols;
        ab_append(ab, hint, mlen);
    }
}

static void refresh_screen(void)
{
    recompute_lineno_width();
    scroll();
    abuf_t ab = {0};
    ab_append(&ab, "\x1b[?25l", 6);
    draw_rows(&ab);
    draw_status(&ab);
    draw_message(&ab);

    int cursor_row = (E.cy - E.rowoff) + 1;
    int cursor_col = (E.rx - E.coloff) + 1 + E.lineno_width;

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

static char *prompt_cb(const char *prompt_fmt, void (*callback)(char *, int))
{
    size_t bufcap = 128;
    size_t buflen = 0;
    char *buf = malloc(bufcap);
    buf[0] = '\0';

    for (;;) {
        set_status(prompt_fmt, buf);
        refresh_screen();

        int slen = (int)strlen(E.statusmsg);
        if (slen > E.screencols) slen = E.screencols;
        char curbuf[32];
        int cn = snprintf(curbuf, sizeof(curbuf), "\x1b[%d;%dH", E.screenrows + 2, slen + 1);
        write(1, curbuf, cn);

        int c = read_key();
        if (c == KEY_DEL || c == KEY_CTRL('h') || c == KEY_BACKSPACE) {
            if (buflen > 0) {
                buflen--;
                while (buflen > 0 && utf8_cont((unsigned char)buf[buflen])) buflen--;
                buf[buflen] = '\0';
            }
        } else if (c == KEY_ESC) {
            set_status("");
            E.statusmsg_visible = 0;
            if (callback) callback(buf, c);
            free(buf);
            return NULL;
        } else if (c == '\r' || c == '\n') {
            if (buflen != 0 || callback) {
                set_status("");
                E.statusmsg_visible = 0;
                if (callback) callback(buf, c);
                return buf;
            }
        } else if (c < 1000 && (c >= 0x80 || !iscntrl(c))) {
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
        if (callback) callback(buf, c);
    }
}

static char *prompt(const char *prompt_fmt)
{
    return prompt_cb(prompt_fmt, NULL);
}

static void move_cursor(int key)
{
    neo_row_t *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) {
        case KEY_ARROW_LEFT:
            if (E.cx > 0) {
                E.cx--;
                while (E.cx > 0 && utf8_cont((unsigned char)row->chars[E.cx])) E.cx--;
            }
            else if (E.cy > 0) { E.cy--; E.cx = E.row[E.cy].size; }
            break;
        case KEY_ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
                while (E.cx < row->size && utf8_cont((unsigned char)row->chars[E.cx])) E.cx++;
            }
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

static int   __find_last_match = -1;
static int   __find_direction  = 1;
static int   __find_saved_cx, __find_saved_cy;
static int   __find_saved_rowoff, __find_saved_coloff;
static char *__find_last_query = NULL;

static void editor_find_callback(char *query, int key)
{
    if (key == '\r' || key == '\n' || key == KEY_ESC) {
        __find_last_match = -1;
        __find_direction  = 1;
        return;
    }
    if (key == KEY_ARROW_DOWN || key == KEY_ARROW_RIGHT) {
        __find_direction = 1;
    } else if (key == KEY_ARROW_UP || key == KEY_ARROW_LEFT) {
        __find_direction = -1;
    } else {
        __find_last_match = -1;
        __find_direction  = 1;
    }
    if (!query || !query[0]) return;

    int current = __find_last_match;
    if (current == -1) current = E.cy;
    int qlen = (int)strlen(query);

    for (int i = 0; i < E.numrows; i++) {
        current += __find_direction;
        if (current == -1)            current = E.numrows - 1;
        else if (current == E.numrows) current = 0;

        neo_row_t *row = &E.row[current];
        char *match = strstr(row->render, query);
        if (match) {
            __find_last_match = current;
            E.cy = current;
            int rx = (int)(match - row->render);
            E.cx = row_rx_to_cx(row, rx);
            E.rowoff = E.numrows;
            (void)qlen;
            return;
        }
    }
}

static void editor_find(void)
{
    __find_saved_cx     = E.cx;
    __find_saved_cy     = E.cy;
    __find_saved_rowoff = E.rowoff;
    __find_saved_coloff = E.coloff;
    __find_last_match   = -1;
    __find_direction    = 1;

    char *query = prompt_cb(
        "Search: %s (Up/Down=prev/next, Enter=keep, ESC=cancel)",
        editor_find_callback);

    if (query) {
        if (query[0] == '\0' && __find_last_query) {
            free(query);
            query = strdup(__find_last_query);
            if (query) {
                editor_find_callback(query, 0);
            }
        }
        if (query && query[0]) {
            free(__find_last_query);
            __find_last_query = strdup(query);
        }
        if (query) free(query);
    } else {
        E.cx     = __find_saved_cx;
        E.cy     = __find_saved_cy;
        E.rowoff = __find_saved_rowoff;
        E.coloff = __find_saved_coloff;
    }
}

static void editor_choose_language(void)
{
    set_status("Syntax  0)Plain 1)C 2)JS 3)CSS 4)HTML 5)ASM   [current: %s]",
               LANG_NAMES[E.syntax]);
    refresh_screen();
    int c = read_key();
    if (c >= '0' && c <= '0' + (LANG_COUNT - 1)) {
        E.syntax = c - '0';
        for (int i = 0; i < E.numrows; i++) editor_update_syntax(i);
        set_status("Syntax: %s", LANG_NAMES[E.syntax]);
    } else {
        set_status("");
    }
}

static void editor_reset_buffer(void)
{
    for (int i = 0; i < E.numrows; i++) row_free(&E.row[i]);
    free(E.row);
    E.row = NULL;
    E.numrows = 0; E.rowscap = 0;
    E.cx = E.cy = E.rx = 0;
    E.rowoff = E.coloff = 0;
    E.dirty = 0;
}

#define TREE_MAX     4096
#define TREE_EXP_MAX 256

typedef struct { char path[512]; char name[256]; int depth; int is_dir; } tnode_t;

static tnode_t g_tnodes[TREE_MAX];
static int     g_tn;
static char    g_expanded[TREE_EXP_MAX][512];
static int     g_nexp;
static char    g_tree_root[512];

static void tree_join(const char *dir, const char *name, char *out) {
    if (!strcmp(dir, "/")) snprintf(out, 512, "/%s", name);
    else                   snprintf(out, 512, "%s/%s", dir, name);
}

static int tree_is_expanded(const char *p) {
    for (int i = 0; i < g_nexp; i++) if (!strcmp(g_expanded[i], p)) return 1;
    return 0;
}

static void tree_toggle_exp(const char *p) {
    for (int i = 0; i < g_nexp; i++) if (!strcmp(g_expanded[i], p)) {
        for (int j = i; j < g_nexp - 1; j++) memcpy(g_expanded[j], g_expanded[j + 1], 512);
        g_nexp--;
        return;
    }
    if (g_nexp < TREE_EXP_MAX) snprintf(g_expanded[g_nexp++], 512, "%s", p);
}

static void tree_expand(const char *p) { if (!tree_is_expanded(p)) tree_toggle_exp(p); }

typedef struct { char name[256]; int is_dir; } tent_t;

static void tree_walk(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return;
    static tent_t ents[1024];
    int ne = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && ne < 1024) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char full[512];
        tree_join(dir, e->d_name, full);
        struct stat st;
        int isd = (stat(full, &st) == 0) ? (S_ISDIR(st.st_mode) ? 1 : 0) : 0;
        snprintf(ents[ne].name, 256, "%s", e->d_name);
        ents[ne].is_dir = isd;
        ne++;
    }
    closedir(d);
    for (int i = 0; i < ne - 1; i++)
        for (int j = i + 1; j < ne; j++) {
            int sw = (ents[j].is_dir != ents[i].is_dir) ? (ents[j].is_dir > ents[i].is_dir)
                                                        : (strcmp(ents[i].name, ents[j].name) > 0);
            if (sw) { tent_t t = ents[i]; ents[i] = ents[j]; ents[j] = t; }
        }
    for (int i = 0; i < ne && g_tn < TREE_MAX; i++) {
        char full[512];
        tree_join(dir, ents[i].name, full);
        tnode_t *n = &g_tnodes[g_tn++];
        snprintf(n->path, 512, "%s", full);
        snprintf(n->name, 256, "%s", ents[i].name);
        n->depth = depth;
        n->is_dir = ents[i].is_dir;
        if (ents[i].is_dir && tree_is_expanded(full)) tree_walk(full, depth + 1);
    }
}

static void tree_rebuild(void) { g_tn = 0; tree_walk(g_tree_root, 0); }

static int tree_input(const char *label, char *buf, int cap) {
    int len = 0; buf[0] = 0;
    for (;;) {
        char line[700];
        int n = snprintf(line, sizeof line, "\x1b[%d;1H\x1b[43m\x1b[30m %s \x1b[0m %s\x1b[K",
                         E.screenrows + 2, label, buf);
        write(1, line, n);
        int k = read_key();
        if (k == '\r' || k == '\n') return len > 0 ? 1 : 0;
        if (k == KEY_ESC) return -1;
        if (k == KEY_BACKSPACE || k == KEY_CTRL('h')) { if (len > 0) buf[--len] = 0; continue; }
        if (k >= 32 && k < 127 && len < cap - 1) { buf[len++] = (char)k; buf[len] = 0; }
    }
}

static int tree_confirm(const char *msg) {
    char line[700];
    int n = snprintf(line, sizeof line, "\x1b[%d;1H\x1b[41m\x1b[97m %s (y/n) \x1b[0m\x1b[K", E.screenrows + 2, msg);
    write(1, line, n);
    for (;;) {
        int k = read_key();
        if (k == 'y' || k == 'Y') return 1;
        if (k == 'n' || k == 'N' || k == KEY_ESC) return 0;
    }
}

static void tree_target_dir(int sel, char *out) {
    if (g_tn == 0) { snprintf(out, 512, "%s", g_tree_root); return; }
    tnode_t *t = &g_tnodes[sel];
    if (t->is_dir) { snprintf(out, 512, "%s", t->path); tree_expand(t->path); }
    else {
        snprintf(out, 512, "%s", t->path);
        char *s = strrchr(out, '/');
        if (s) { if (s == out) out[1] = 0; else *s = 0; }
    }
}

static void editor_tree(void)
{
    if (g_tree_root[0] == 0) {
        if (E.filename) {
            snprintf(g_tree_root, 512, "%s", E.filename);
            char *s = strrchr(g_tree_root, '/');
            if (s) { if (s == g_tree_root) g_tree_root[1] = 0; else *s = 0; }
            else if (!getcwd(g_tree_root, 512)) strcpy(g_tree_root, ".");
        } else if (!getcwd(g_tree_root, 512)) strcpy(g_tree_root, "/");
    }
    tree_rebuild();

    int sel = 0, top = 0;
    for (;;) {
        int view = E.screenrows;
        if (view < 1) view = 1;
        if (sel >= g_tn) sel = g_tn > 0 ? g_tn - 1 : 0;
        if (sel < top) top = sel;
        if (sel >= top + view) top = sel - view + 1;

        abuf_t ab = {0};
        ab_append(&ab, "\x1b[?25l\x1b[H", 9);
        char hdr[700];
        int n = snprintf(hdr, sizeof hdr, "\x1b[44m\x1b[97m tree \x1b[0m\x1b[44m %-*.*s\x1b[0m\x1b[K\r\n",
                         E.screencols - 7, E.screencols - 7, g_tree_root);
        ab_append(&ab, hdr, n);
        for (int i = 0; i < view; i++) {
            int idx = top + i;
            char line[800];
            if (idx < g_tn) {
                tnode_t *t = &g_tnodes[idx];
                char ind[130]; int k = 0;
                for (int d = 0; d < t->depth && k < 126; d++) { ind[k++] = ' '; ind[k++] = ' '; }
                ind[k] = 0;
                const char *mark = t->is_dir ? (tree_is_expanded(t->path) ? "\x1b[93m- " : "\x1b[93m+ ") : "  ";
                const char *col  = t->is_dir ? "\x1b[94m" : "";
                n = snprintf(line, sizeof line, "%s%s%s%s%.*s\x1b[0m\x1b[K\r\n",
                             idx == sel ? "\x1b[7m" : "", ind, mark, col,
                             E.screencols - t->depth * 2 - 3, t->name);
            } else {
                n = snprintf(line, sizeof line, "\x1b[K\r\n");
            }
            ab_append(&ab, line, n);
        }
        char foot[800];
        n = snprintf(foot, sizeof foot,
                     "\x1b[%d;1H\x1b[46m\x1b[30m \x18\x19 nav  Enter open/expand  a file  m folder  r rename  d del  g refresh  Esc close \x1b[0m\x1b[K",
                     E.screenrows + 2);
        ab_append(&ab, foot, n);
        write(1, ab.b, ab.len);
        ab_free(&ab);

        int c = read_key();
        if (c == KEY_ESC || c == 'q' || c == KEY_CTRL('t')) break;
        else if (c == KEY_ARROW_UP)   { if (sel > 0) sel--; }
        else if (c == KEY_ARROW_DOWN) { if (sel < g_tn - 1) sel++; }
        else if (c == KEY_PAGE_UP)    { sel -= view; if (sel < 0) sel = 0; }
        else if (c == KEY_PAGE_DOWN)  { sel += view; if (sel > g_tn - 1) sel = g_tn - 1; }
        else if (c == KEY_HOME)       sel = 0;
        else if (c == KEY_END)        sel = g_tn - 1;
        else if (c == KEY_ARROW_LEFT) {
            if (g_tn && g_tnodes[sel].is_dir && tree_is_expanded(g_tnodes[sel].path)) {
                tree_toggle_exp(g_tnodes[sel].path); tree_rebuild();
            } else if (g_tn) {
                int d = g_tnodes[sel].depth;
                for (int i = sel - 1; i >= 0; i--) if (g_tnodes[i].depth < d) { sel = i; break; }
            }
        }
        else if (c == '\r' || c == KEY_ARROW_RIGHT) {
            if (!g_tn) continue;
            tnode_t *t = &g_tnodes[sel];
            if (t->is_dir) { tree_toggle_exp(t->path); tree_rebuild(); }
            else {
                if (E.dirty && !tree_confirm("Discard unsaved changes?")) continue;
                char path[512]; snprintf(path, sizeof path, "%s", t->path);
                editor_reset_buffer();
                editor_open(path);
                write(1, "\x1b[2J\x1b[H", 7);
                return;
            }
        }
        else if (c == 'a' || c == 'A') {
            char dir[512]; tree_target_dir(sel, dir);
            char name[256];
            if (tree_input("New file:", name, sizeof name) == 1) {
                char full[512]; tree_join(dir, name, full);
                int fd = open(full, O_WRONLY | O_CREAT, 0644);
                if (fd >= 0) close(fd);
                int has_content = E.numrows > 0 && (E.numrows > 1 || E.row[0].size > 0);
                if (has_content && tree_confirm("Save current text into new file?")) {
                    free(E.filename);
                    E.filename = malloc(strlen(full) + 1);
                    memcpy(E.filename, full, strlen(full) + 1);
                    E.syntax = lang_from_ext(full);
                    for (int i = 0; i < E.numrows; i++) editor_update_syntax(i);
                    editor_save();
                    write(1, "\x1b[2J\x1b[H", 7);
                    set_status("Saved to %s", full);
                    return;
                }
                tree_rebuild();
            }
        }
        else if (c == 'm' || c == 'M') {
            char dir[512]; tree_target_dir(sel, dir);
            char name[256];
            if (tree_input("New folder:", name, sizeof name) == 1) {
                char full[512]; tree_join(dir, name, full);
                mkdir(full, 0755);
                tree_rebuild();
            }
        }
        else if (c == 'r' || c == 'R') {
            if (!g_tn) continue;
            tnode_t *t = &g_tnodes[sel];
            char name[256]; snprintf(name, sizeof name, "%s", t->name);
            if (tree_input("Rename to:", name, sizeof name) == 1) {
                char dir[512]; snprintf(dir, sizeof dir, "%s", t->path);
                char *s = strrchr(dir, '/'); if (s) { if (s == dir) dir[1] = 0; else *s = 0; }
                char dst[512]; tree_join(dir, name, dst);
                rename(t->path, dst);
                tree_rebuild();
            }
        }
        else if (c == 'd' || c == 'D') {
            if (!g_tn) continue;
            tnode_t *t = &g_tnodes[sel];
            char msg[300]; snprintf(msg, sizeof msg, "Delete '%s'?", t->name);
            if (tree_confirm(msg)) {
                if (t->is_dir) rmdir(t->path);
                else           unlink(t->path);
                tree_rebuild();
            }
        }
        else if (c == 'g') tree_rebuild();
    }
    write(1, "\x1b[2J\x1b[H", 7);
    set_status("");
}

static void editor_settings(void)
{
    int sel = 0;
    const int NITEMS = 5;
    const char *names[5] = { "Auto-pairs", "Line numbers", "Syntax", "Tab width", "Quit confirm" };
    for (;;) {
        static const char hdr[] = "\x1b[44m\x1b[97m neo settings \x1b[0m  \x18\x19 move   < > change   Esc close\r\n\r\n";
        abuf_t ab = {0};
        ab_append(&ab, "\x1b[?25l\x1b[2J\x1b[H", 13);
        ab_append(&ab, hdr, (int)(sizeof hdr - 1));
        char vals[5][40];
        snprintf(vals[0], sizeof vals[0], "%s", E.autopairs ? "ON" : "OFF");
        snprintf(vals[1], sizeof vals[1], "%s", E.show_lineno ? "ON" : "OFF");
        snprintf(vals[2], sizeof vals[2], "%s", LANG_NAMES[E.syntax]);
        snprintf(vals[3], sizeof vals[3], "%d", E.tabstop);
        snprintf(vals[4], sizeof vals[4], "%s", E.quit_confirm ? "ON" : "OFF");
        for (int i = 0; i < NITEMS; i++) {
            char line[128];
            int n = snprintf(line, sizeof line, "%s  %-16s %-12s\x1b[0m\r\n",
                             i == sel ? "\x1b[7m" : "  ", names[i], vals[i]);
            ab_append(&ab, line, n);
        }
        write(1, ab.b, ab.len);
        ab_free(&ab);

        int c = read_key();
        if (c == KEY_ESC || c == 'q') break;
        else if (c == KEY_ARROW_UP)   sel = (sel + NITEMS - 1) % NITEMS;
        else if (c == KEY_ARROW_DOWN) sel = (sel + 1) % NITEMS;
        else if (c == KEY_ARROW_LEFT || c == KEY_ARROW_RIGHT || c == '\r' || c == ' ') {
            int dir = (c == KEY_ARROW_LEFT) ? -1 : 1;
            switch (sel) {
                case 0: E.autopairs = !E.autopairs; break;
                case 1: E.show_lineno = !E.show_lineno; recompute_lineno_width(); break;
                case 2: E.syntax = (E.syntax + LANG_COUNT + dir) % LANG_COUNT;
                        for (int i = 0; i < E.numrows; i++) editor_update_syntax(i); break;
                case 3: E.tabstop += dir; if (E.tabstop < 1) E.tabstop = 1; if (E.tabstop > 16) E.tabstop = 16;
                        for (int i = 0; i < E.numrows; i++) row_update(&E.row[i]); break;
                case 4: E.quit_confirm = !E.quit_confirm; break;
            }
        }
    }
    write(1, "\x1b[2J\x1b[H", 7);
    set_status("");
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
            if (E.dirty && E.quit_confirm && !E.quit_pending) {
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
            if (E.dirty && E.quit_confirm && !E.quit_pending) {
                set_status("Unsaved changes. Ctrl-Q again to force quit.");
                E.quit_pending = 1;
                return 1;
            }
            return 0;

        case KEY_CTRL('g'):
            goto_line();
            break;

        case KEY_CTRL('f'):
            editor_find();
            break;

        case KEY_CTRL('b'):
            E.cy = 0;
            E.cx = 0;
            E.rowoff = 0;
            E.coloff = 0;
            break;

        case KEY_CTRL('e'):
            E.cy = E.numrows == 0 ? 0 : E.numrows - 1;
            if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
            else E.cx = 0;
            break;

        case KEY_CTRL('x'):
            editor_cut_line();
            set_status("Line cut to clipboard");
            break;

        case KEY_CTRL('c'):
            editor_copy_line();
            set_status("Line copied to clipboard");
            break;

        case KEY_CTRL('v'):
            editor_paste();
            break;

        case KEY_CTRL('d'):
            editor_duplicate_line();
            break;

        case KEY_CTRL('n'):
            E.show_lineno = !E.show_lineno;
            recompute_lineno_width();
            set_status(E.show_lineno ? "Line numbers ON" : "Line numbers OFF");
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
            editor_delete_char_forward();
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
            editor_choose_language();
            break;

        case KEY_CTRL('p'):
            editor_settings();
            break;

        case KEY_CTRL('t'):
            editor_tree();
            break;

        case 0:
            break;

        default:
            if (c == '\t')               editor_insert_char('\t');
            else if (c >= 32 && c < 1000) {
                if (!editor_autopair(c)) editor_insert_char(c);
            }
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
    E.disk_size = -1;
    E.show_lineno = 1;
    E.lineno_width = 0;
    E.clipboard = NULL;
    E.clipboard_len = 0;
    E.clipboard_was_line = 0;
    E.autopairs = 1;
    E.tabstop = NEO_TABSTOP;
    E.quit_confirm = NEO_QUIT_CONFIRM;
    get_window_size();
    recompute_lineno_width();
}

int main(int argc, char **argv)
{

    if (!isatty(0)) {
        close(0);
        int tty_fd = open("/dev/tty", O_RDONLY, 0);
        if (tty_fd < 0) {
            fputs("neo: cannot open /dev/tty (stdin is a pipe)\n", stderr);
            return 1;
        }
        if (tty_fd != 0) { dup2(tty_fd, 0); close(tty_fd); }
    }

    init_editor();
    enable_raw_mode();

    const char *file_to_open = NULL;
    for (int i = 1; i < argc; i++) {
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
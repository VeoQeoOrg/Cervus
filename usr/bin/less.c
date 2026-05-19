#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <regex.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <cervus_util.h>

#define TIOCGWINSZ 0x5413
typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } less_winsize_t;

#define LESS_LINE_INIT 256
#define LESS_INIT_LINES 1024

typedef struct {
    char  *data;
    size_t size;
    size_t cap;
} line_t;

typedef struct {
    line_t *lines;
    size_t  count;
    size_t  cap;
} doc_t;

static doc_t g_doc;
static int   g_top = 0;
static int   g_term_rows = 24;
static int   g_term_cols = 80;
static int   g_running = 1;
static int   g_show_lineno = 0;
static char  g_status_msg[256];
static int   g_status_visible = 0;
static const char *g_filename = "(stdin)";

static struct termios g_orig_tio;
static int g_in_raw = 0;

static regex_t g_re;
static int     g_re_ready = 0;
static char    g_search_pat[256];
static int     g_search_dir = 1;

static int doc_append_line(doc_t *d, const char *s, size_t n)
{
    if (d->count >= d->cap) {
        size_t nc = d->cap ? d->cap * 2 : LESS_INIT_LINES;
        line_t *nl = (line_t *)realloc(d->lines, nc * sizeof(line_t));
        if (!nl) return -1;
        d->lines = nl;
        d->cap = nc;
    }
    line_t *L = &d->lines[d->count];
    L->cap = n + 1;
    L->data = (char *)malloc(L->cap);
    if (!L->data) return -1;
    memcpy(L->data, s, n);
    L->data[n] = '\0';
    L->size = n;
    d->count++;
    return 0;
}

static int load_fd(int fd)
{
    char buf[8192];
    char *line = (char *)malloc(LESS_LINE_INIT);
    size_t lcap = LESS_LINE_INIT;
    size_t llen = 0;
    if (!line) return -1;

    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                if (doc_append_line(&g_doc, line, llen) < 0) { free(line); return -1; }
                llen = 0;
                continue;
            }
            if (llen + 1 >= lcap) {
                size_t nc = lcap * 2;
                char *nb = (char *)realloc(line, nc);
                if (!nb) { free(line); return -1; }
                line = nb; lcap = nc;
            }
            line[llen++] = c;
        }
    }
    if (llen > 0) doc_append_line(&g_doc, line, llen);
    free(line);
    return 0;
}

static void less_update_size(void)
{
    less_winsize_t ws;
    if (syscall3(SYS_IOCTL, 1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 16 && ws.ws_row >= 4) {
        g_term_cols = ws.ws_col;
        g_term_rows = ws.ws_row;
    }
}

static void restore_term(void)
{
    fputs("\x1b[0m\x1b[?7h\x1b[2J\x1b[H\x1b[?25h", stdout);
    fflush(stdout);
    if (g_in_raw) {
        tcsetattr(0, TCSAFLUSH, &g_orig_tio);
        g_in_raw = 0;
    }
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

static void enter_raw(void)
{
    if (!isatty(0)) return;
    if (tcgetattr(0, &g_orig_tio) < 0) return;
    struct termios raw = g_orig_tio;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &raw) == 0) g_in_raw = 1;
    fputs("\x1b[?7l\x1b[?25l\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

static int read_key(void)
{
    unsigned char c;
    if (read(0, &c, 1) != 1) return -1;
    if (c != 0x1B) return c;
    unsigned char s0, s1;
    if (read(0, &s0, 1) != 1) return 0x1B;
    if (s0 != '[') return 0x1B;
    if (read(0, &s1, 1) != 1) return 0x1B;
    if (s1 >= '0' && s1 <= '9') {
        unsigned char s2;
        if (read(0, &s2, 1) != 1) return 0x1B;
        if (s2 == '~') {
            switch (s1) {
                case '1': case '7': return 2000;
                case '4': case '8': return 2001;
                case '5': return 2002;
                case '6': return 2003;
                default: return 0x1B;
            }
        }
        return 0x1B;
    }
    switch (s1) {
        case 'A': return 1000;
        case 'B': return 1001;
        case 'C': return 1002;
        case 'D': return 1003;
        case 'H': return 2000;
        case 'F': return 2001;
        default:  return 0x1B;
    }
}

static int line_matches(const char *line)
{
    if (!g_re_ready) return 0;
    return regexec(&g_re, line, 0, NULL, 0) == 0;
}

static int find_next(int from, int dir)
{
    if (!g_re_ready || g_doc.count == 0) return -1;
    int i = from;
    int n = (int)g_doc.count;
    if (dir > 0) {
        for (; i < n; i++) if (line_matches(g_doc.lines[i].data)) return i;
    } else {
        for (; i >= 0; i--) if (line_matches(g_doc.lines[i].data)) return i;
    }
    return -1;
}

static void draw_status(void)
{
    fputs("\x1b[", stdout);
    printf("%d;1H", g_term_rows);
    fputs("\x1b[K\x1b[7m", stdout);
    int pct = 0;
    int visible_rows = g_term_rows - 1;
    int bottom = g_top + visible_rows;
    if ((int)g_doc.count > 0) {
        if (bottom >= (int)g_doc.count) pct = 100;
        else pct = (int)((uint64_t)bottom * 100 / (uint64_t)g_doc.count);
    }
    char msg[256];
    if (g_status_visible) {
        snprintf(msg, sizeof(msg), " %s ", g_status_msg);
    } else if ((int)g_doc.count > g_term_rows - 1) {
        snprintf(msg, sizeof(msg), " %s   lines %d-%d/%zu   %d%%   (h for help, q to quit) ",
                 g_filename, g_top + 1,
                 bottom > (int)g_doc.count ? (int)g_doc.count : bottom,
                 g_doc.count, pct);
    } else {
        snprintf(msg, sizeof(msg), " %s   (END)   (h for help, q to quit) ",
                 g_filename);
    }
    fputs(msg, stdout);
    int written = (int)strlen(msg);
    int pad = g_term_cols - written - 1;
    for (int x = 0; x < pad; x++) putchar(' ');
    fputs("\x1b[0m", stdout);
    fputs("\x1b[1;1H", stdout);
}

static void render_line(const line_t *L, int width)
{
    if (!g_re_ready || !g_search_pat[0]) {
        size_t n = L->size;
        if ((int)n > width) n = (size_t)width;
        fwrite(L->data, 1, n, stdout);
        fputs("\x1b[K", stdout);
        return;
    }
    int col = 0;
    const char *s = L->data;
    while (*s && col < width) {
        regmatch_t m;
        if (regexec(&g_re, s, 1, &m, 0) != 0) {
            int rem = (int)strlen(s);
            int chunk = (rem > width - col) ? (width - col) : rem;
            fwrite(s, 1, (size_t)chunk, stdout);
            col += chunk;
            break;
        }
        if (m.rm_so > 0) {
            int chunk = (int)m.rm_so;
            if (col + chunk > width) chunk = width - col;
            fwrite(s, 1, (size_t)chunk, stdout);
            col += chunk;
            if (col >= width) break;
            s += m.rm_so;
            m.rm_eo -= m.rm_so;
            m.rm_so = 0;
        }
        int hl_len = (int)(m.rm_eo - m.rm_so);
        if (hl_len <= 0) {
            if (*s) { putchar(*s); s++; col++; }
            continue;
        }
        if (col + hl_len > width) hl_len = width - col;
        fputs("\x1b[1;33m\x1b[7m", stdout);
        fwrite(s, 1, (size_t)hl_len, stdout);
        fputs("\x1b[0m", stdout);
        col += hl_len;
        s += hl_len;
    }
    fputs("\x1b[K", stdout);
}

static void draw_screen(void)
{
    less_update_size();
    if (g_top < 0) g_top = 0;
    int visible_rows = g_term_rows - 1;
    if (visible_rows < 1) visible_rows = 1;
    int max_top = (int)g_doc.count - visible_rows;
    if (max_top < 0) max_top = 0;
    if (g_top > max_top) g_top = max_top;

    fputs("\x1b[?25l", stdout);
    for (int i = 0; i < visible_rows; i++) {
        printf("\x1b[%d;1H\x1b[K", i + 1);
        int idx = g_top + i;
        if (idx >= (int)g_doc.count) continue;
        int width = g_term_cols;
        if (g_show_lineno) {
            int prefix_w = printf("\x1b[90m%6d\x1b[0m  ", idx + 1);
            (void)prefix_w;
            width -= 8;
            if (width < 1) width = 1;
        }
        render_line(&g_doc.lines[idx], width);
    }
    draw_status();
    fflush(stdout);
}

static int prompt_line(char prefix, char *out, int max)
{
    printf("\x1b[%d;1H\x1b[K", g_term_rows);
    fputs("\x1b[?25h", stdout);
    putchar(prefix);
    fflush(stdout);
    int n = 0;
    while (n < max - 1) {
        unsigned char c;
        if (read(0, &c, 1) != 1) { out[0] = '\0'; break; }
        if (c == '\r' || c == '\n') break;
        if (c == 27) { out[0] = '\0'; fputs("\x1b[?25l", stdout); fflush(stdout); return -1; }
        if (c == 127 || c == 8) {
            if (n > 0) { n--; fputs("\b \b", stdout); fflush(stdout); }
            continue;
        }
        out[n++] = (char)c;
        putchar((char)c);
        fflush(stdout);
    }
    out[n] = '\0';
    fputs("\x1b[?25l", stdout);
    fflush(stdout);
    return n;
}

static void compile_search(const char *pat)
{
    if (g_re_ready) { regfree(&g_re); g_re_ready = 0; }
    if (!pat || !pat[0]) { g_search_pat[0] = '\0'; return; }
    int cflags = REG_EXTENDED;
    if (regcomp(&g_re, pat, cflags) == 0) {
        g_re_ready = 1;
        strncpy(g_search_pat, pat, sizeof(g_search_pat) - 1);
        g_search_pat[sizeof(g_search_pat) - 1] = '\0';
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg), "bad pattern: %s", pat);
        g_status_visible = 1;
        g_search_pat[0] = '\0';
    }
}

static void show_help(void)
{
    fputs("\x1b[2J\x1b[H\x1b[?25l", stdout);
    fputs(C_BOLD C_CYAN "  Cervus less - keys" C_RESET "\r\n\r\n", stdout);
    fputs("    q              quit\r\n", stdout);
    fputs("    h              this help (any key to return)\r\n", stdout);
    fputs("    j, down        scroll one line down\r\n", stdout);
    fputs("    k, up          scroll one line up\r\n", stdout);
    fputs("    space, PgDn    scroll one page down\r\n", stdout);
    fputs("    b,     PgUp    scroll one page up\r\n", stdout);
    fputs("    g, Home        jump to first line\r\n", stdout);
    fputs("    G, End         jump to last line\r\n", stdout);
    fputs("    /pattern       search forward  (POSIX ERE)\r\n", stdout);
    fputs("    ?pattern       search backward\r\n", stdout);
    fputs("    n              next match\r\n", stdout);
    fputs("    N              previous match\r\n", stdout);
    fputs("    =, F           toggle line numbers\r\n", stdout);
    fputs("    Esc            cancel an in-progress prompt\r\n", stdout);
    fputs("\r\n  Press any key to return...", stdout);
    fflush(stdout);
    unsigned char c; (void)read(0, &c, 1);
}

int main(int argc, char **argv)
{
    static const char USAGE[] =
        "Usage: less [-N] [FILE]\n"
        "Page through FILE (or stdin) with regex search.\n"
        "\n"
        "Options:\n"
        "  -N             show line numbers in the left margin\n"
        "  --help         this message\n"
        "  --version      print version\n"
        "\n"
        "Keys:\n"
        "  q, Ctrl-C      quit\n"
        "  h              built-in help screen\n"
        "  j, down        scroll one line down\n"
        "  k, up          scroll one line up\n"
        "  space, PgDn    scroll one page down\n"
        "  b, PgUp        scroll one page up\n"
        "  g, Home        jump to first line\n"
        "  G, End         jump to last line\n"
        "  /PATTERN       search forward (POSIX ERE)\n"
        "  ?PATTERN       search backward\n"
        "  n              repeat last search in same direction\n"
        "  N              repeat last search in opposite direction\n"
        "  =, F           toggle line numbers\n"
        "  Esc            cancel an in-progress prompt\n"
        "\n"
        "Examples:\n"
        "  less /apps/shell.c\n"
        "  less -N /usr/share/man/man1/find.1\n"
        "  cat big.log | less\n";
    if (cervus_check_help_version(argc, argv, USAGE, "less")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    int fd = 0;
    int show_lineno = 0;
    const char *file_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-N") == 0) show_lineno = 1;
        else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fputs(USAGE, stderr); return 1;
        } else {
            file_arg = argv[i];
        }
    }
    g_show_lineno = show_lineno;

    if (file_arg) {
        char resolved[512];
        resolve_path(cwd, file_arg, resolved, sizeof(resolved));
        fd = open(resolved, O_RDONLY, 0);
        if (fd < 0) {
            fprintf(stderr, "less: cannot open '%s'\n", file_arg);
            return 1;
        }
        g_filename = file_arg;
    } else if (isatty(0)) {
        fputs("less: missing file argument\n", stderr);
        return 1;
    }

    memset(&g_doc, 0, sizeof(g_doc));
    if (load_fd(fd) < 0) {
        fprintf(stderr, "less: out of memory\n");
        if (fd > 0) close(fd);
        return 1;
    }
    if (fd > 0) close(fd);

    if (!isatty(1)) {
        for (size_t i = 0; i < g_doc.count; i++) {
            fputs(g_doc.lines[i].data, stdout);
            putchar('\n');
        }
        return 0;
    }

    enter_raw();
    atexit(restore_term);
    draw_screen();

    while (g_running) {
        int k = read_key();
        if (k < 0) break;
        g_status_visible = 0;
        int visible_rows = g_term_rows - 1;
        int max_top = (int)g_doc.count - visible_rows;
        if (max_top < 0) max_top = 0;

        switch (k) {
            case 'q': case 'Q': case 3: g_running = 0; break;
            case 'h': case 'H': case '?': /* fallthrough handled below */
                if (k == '?') {
                    char pat[200];
                    int n = prompt_line('?', pat, sizeof(pat));
                    if (n > 0) {
                        compile_search(pat);
                        g_search_dir = -1;
                        if (g_re_ready) {
                            int from = g_top - 1;
                            int hit = find_next(from, -1);
                            if (hit >= 0) g_top = hit;
                            else { snprintf(g_status_msg, sizeof(g_status_msg), "pattern not found"); g_status_visible = 1; }
                        }
                    }
                } else {
                    show_help();
                }
                break;
            case 'j': case 1001: if (g_top < max_top) g_top++; break;
            case 'k': case 1000: if (g_top > 0)       g_top--; break;
            case ' ': case 1003 /* page down via ESC[C? */: case 2003:
                g_top += visible_rows;
                if (g_top > max_top) g_top = max_top;
                break;
            case 'b': case 2002:
                g_top -= visible_rows;
                if (g_top < 0) g_top = 0;
                break;
            case 'g': case 2000: g_top = 0; break;
            case 'G': case 2001: g_top = max_top; break;
            case '/': {
                char pat[200];
                int n = prompt_line('/', pat, sizeof(pat));
                if (n > 0) {
                    compile_search(pat);
                    g_search_dir = 1;
                    if (g_re_ready) {
                        int hit = find_next(g_top + 1, 1);
                        if (hit < 0) hit = find_next(0, 1);
                        if (hit >= 0) g_top = hit;
                        else { snprintf(g_status_msg, sizeof(g_status_msg), "pattern not found"); g_status_visible = 1; }
                    }
                }
                break;
            }
            case 'n': {
                if (!g_re_ready) { snprintf(g_status_msg, sizeof(g_status_msg), "no pattern"); g_status_visible = 1; break; }
                int from = g_top + g_search_dir;
                int hit = find_next(from, g_search_dir);
                if (hit < 0 && g_search_dir > 0) hit = find_next(0, 1);
                if (hit < 0 && g_search_dir < 0) hit = find_next((int)g_doc.count - 1, -1);
                if (hit >= 0) g_top = hit;
                else { snprintf(g_status_msg, sizeof(g_status_msg), "pattern not found"); g_status_visible = 1; }
                break;
            }
            case 'N': {
                if (!g_re_ready) { snprintf(g_status_msg, sizeof(g_status_msg), "no pattern"); g_status_visible = 1; break; }
                int dir = -g_search_dir;
                int hit = find_next(g_top + dir, dir);
                if (hit >= 0) g_top = hit;
                else { snprintf(g_status_msg, sizeof(g_status_msg), "pattern not found"); g_status_visible = 1; }
                break;
            }
            case '=': case 'F': g_show_lineno = !g_show_lineno; break;
            default: continue;
        }
        draw_screen();
    }

    return 0;
}

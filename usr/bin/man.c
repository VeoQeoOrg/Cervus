#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <cervus_util.h>

#define TIOCGWINSZ 0x5413
typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } man_winsize_t;

static const char *MAN_PATHS[] = {
    "/usr/share/man",
    "/mnt/usr/share/man",
    NULL,
};

static int g_term_rows = 24;
static int g_term_cols = 80;
static struct termios g_orig_tio;
static int g_in_raw = 0;

static void man_restore_term(void)
{
    if (g_in_raw) {
        tcsetattr(0, TCSAFLUSH, &g_orig_tio);
        g_in_raw = 0;
        fputs("\x1b[0m\x1b[?25h", stdout);
        fflush(stdout);
    }
}

static void man_enter_raw(void)
{
    if (!isatty(0)) return;
    if (tcgetattr(0, &g_orig_tio) < 0) return;
    struct termios raw = g_orig_tio;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &raw) == 0) g_in_raw = 1;
}

static int read_key_blocking(void)
{
    unsigned char c;
    if (read(0, &c, 1) != 1) return -1;
    if (c != 0x1B) return c;
    unsigned char s0, s1;
    if (read(0, &s0, 1) != 1) return 0x1B;
    if (s0 != '[') return 0x1B;
    if (read(0, &s1, 1) != 1) return 0x1B;
    switch (s1) {
        case 'A': return 1000;
        case 'B': return 1001;
        case 'C': return 1002;
        case 'D': return 1003;
        case '5': { unsigned char z; (void)read(0, &z, 1); return 1004; }
        case '6': { unsigned char z; (void)read(0, &z, 1); return 1005; }
        default:  return 0x1B;
    }
}

static void update_size(void)
{
    man_winsize_t ws;
    if (syscall3(SYS_IOCTL, 1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 16 && ws.ws_row >= 6) {
        g_term_cols = ws.ws_col;
        g_term_rows = ws.ws_row;
    }
}

static int try_open(const char *path, int *out_fd)
{
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    *out_fd = fd;
    return 1;
}

static int find_page(const char *name, int explicit_section, char *out_path, size_t out_sz, int *out_fd)
{
    for (int p = 0; MAN_PATHS[p]; p++) {
        const char *base = MAN_PATHS[p];
        int sections[3] = {1, 2, 3};
        if (explicit_section >= 1 && explicit_section <= 3) {
            sections[0] = explicit_section;
            sections[1] = 0;
            sections[2] = 0;
        }
        for (int s = 0; s < 3 && sections[s]; s++) {
            int sec = sections[s];
            snprintf(out_path, out_sz, "%s/man%d/%s.%d", base, sec, name, sec);
            if (try_open(out_path, out_fd)) return sec;
            snprintf(out_path, out_sz, "%s/man%d/%s", base, sec, name);
            if (try_open(out_path, out_fd)) return sec;
        }
    }
    return -1;
}

static int is_header_line(const char *line)
{
    if (!line[0] || line[0] == ' ' || line[0] == '\t') return 0;
    for (const char *p = line; *p; p++) {
        if (*p == ' ' || *p == '\n') continue;
        if (*p >= 'a' && *p <= 'z') return 0;
        if (*p == '(' || *p == ')') continue;
    }
    return 1;
}

static int render_line(const char *line)
{
    if (is_header_line(line)) {
        fputs("\x1b[1m", stdout);
        fputs(line, stdout);
        fputs("\x1b[0m", stdout);
    } else {
        fputs(line, stdout);
    }
    putchar('\n');
    return 1;
}

static int pager(int fd)
{
    update_size();
    int page_rows = g_term_rows > 4 ? g_term_rows - 1 : g_term_rows;
    char buf[8192];
    char line[1024];
    int  li = 0;
    int  shown = 0;
    int  paged = isatty(1) && isatty(0);

    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || li >= (int)sizeof(line) - 1) {
                line[li] = '\0';
                render_line(line);
                li = 0;
                shown++;
                if (paged && shown >= page_rows) {
                    fputs("\x1b[7m--More-- (space:page, enter:line, q:quit)\x1b[0m", stdout);
                    fflush(stdout);
                    int k;
                    for (;;) {
                        k = read_key_blocking();
                        if (k == 'q' || k == 'Q' || k == 3 || k == -1) {
                            fputs("\r\x1b[K", stdout);
                            return 0;
                        }
                        if (k == ' ') { shown = 0; break; }
                        if (k == '\r' || k == '\n' || k == 1001) { shown = page_rows - 1; break; }
                    }
                    fputs("\r\x1b[K", stdout);
                }
            } else {
                line[li++] = c;
            }
        }
    }
    if (li > 0) {
        line[li] = '\0';
        render_line(line);
    }
    return 0;
}

static void list_all(void)
{
    fputs("Available manual pages:\n\n", stdout);
    for (int p = 0; MAN_PATHS[p]; p++) {
        for (int sec = 1; sec <= 3; sec++) {
            char dir[256];
            snprintf(dir, sizeof(dir), "%s/man%d", MAN_PATHS[p], sec);
            DIR *d = opendir(dir);
            if (!d) continue;
            int header_printed = 0;
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                if (!header_printed) {
                    printf("  Section %d (%s):\n",
                           sec,
                           sec == 1 ? "user commands"
                         : sec == 2 ? "system calls"
                                    : "library functions");
                    header_printed = 1;
                }
                char nm[64];
                strncpy(nm, de->d_name, sizeof(nm) - 1);
                nm[sizeof(nm) - 1] = '\0';
                char *dot = strrchr(nm, '.');
                if (dot) *dot = '\0';
                printf("    %s(%d)\n", nm, sec);
            }
            closedir(d);
        }
    }
}

static const char USAGE[] =
    "Usage: man [section] page\n"
    "       man -l   list all available pages\n"
    "       man -a   alias for -l\n"
    "\n"
    "Sections:\n"
    "  1  user commands\n"
    "  2  system calls\n"
    "  3  library functions\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "man")) return 0;
    argc = cervus_filter_args(argc, argv);

    if (argc < 2) { fputs(USAGE, stderr); return 1; }

    if (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "-a") == 0
        || strcmp(argv[1], "--list") == 0) {
        list_all();
        return 0;
    }

    int explicit_section = 0;
    const char *name = NULL;
    if (argc >= 3 && argv[1][0] >= '1' && argv[1][0] <= '3' && argv[1][1] == '\0') {
        explicit_section = argv[1][0] - '0';
        name = argv[2];
    } else {
        name = argv[1];
    }

    char path[256];
    int  fd = -1;
    int  sec = find_page(name, explicit_section, path, sizeof(path), &fd);
    if (sec < 0) {
        fprintf(stderr, "man: no manual entry for '%s'\n", name);
        return 1;
    }

    man_enter_raw();
    atexit(man_restore_term);

    fprintf(stdout, "\x1b[1m%s(%d)\x1b[0m  -  Cervus Manual\n\n", name, sec);
    pager(fd);
    close(fd);
    return 0;
}

#include "../apps/cervus_user.h"

#ifndef VFS_MAX_PATH
#define VFS_MAX_PATH 512
#endif

static int seq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a == (unsigned char)*b;
}
static void scpy(char *d, const char *s, size_t max) {
    size_t i = 0;
    while (i + 1 < max && s[i]) {
        d[i] = s[i];
        i++;
    }
    d[i] = '\0';
}
static size_t slen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void vt_right(int n) {
    if (n <= 0)
        return;
    char b[12];
    b[0] = '\x1b';
    b[1] = '[';
    int i = 2;
    if (n >= 100)
        b[i++] = '0' + n / 100;
    if (n >= 10)
        b[i++] = '0' + (n / 10) % 10;
    b[i++] = '0' + n % 10;
    b[i++] = 'C';
    write(1, b, i);
}
static void vt_left(int n) {
    if (n <= 0)
        return;
    char b[12];
    b[0] = '\x1b';
    b[1] = '[';
    int i = 2;
    if (n >= 100)
        b[i++] = '0' + n / 100;
    if (n >= 10)
        b[i++] = '0' + (n / 10) % 10;
    b[i++] = '0' + n % 10;
    b[i++] = 'D';
    write(1, b, i);
}
static void vt_eol(void) {
    ws("\x1b[K");
}

#define HIST_MAX 20
#define LINE_MAX 512
static char history[HIST_MAX][LINE_MAX];
static int hist_count = 0, hist_head = 0;
static void hist_push(const char *l) {
    if (!l[0])
        return;
    if (hist_count > 0) {
        int last = (hist_head + hist_count - 1) % HIST_MAX;
        if (seq(history[last], l))
            return;
    }
    int idx = (hist_head + hist_count) % HIST_MAX;
    scpy(history[idx], l, LINE_MAX);
    if (hist_count < HIST_MAX)
        hist_count++;
    else
        hist_head = (hist_head + 1) % HIST_MAX;
}
static const char *hist_get(int n) {
    if (n < 1 || n > hist_count)
        return (void *)0;
    return history[(hist_head + hist_count - n) % HIST_MAX];
}

static char cwd[VFS_MAX_PATH];
static int prompt_visible_len = 0;

static void print_prompt(void) {
    ws(C_GREEN "cervus" C_RESET ":" C_BLUE);
    ws(cwd);
    ws(C_RESET "$ ");
    prompt_visible_len = 9 + (int)slen(cwd);
}

static int get_max_input(void) {
    int cols = 160, avail = cols - prompt_visible_len - 1;
    if (avail < 20)
        avail = 20;
    if (avail > LINE_MAX - 1)
        avail = LINE_MAX - 1;
    return avail;
}

static int readline_edit(char *buf, int maxlen) {
    int limit = get_max_input();
    if (limit > maxlen - 1)
        limit = maxlen - 1;
    int len = 0, pos = 0, hidx = 0;
    char saved[LINE_MAX];
    saved[0] = '\0';
    buf[0] = '\0';
    for (;;) {
        char c;
        if (read(0, &c, 1) <= 0)
            return -1;
        if (c == '\x1b') {
            char s[4];
            if (read(0, &s[0], 1) <= 0)
                continue;
            if (s[0] != '[')
                continue;
            if (read(0, &s[1], 1) <= 0)
                continue;
            if (s[1] == 'A') {
                if (hidx == 0)
                    scpy(saved, buf, LINE_MAX);
                if (hidx < hist_count) {
                    hidx++;
                    const char *h = hist_get(hidx);
                    if (h) {
                        vt_left(pos);
                        vt_eol();
                        int hl = slen(h);
                        if (hl > limit)
                            hl = limit;
                        for (int i = 0; i < hl; i++)
                            buf[i] = h[i];
                        buf[hl] = '\0';
                        len = hl;
                        pos = len;
                        write(1, buf, len);
                    }
                }
                continue;
            }
            if (s[1] == 'B') {
                if (hidx > 0) {
                    hidx--;
                    const char *h = (hidx == 0) ? saved : hist_get(hidx);
                    if (!h)
                        h = "";
                    vt_left(pos);
                    vt_eol();
                    int hl = slen(h);
                    if (hl > limit)
                        hl = limit;
                    for (int i = 0; i < hl; i++)
                        buf[i] = h[i];
                    buf[hl] = '\0';
                    len = hl;
                    pos = len;
                    write(1, buf, len);
                }
                continue;
            }
            if (s[1] == 'C') {
                if (pos < len) {
                    vt_right(1);
                    pos++;
                }
                continue;
            }
            if (s[1] == 'D') {
                if (pos > 0) {
                    vt_left(1);
                    pos--;
                }
                continue;
            }
            if (s[1] == 'H') {
                if (pos > 0) {
                    vt_left(pos);
                    pos = 0;
                }
                continue;
            }
            if (s[1] == 'F') {
                if (pos < len) {
                    vt_right(len - pos);
                    pos = len;
                }
                continue;
            }
            if (s[1] == '3') {
                char tilde;
                read(0, &tilde, 1);
                if (pos < len) {
                    for (int i = pos; i < len - 1; i++)
                        buf[i] = buf[i + 1];
                    len--;
                    buf[len] = '\0';
                    write(1, buf + pos, len - pos);
                    vt_eol();
                    if (len > pos)
                        vt_left(len - pos);
                }
                continue;
            }
            continue;
        }
        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            if (pos < len)
                vt_right(len - pos);
            wn();
            return len;
        }
        if (c == 3) {
            ws("^C");
            wn();
            buf[0] = '\0';
            return 0;
        }
        if (c == 4) {
            if (len == 0)
                return -1;
            continue;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                for (int i = pos - 1; i < len - 1; i++)
                    buf[i] = buf[i + 1];
                len--;
                pos--;
                buf[len] = '\0';
                vt_left(1);
                write(1, buf + pos, len - pos);
                vt_eol();
                if (len > pos)
                    vt_left(len - pos);
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7F) {
            if (len >= limit)
                continue;
            for (int i = len; i > pos; i--)
                buf[i] = buf[i - 1];
            buf[pos] = c;
            len++;
            buf[len] = '\0';
            write(1, buf + pos, len - pos);
            pos++;
            if (len > pos)
                vt_left(len - pos);
            continue;
        }
    }
}

#define MAX_ARGS 32
static int tokenize(char *line, char *argv[], int maxargs) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (argc >= maxargs - 1)
            break;
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"')
                p++;
            if (*p)
                *p++ = '\0';
        } else if (*p == '\'') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '\'')
                p++;
            if (*p)
                *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = '\0';
        }
    }
    argv[argc] = (void *)0;
    return argc;
}

static void path_join_s(const char *base, const char *name, char *out, size_t sz) {
    if (!name || !name[0]) {
        scpy(out, base, sz);
        return;
    }
    if (name[0] == '/') {
        scpy(out, name, sz);
        return;
    }
    scpy(out, base, sz);
    size_t bl = slen(out);
    if (bl > 0 && out[bl - 1] != '/' && bl + 1 < sz) {
        out[bl] = '/';
        out[bl + 1] = '\0';
        bl++;
    }
    size_t nl = slen(name);
    if (bl + nl + 1 < sz)
        memcpy(out + bl, name, nl + 1);
}

static void path_norm_s(char *path) {
    char tmp[VFS_MAX_PATH];
    scpy(tmp, path, sizeof(tmp));
    char *parts[64];
    int np = 0;
    char *p = tmp;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        char *s = p;
        while (*p && *p != '/')
            p++;
        if (*p)
            *p++ = '\0';
        if (seq(s, "."))
            continue;
        if (seq(s, "..")) {
            if (np > 0)
                np--;
            continue;
        }
        if (np < 64)
            parts[np++] = s;
    }
    char out[VFS_MAX_PATH];
    size_t ol = 0;
    for (int i = 0; i < np; i++) {
        out[ol++] = '/';
        size_t pl = slen(parts[i]);
        memcpy(out + ol, parts[i], pl);
        ol += pl;
    }
    out[ol] = '\0';
    if (ol == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
    scpy(path, out, VFS_MAX_PATH);
}

static void cmd_help(void) {
    wn();
    ws("  " C_CYAN "Cervus Shell" C_RESET " - commands\n");
    ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
    ws("  " C_BOLD "help" C_RESET "          show this message\n");
    ws("  " C_BOLD "cd" C_RESET " <dir>      change directory\n");
    ws("  " C_BOLD "exit" C_RESET "          quit shell\n");
    ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
    ws("  Programs in " C_BOLD "/bin" C_RESET ":\n");
    ws("  ls, cat, echo, pwd, clear, uname, meminfo, cpuinfo\n");
    ws("  ps, kill, find, stat, wc, yes, sleep\n");
    ws("  " C_RED "shutdown" C_RESET ", " C_CYAN "reboot" C_RESET "\n");
    ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
    ws("  " C_BOLD "Operators:" C_RESET "  cmd1 " C_YELLOW ";" C_RESET
       " cmd2   " C_YELLOW "&&" C_RESET "   " C_YELLOW "||" C_RESET "\n");
    ws("  " C_BOLD "Ctrl+C" C_RESET "    interrupt command\n");
    ws("  " C_BOLD "Arrows" C_RESET "    cursor / history\n");
    ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
    ws("  Programs in " C_BOLD "/apps" C_RESET " run from that dir:\n");
    ws("    cd /apps && hello\n");
    wn();
}

static int cmd_cd(const char *path) {
    if (!path || !path[0])
        path = "/";
    char np[VFS_MAX_PATH];
    path_join_s(cwd, path, np, sizeof(np));
    path_norm_s(np);
    cervus_stat_t st;
    if (stat(np, &st) < 0) {
        ws(C_RED "cd: not found: " C_RESET);
        ws(path);
        wn();
        return 1;
    }
    if (st.st_type != 1) {
        ws(C_RED "cd: not a dir: " C_RESET);
        ws(path);
        wn();
        return 1;
    }
    scpy(cwd, np, sizeof(cwd));
    return 0;
}

static int run_single(char *line) {
    char buf[LINE_MAX];
    scpy(buf, line, LINE_MAX);
    char *argv[MAX_ARGS];
    int argc = tokenize(buf, argv, MAX_ARGS);
    if (!argc)
        return 0;
    const char *cmd = argv[0];

    if (seq(cmd, "help")) {
        cmd_help();
        return 0;
    }
    if (seq(cmd, "exit")) {
        ws("Goodbye!\n");
        exit(0);
    }
    if (seq(cmd, "cd")) {
        return cmd_cd(argc > 1 ? argv[1] : (void *)0);
    }

    char binpath[VFS_MAX_PATH];
    cervus_stat_t st;

    if (cmd[0] == '/' || cmd[0] == '.') {

        scpy(binpath, cmd, sizeof(binpath));
    } else {

        char t_bin[VFS_MAX_PATH];
        path_join_s("/bin", cmd, t_bin, sizeof(t_bin));

        char t_cwd[VFS_MAX_PATH];
        int cwd_in_apps = (cwd[0] == '/' && cwd[1] == 'a' && cwd[2] == 'p' &&
                           cwd[3] == 'p' && cwd[4] == 's' &&
                           (cwd[5] == '\0' || cwd[5] == '/'));
        path_join_s(cwd, cmd, t_cwd, sizeof(t_cwd));
        path_norm_s(t_cwd);

        if (stat(t_bin, &st) == 0 && st.st_type != 1) {
            scpy(binpath, t_bin, sizeof(binpath));
        } else if (cwd_in_apps && stat(t_cwd, &st) == 0 && st.st_type != 1) {
            scpy(binpath, t_cwd, sizeof(binpath));
        } else {
            ws(C_RED "not found: " C_RESET);
            ws(cmd);
            wn();
            return 127;
        }
    }

    char *real_argv[MAX_ARGS + 1];
    int real_argc = argc;
    real_argv[0] = binpath;
    int _cwd_cmd = seq(cmd, "pwd") || (slen(binpath) >= 3 && seq(binpath + slen(binpath) - 3, "pwd")) || seq(cmd, "ls") || (slen(binpath) >= 2 && seq(binpath + slen(binpath) - 2, "ls"));
    if (_cwd_cmd) {

        if (argc < 2) {
            real_argv[1] = cwd;
            real_argv[2] = (void *)0;
            real_argc = 2;
        } else {
            for (int i = 1; i < argc; i++)
                real_argv[i] = argv[i];
            real_argv[argc] = (void *)0;
        }
    } else {
        for (int i = 1; i < argc; i++)
            real_argv[i] = argv[i];
        real_argv[argc] = (void *)0;
    }

    pid_t child = fork();
    if (child < 0) {
        ws(C_RED "fork failed" C_RESET "\n");
        return 1;
    }
    if (child == 0) {
        execve(binpath, (const char **)real_argv, (void *)0);
        ws(C_RED "exec failed: " C_RESET);
        ws(binpath);
        wn();
        exit(127);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return (status >> 8) & 0xFF;
}

typedef enum { CH_NONE = 0,
               CH_SEQ,
               CH_AND,
               CH_OR } chain_t;

static void run_command(char *line) {
    char work[LINE_MAX];
    scpy(work, line, LINE_MAX);
    char *segs[64];
    chain_t ops[64];
    int ns = 0;
    segs[0] = work;
    ops[0] = CH_NONE;
    ns = 1;
    char *p = work;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p && *p != '"')
                p++;
            if (*p)
                p++;
            continue;
        }
        if (*p == '\'') {
            p++;
            while (*p && *p != '\'')
                p++;
            if (*p)
                p++;
            continue;
        }
        if (*p == '&' && *(p + 1) == '&') {
            *p = '\0';
            p += 2;
            while (*p == ' ')
                p++;
            ops[ns] = CH_AND;
            segs[ns] = p;
            ns++;
            continue;
        }
        if (*p == '|' && *(p + 1) == '|') {
            *p = '\0';
            p += 2;
            while (*p == ' ')
                p++;
            ops[ns] = CH_OR;
            segs[ns] = p;
            ns++;
            continue;
        }
        if (*p == ';') {
            *p = '\0';
            p++;
            while (*p == ' ')
                p++;
            ops[ns] = CH_SEQ;
            segs[ns] = p;
            ns++;
            continue;
        }
        p++;
    }
    int rc = 0;
    for (int i = 0; i < ns; i++) {
        char *s = segs[i];
        while (*s == ' ' || *s == '\t')
            s++;
        size_t sl = slen(s);
        while (sl > 0 && (s[sl - 1] == ' ' || s[sl - 1] == '\t'))
            s[--sl] = '\0';
        if (!s[0])
            continue;
        if (i > 0) {
            if (ops[i] == CH_AND && rc != 0)
                continue;
            if (ops[i] == CH_OR && rc == 0)
                continue;
        }
        rc = run_single(s);
    }
}

static void print_motd(void) {
    int fd = open("/etc/motd", O_RDONLY, 0);
    if (fd < 0) {
        wn();
        ws("  Cervus OS v0.0.1\n  Type 'help' for commands.\n");
        wn();
        return;
    }
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) {
        buf[n] = '\0';
        write(1, buf, n);
    }
}

__attribute__((naked)) void _start(void) {
    asm volatile(
        "mov %%rsp, %%rdi\n"
        "and $-16, %%rsp\n"
        "call _start_main\n"
        "xor %%rdi, %%rdi\n"
        "xor %%rax, %%rax\n"
        "syscall\n"
        ".byte 0xf4\n"
        "jmp . - 1\n" ::: "memory");
}

__attribute__((noreturn)) void _start_main(uint64_t *initial_rsp) {
    (void)initial_rsp;
    scpy(cwd, "/", sizeof(cwd));
    print_motd();
    char line[LINE_MAX];
    for (;;) {
        print_prompt();
        int n = readline_edit(line, LINE_MAX);
        if (n < 0) {
            ws("\nSession ended. Restarting shell...\n");
            scpy(cwd, "/", sizeof(cwd));
            print_motd();
            continue;
        }
        int len = slen(line);
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t'))
            line[--len] = '\0';
        if (len > 0) {
            hist_push(line);
            run_command(line);
        }
    }
    __builtin_unreachable();
}
#include "../apps/cervus_user.h"

#ifndef VFS_MAX_PATH
#define VFS_MAX_PATH 1024
#endif

static int seq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a == (unsigned char)*b;
}
static void scpy(char *d, const char *s, size_t max) {
    size_t i = 0;
    while (i + 1 < max && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}
static size_t slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

#define TIOCGWINSZ  0x5413
#define TIOCGCURSOR 0x5480

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } cervus_winsize_t;
typedef struct { uint32_t row, col; } cervus_cursor_pos_t;

static inline int ioctl(int fd, unsigned long req, void *arg) {
    return (int)syscall3(SYS_IOCTL, fd, req, arg);
}

static int g_cols = 80;
static int g_rows = 25;

static void term_update_size(void) {
    cervus_winsize_t ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 8 && ws.ws_row >= 2) {
        g_cols = (int)ws.ws_col;
        g_rows = (int)ws.ws_row;
    }
}

static int term_get_cursor_row(void) {
    cervus_cursor_pos_t cp;
    if (ioctl(1, TIOCGCURSOR, &cp) == 0)
        return (int)cp.row;
    return 0;
}

static int itoa_buf(char *b, int n) {
    if (n <= 0) { b[0] = '0'; return 1; }
    int len = 0, tmp = n;
    while (tmp) { tmp /= 10; len++; }
    for (int i = len - 1; i >= 0; i--) { b[i] = '0' + n % 10; n /= 10; }
    return len;
}

static void vt_up(int n) {
    if (n <= 0) return;
    char b[12] = "\x1b["; int i = 2;
    i += itoa_buf(b + i, n); b[i++] = 'A';
    write(1, b, i);
}

static void vt_down(int n) {
    if (n <= 0) return;
    char b[12] = "\x1b["; int i = 2;
    i += itoa_buf(b + i, n); b[i++] = 'B';
    write(1, b, i);
}

static void vt_right(int n) {
    if (n <= 0) return;
    char b[12] = "\x1b["; int i = 2;
    i += itoa_buf(b + i, n); b[i++] = 'C';
    write(1, b, i);
}

static void vt_left(int n) {
    if (n <= 0) return;
    char b[12] = "\x1b["; int i = 2;
    i += itoa_buf(b + i, n); b[i++] = 'D';
    write(1, b, i);
}

static void vt_eol(void) { ws("\x1b[K"); }

static void vt_cr(void) { write(1, "\r", 1); }

static void vt_goto(int row, int col) {
    char b[24] = "\x1b["; int i = 2;
    i += itoa_buf(b + i, row + 1); b[i++] = ';';
    i += itoa_buf(b + i, col + 1); b[i++] = 'H';
    write(1, b, i);
}

#define HIST_MAX  1024
#define LINE_MAX  1024

static char history[HIST_MAX][LINE_MAX];
static int hist_count = 0, hist_head = 0;

static void hist_push(const char *l) {
    if (!l[0]) return;
    if (hist_count > 0) {
        int last = (hist_head + hist_count - 1) % HIST_MAX;
        if (seq(history[last], l)) return;
    }
    int idx = (hist_head + hist_count) % HIST_MAX;
    scpy(history[idx], l, LINE_MAX);
    if (hist_count < HIST_MAX) hist_count++;
    else hist_head = (hist_head + 1) % HIST_MAX;
}
static const char *hist_get(int n) {
    if (n < 1 || n > hist_count) return (void *)0;
    return history[(hist_head + hist_count - n) % HIST_MAX];
}

#define ENV_MAX_VARS  128
#define ENV_NAME_MAX   64
#define ENV_VAL_MAX   512

typedef struct {
    char name[ENV_NAME_MAX];
    char value[ENV_VAL_MAX];
} env_var_t;

static env_var_t g_env[ENV_MAX_VARS];
static int       g_env_count = 0;

static int env_find(const char *name) {
    for (int i = 0; i < g_env_count; i++)
        if (seq(g_env[i].name, name)) return i;
    return -1;
}

static const char *env_get(const char *name) {
    int i = env_find(name);
    return (i >= 0) ? g_env[i].value : "";
}

static void env_set(const char *name, const char *value) {
    int i = env_find(name);
    if (i >= 0) {
        scpy(g_env[i].value, value, ENV_VAL_MAX);
        return;
    }
    if (g_env_count >= ENV_MAX_VARS) return;
    scpy(g_env[g_env_count].name,  name,  ENV_NAME_MAX);
    scpy(g_env[g_env_count].value, value, ENV_VAL_MAX);
    g_env_count++;
}

static void env_unset(const char *name) {
    int i = env_find(name);
    if (i < 0) return;
    g_env[i] = g_env[--g_env_count];
}

static int g_last_rc = 0;

static void expand_vars(const char *src, char *dst, size_t dsz) {
    size_t di = 0;
    for (const char *p = src; *p && di + 1 < dsz; ) {
        if (*p != '$') { dst[di++] = *p++; continue; }
        p++;
        if (*p == '?') {
            char tmp[12]; int ti = 0;
            int v = g_last_rc;
            if (v == 0) { tmp[ti++] = '0'; }
            else {
                char rev[12]; int ri = 0;
                while (v > 0) { rev[ri++] = '0' + v % 10; v /= 10; }
                for (int x = ri - 1; x >= 0; x--) tmp[ti++] = rev[x];
            }
            for (int x = 0; x < ti && di + 1 < dsz; x++) dst[di++] = tmp[x];
            p++;
            continue;
        }
        int braced = (*p == '{');
        if (braced) p++;
        char name[ENV_NAME_MAX]; int ni = 0;
        while (*p && ni + 1 < (int)ENV_NAME_MAX) {
            char c = *p;
            if (braced) {
                if (c == '}') { p++; break; }
            } else {
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_')) break;
            }
            name[ni++] = c; p++;
        }
        name[ni] = '\0';
        if (ni == 0) { dst[di++] = '$'; continue; }
        const char *val = env_get(name);
        for (; *val && di + 1 < dsz; val++) dst[di++] = *val;
    }
    dst[di] = '\0';
}

static char cwd[VFS_MAX_PATH];
static int prompt_len = 0;

static void print_prompt(void) {
    ws(C_GREEN "cervus" C_RESET ":" C_BLUE);
    ws(cwd);
    ws(C_RESET "$ ");
    prompt_len = 9 + (int)slen(cwd);
}

static int g_start_row = 0;

static void sync_start_row(int cur_logical_pos) {
    int real_row = term_get_cursor_row();
    int abs_pos = prompt_len + cur_logical_pos;
    int row_offset = abs_pos / g_cols;
    g_start_row = real_row - row_offset;
    if (g_start_row < 0) g_start_row = 0;
}

static void input_pos_to_screen(int pos, int *row, int *col) {
    int abs = prompt_len + pos;
    *row = g_start_row + abs / g_cols;
    *col = abs % g_cols;
}

static void cursor_to(int pos) {
    int row, col;
    input_pos_to_screen(pos, &row, &col);
    if (row >= g_rows) row = g_rows - 1;
    if (row < 0) row = 0;
    vt_goto(row, col);
}

static int last_row_of(int len) {
    int abs = prompt_len + len;
    return g_start_row + (abs > 0 ? (abs - 1) : 0) / g_cols;
}

static void redraw(const char *buf, int from, int new_len, int old_len, int pos) {
    cursor_to(from);

    if (new_len > from)
        write(1, buf + from, new_len - from);

    sync_start_row(new_len);

    if (old_len > new_len) {
        int old_last = last_row_of(old_len);
        int new_last = last_row_of(new_len);

        cursor_to(new_len);
        vt_eol();

        for (int r = new_last + 1; r <= old_last; r++) {
            if (r >= g_rows) break;
            vt_goto(r, 0);
            vt_eol();
        }
    }

    cursor_to(pos);
}

static void replace_line(char *buf, int *len, int *pos,
                         const char *newtext, int newlen) {
    int old_len = *len;

    for (int i = 0; i < newlen; i++) buf[i] = newtext[i];
    buf[newlen] = '\0';
    *len = newlen;
    *pos = newlen;

    redraw(buf, 0, newlen, old_len, newlen);
}

static int readline_edit(char *buf, int maxlen) {
    term_update_size();

    {
        int real_row = term_get_cursor_row();
        int prompt_rows = prompt_len / g_cols;
        g_start_row = real_row - prompt_rows;
        if (g_start_row < 0) g_start_row = 0;
    }

    int len  = 0;
    int pos  = 0;
    int hidx = 0;
    static char saved[LINE_MAX];
    saved[0] = '\0';
    buf[0]   = '\0';

    for (;;) {
        char c;
        if (read(0, &c, 1) <= 0) return -1;

        if (c == '\x1b') {
            char s[4];
            if (read(0, &s[0], 1) <= 0) continue;
            if (s[0] != '[') continue;
            if (read(0, &s[1], 1) <= 0) continue;

            if (s[1] == 'A') {
                if (hidx == 0) scpy(saved, buf, LINE_MAX);
                if (hidx < hist_count) {
                    hidx++;
                    const char *h = hist_get(hidx);
                    if (h) {
                        int hl = (int)slen(h);
                        if (hl > maxlen - 1) hl = maxlen - 1;
                        replace_line(buf, &len, &pos, h, hl);
                    }
                }
                continue;
            }

            if (s[1] == 'B') {
                if (hidx > 0) {
                    hidx--;
                    const char *h = (hidx == 0) ? saved : hist_get(hidx);
                    if (!h) h = "";
                    int hl = (int)slen(h);
                    if (hl > maxlen - 1) hl = maxlen - 1;
                    replace_line(buf, &len, &pos, h, hl);
                }
                continue;
            }

            if (s[1] == 'C') {
                if (pos < len) { pos++; cursor_to(pos); }
                continue;
            }

            if (s[1] == 'D') {
                if (pos > 0) { pos--; cursor_to(pos); }
                continue;
            }

            if (s[1] == 'H') {
                pos = 0; cursor_to(0);
                continue;
            }

            if (s[1] == 'F') {
                pos = len; cursor_to(len);
                continue;
            }

            if (s[1] >= '1' && s[1] <= '6') {
                char tilde;
                read(0, &tilde, 1);
                if (tilde != '~') continue;

                if (s[1] == '3') {
                    if (pos < len) {
                        for (int i = pos; i < len - 1; i++) buf[i] = buf[i + 1];
                        len--; buf[len] = '\0';
                        redraw(buf, pos, len, len + 1, pos);
                    }
                }
                else if (s[1] == '1') {
                    pos = 0; cursor_to(0);
                }
                else if (s[1] == '4') {
                    pos = len; cursor_to(len);
                }
                continue;
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            cursor_to(len);
            wn();
            return len;
        }

        if (c == 3) {
            ws("^C"); wn();
            buf[0] = '\0';
            return 0;
        }

        if (c == 4) {
            if (len == 0) return -1;
            continue;
        }

        if (c == 1) {
            pos = 0; cursor_to(0);
            continue;
        }

        if (c == 5) {
            pos = len; cursor_to(len);
            continue;
        }

        if (c == 11) {
            if (pos < len) {
                int old_len = len;
                len = pos;
                buf[len] = '\0';
                redraw(buf, pos, len, old_len, pos);
            }
            continue;
        }

        if (c == 21) {
            if (pos > 0) {
                int old_len = len;
                int del = pos;
                for (int i = 0; i < len - del; i++) buf[i] = buf[i + del];
                len -= del; pos = 0; buf[len] = '\0';
                redraw(buf, 0, len, old_len, 0);
            }
            continue;
        }

        if (c == 23) {
            if (pos > 0) {
                int p = pos;
                while (p > 0 && buf[p - 1] == ' ') p--;
                while (p > 0 && buf[p - 1] != ' ') p--;
                int old_len = len;
                int del = pos - p;
                for (int i = p; i < len - del; i++) buf[i] = buf[i + del];
                len -= del; pos = p; buf[len] = '\0';
                redraw(buf, p, len, old_len, p);
            }
            continue;
        }

        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                int old_len = len;
                for (int i = pos - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; pos--; buf[len] = '\0';
                redraw(buf, pos, len, old_len, pos);
            }
            continue;
        }

        if (c >= 0x20 && c < 0x7F) {
            if (len >= maxlen - 1) continue;

            for (int i = len; i > pos; i--) buf[i] = buf[i - 1];
            buf[pos] = c; len++; buf[len] = '\0';

            cursor_to(pos);
            write(1, buf + pos, len - pos);
            sync_start_row(len);
            pos++;
            cursor_to(pos);
            continue;
        }
    }
}

#define MAX_ARGS 32

static int tokenize(char *line, char *argv[], int maxargs) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (argc >= maxargs - 1) break;

        char *out = p;
        argv[argc++] = out;
        int in_dquote = 0, in_squote = 0;
        while (*p) {
            if (in_dquote) {
                if (*p == '"') { in_dquote = 0; p++; }
                else           { *out++ = *p++; }
            } else if (in_squote) {
                if (*p == '\'') { in_squote = 0; p++; }
                else            { *out++ = *p++; }
            } else {
                if (*p == '"')  { in_dquote = 1; p++; }
                else if (*p == '\'') { in_squote = 1; p++; }
                else if (*p == ' ' || *p == '\t') { p++; break; }
                else { *out++ = *p++; }
            }
        }
        *out = '\0';
        if (in_dquote || in_squote) {
            argv[argc] = (void *)0;
            return -1;
        }
    }
    argv[argc] = (void *)0;
    return argc;
}

static void path_join_s(const char *base, const char *name, char *out, size_t sz) {
    if (!name || !name[0]) { scpy(out, base, sz); return; }
    if (name[0] == '/') { scpy(out, name, sz); return; }
    scpy(out, base, sz);
    size_t bl = slen(out);
    if (bl > 0 && out[bl - 1] != '/' && bl + 1 < sz) {
        out[bl] = '/'; out[bl + 1] = '\0'; bl++;
    }
    size_t nl = slen(name);
    if (bl + nl + 1 < sz) memcpy(out + bl, name, nl + 1);
}

static void path_norm_s(char *path) {
    char tmp[VFS_MAX_PATH];
    scpy(tmp, path, sizeof(tmp));
    char *parts[64]; int np = 0;
    char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *s = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = '\0';
        if (seq(s, ".")) continue;
        if (seq(s, "..")) { if (np > 0) np--; continue; }
        if (np < 64) parts[np++] = s;
    }
    char out[VFS_MAX_PATH]; size_t ol = 0;
    for (int i = 0; i < np; i++) {
        out[ol++] = '/';
        size_t pl = slen(parts[i]);
        memcpy(out + ol, parts[i], pl); ol += pl;
    }
    out[ol] = '\0';
    if (ol == 0) { out[0] = '/'; out[1] = '\0'; }
    scpy(path, out, VFS_MAX_PATH);
}

static void cmd_help(void) {
    wn();
    ws("  " C_CYAN "Cervus Shell" C_RESET " - commands\n");
    ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
    ws("  " C_BOLD "help" C_RESET "          show this message\n");
    ws("  " C_BOLD "cd" C_RESET " <dir>      change directory\n");
    ws("  " C_BOLD "export" C_RESET " N=V    set variable N to value V\n");
    ws("  " C_BOLD "unset" C_RESET " N       delete variable N\n");
    ws("  " C_BOLD "env" C_RESET "           list variables (external program)\n");
    ws("  " C_BOLD "exit" C_RESET "          quit shell\n");
    ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
    ws("  Programs in " C_BOLD "/bin" C_RESET ":\n");
    ws("  ls, cat, echo, pwd, clear, uname, meminfo, cpuinfo\n");
    ws("  ps, kill, find, stat, wc, yes, sleep\n");
    ws("  " C_RED "shutdown" C_RESET ", " C_CYAN "reboot" C_RESET "\n");
    ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
    ws("  " C_BOLD "Operators:" C_RESET "  cmd1 " C_YELLOW ";" C_RESET
       " cmd2   " C_YELLOW "&&" C_RESET "   " C_YELLOW "||" C_RESET "\n");
    ws("  " C_BOLD "Variables:" C_RESET "  $VAR, ${VAR}, $? " C_GRAY "(exit code of last command)" C_RESET "\n");
    ws("  " C_BOLD "Ctrl+C" C_RESET "    interrupt\n");
    ws("  " C_BOLD "Ctrl+A/E" C_RESET "  move cursor to beginning/end of line\n");
    ws("  " C_BOLD "Ctrl+K" C_RESET "    delete from cursor to end\n");
    ws("  " C_BOLD "Ctrl+U" C_RESET "    delete from cursor to beginning\n");
    ws("  " C_BOLD "Ctrl+W" C_RESET "    delete word\n");
    ws("  " C_BOLD "Arrows" C_RESET "    cursor / history\n");
    ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
    wn();
}

static int cmd_cd(const char *path) {
    if (!path || !path[0]) path = "/";
    char np[VFS_MAX_PATH];
    path_join_s(cwd, path, np, sizeof(np));
    path_norm_s(np);
    cervus_stat_t st;
    if (stat(np, &st) < 0) {
        ws(C_RED "cd: not found: " C_RESET); ws(path); wn(); return 1;
    }
    if (st.st_type != 1) {
        ws(C_RED "cd: not a dir: " C_RESET); ws(path); wn(); return 1;
    }
    scpy(cwd, np, sizeof(cwd));
    return 0;
}

static int valid_varname(const char *s) {
    if (!s || !*s) return 0;
    char c = *s;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
        return 0;
    for (s++; *s; s++) {
        c = *s;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

static int cmd_export(int argc, char *argv[]) {
    if (argc < 2) {
        ws(C_RED "export: usage: export NAME=VALUE\n" C_RESET);
        return 1;
    }

    if (argc > 2) {
        ws(C_RED "export: invalid syntax (unexpected tokens after assignment)\n" C_RESET);
        ws(C_RED "       use: export NAME=VALUE or export NAME=\"multi word\"\n" C_RESET);
        return 1;
    }

    char *arg    = argv[1];
    char *eq_pos = arg;
    while (*eq_pos && *eq_pos != '=') eq_pos++;

    if (*eq_pos != '=') {
        if (!valid_varname(arg)) {
            ws(C_RED "export: '"); ws(arg);
            ws("': not a valid identifier\n" C_RESET);
            return 1;
        }
        env_set(arg, "");
        return 0;
    }

    *eq_pos = '\0';
    const char *name = arg;
    const char *val  = eq_pos + 1;

    if (!valid_varname(name)) {
        ws(C_RED "export: '"); ws(name);
        ws("': not a valid identifier\n" C_RESET);
        *eq_pos = '=';
        return 1;
    }

    if (*val == '\0') {
        ws(C_RED "export: value is empty; use export ");
        ws(name);
        ws("=\"\" for an empty string\n" C_RESET);
        *eq_pos = '=';
        return 1;
    }

    env_set(name, val);
    *eq_pos = '=';
    return 0;
}

static int cmd_unset(int argc, char *argv[]) {
    if (argc < 2) {
        ws(C_RED "unset: usage: unset NAME\n" C_RESET);
        return 1;
    }
    for (int i = 1; i < argc; i++)
        env_unset(argv[i]);
    return 0;
}

static int find_in_path(const char *cmd, char *out, size_t outsz) {
    const char *pathvar = env_get("PATH");
    if (!pathvar || !pathvar[0]) {
        path_join_s("/bin", cmd, out, outsz);
        cervus_stat_t st;
        return (stat(out, &st) == 0 && st.st_type != 1);
    }

    char tmp[ENV_VAL_MAX];
    scpy(tmp, pathvar, sizeof(tmp));
    char *p = tmp;
    while (*p) {
        char *seg = p;
        while (*p && *p != ':') p++;
        if (*p == ':') *p++ = '\0';
        if (!seg[0]) continue;
        char candidate[VFS_MAX_PATH];
        path_join_s(seg, cmd, candidate, sizeof(candidate));
        cervus_stat_t st;
        if (stat(candidate, &st) == 0 && st.st_type != 1) {
            scpy(out, candidate, outsz);
            return 1;
        }
    }
    return 0;
}

static int run_single(char *line) {
    char expanded[LINE_MAX];
    expand_vars(line, expanded, sizeof(expanded));

    char buf[LINE_MAX];
    scpy(buf, expanded, LINE_MAX);
    char *argv[MAX_ARGS];
    int argc = tokenize(buf, argv, MAX_ARGS);
    if (argc < 0) {
        ws(C_RED "syntax error: unclosed quote\n" C_RESET);
        return 1;
    }
    if (!argc) return 0;
    const char *cmd = argv[0];

    if (seq(cmd, "help"))   { cmd_help(); return 0; }
    if (seq(cmd, "exit"))   { ws("Goodbye!\n"); exit(0); }
    if (seq(cmd, "cd"))     { return cmd_cd(argc > 1 ? argv[1] : (void *)0); }
    if (seq(cmd, "export")) { return cmd_export(argc, argv); }
    if (seq(cmd, "unset"))  { return cmd_unset(argc, argv); }

    char binpath[VFS_MAX_PATH];
    cervus_stat_t st;

    if (cmd[0] == '/' || cmd[0] == '.') {
        scpy(binpath, cmd, sizeof(binpath));
    } else {
        if (!find_in_path(cmd, binpath, sizeof(binpath))) {
            int cwd_in_apps = (cwd[0] == '/' && cwd[1] == 'a' && cwd[2] == 'p' &&
                               cwd[3] == 'p' && cwd[4] == 's' &&
                               (cwd[5] == '\0' || cwd[5] == '/'));
            char t_cwd[VFS_MAX_PATH];
            path_join_s(cwd, cmd, t_cwd, sizeof(t_cwd));
            path_norm_s(t_cwd);

            if (cwd_in_apps && stat(t_cwd, &st) == 0 && st.st_type != 1) {
                scpy(binpath, t_cwd, sizeof(binpath));
            } else {
                ws(C_RED "not found: " C_RESET); ws(cmd); wn(); return 127;
            }
        }
    }

    static const char *cwd_as_arg[] = {"pwd", "ls", NULL};
    int _inject_cwd_arg = 0;
    for (int _k = 0; cwd_as_arg[_k]; _k++)
        if (seq(cmd, cwd_as_arg[_k])) { _inject_cwd_arg = 1; break; }

#define REAL_ARGV_MAX (MAX_ARGS + ENV_MAX_VARS + 4)
    char *real_argv_buf[REAL_ARGV_MAX];
    static char _cwd_flag[VFS_MAX_PATH + 8];
    static char _env_flags[ENV_MAX_VARS][ENV_NAME_MAX + ENV_VAL_MAX + 8];

    int ri = 0;
    real_argv_buf[ri++] = binpath;

    if (_inject_cwd_arg && argc < 2) {
        real_argv_buf[ri++] = cwd;
    } else {
        for (int i = 1; i < argc; i++) real_argv_buf[ri++] = argv[i];
    }

    _cwd_flag[0]='-'; _cwd_flag[1]='-'; _cwd_flag[2]='c';
    _cwd_flag[3]='w'; _cwd_flag[4]='d'; _cwd_flag[5]='=';
    scpy(_cwd_flag + 6, cwd, sizeof(_cwd_flag) - 6);
    real_argv_buf[ri++] = _cwd_flag;

    for (int ei = 0; ei < g_env_count && ri < REAL_ARGV_MAX - 1; ei++) {
        char *buf = _env_flags[ei];
        buf[0]='-'; buf[1]='-'; buf[2]='e';
        buf[3]='n'; buf[4]='v'; buf[5]=':';
        size_t ni = 0;
        const char *n = g_env[ei].name;
        while (*n && ni + 7 < ENV_NAME_MAX + ENV_VAL_MAX + 7)
            buf[6 + ni++] = *n++;
        buf[6 + ni++] = '=';
        size_t vi = 0;
        const char *v = g_env[ei].value;
        while (*v && ni + vi + 7 < (size_t)(ENV_NAME_MAX + ENV_VAL_MAX + 7))
            buf[6 + ni + vi++] = *v++;
        buf[6 + ni + vi] = '\0';
        real_argv_buf[ri++] = buf;
    }

    real_argv_buf[ri] = (void *)0;

    pid_t child = fork();
    if (child < 0) { ws(C_RED "fork failed" C_RESET "\n"); return 1; }
    if (child == 0) {
        execve(binpath, (const char **)real_argv_buf, (void *)0);
        ws(C_RED "exec failed: " C_RESET); ws(binpath); wn(); exit(127);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return (status >> 8) & 0xFF;
}

typedef enum { CH_NONE = 0, CH_SEQ, CH_AND, CH_OR } chain_t;

static void run_command(char *line) {
    char work[LINE_MAX];
    scpy(work, line, LINE_MAX);
    char *segs[64]; chain_t ops[64]; int ns = 1;
    segs[0] = work; ops[0] = CH_NONE;
    char *p = work;
    while (*p) {
        if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; continue; }
        if (*p == '\'') { p++; while (*p && *p != '\'') p++; if (*p) p++; continue; }
        if (*p == '&' && *(p+1) == '&') {
            *p='\0'; p+=2; while(*p==' ')p++;
            ops[ns]=CH_AND; segs[ns]=p; ns++; continue;
        }
        if (*p == '|' && *(p+1) == '|') {
            *p='\0'; p+=2; while(*p==' ')p++;
            ops[ns]=CH_OR; segs[ns]=p; ns++; continue;
        }
        if (*p == ';') {
            *p='\0'; p++; while(*p==' ')p++;
            ops[ns]=CH_SEQ; segs[ns]=p; ns++; continue;
        }
        p++;
    }
    int rc = 0;
    for (int i = 0; i < ns; i++) {
        char *s = segs[i];
        while (*s == ' ' || *s == '\t') s++;
        size_t sl = slen(s);
        while (sl > 0 && (s[sl-1] == ' ' || s[sl-1] == '\t')) s[--sl] = '\0';
        if (!s[0]) continue;
        if (i > 0) {
            if (ops[i] == CH_AND && rc != 0) continue;
            if (ops[i] == CH_OR  && rc == 0) continue;
        }
        rc = run_single(s);
    }
    g_last_rc = rc;
}

static void print_motd(void) {
    int fd = open("/etc/motd", O_RDONLY, 0);
    if (fd < 0) {
        wn(); ws("  Cervus OS v0.0.1\n  Type 'help' for commands.\n"); wn(); return;
    }
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) { buf[n] = '\0'; write(1, buf, n); }
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

    env_set("PATH", "/bin:/apps");
    env_set("HOME", "/");
    env_set("SHELL", "/apps/shell");

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
        int len = (int)slen(line);
        while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t'))
            line[--len] = '\0';
        if (len > 0) {
            hist_push(line);
            run_command(line);
        }
    }
    __builtin_unreachable();
}
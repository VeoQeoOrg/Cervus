#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <cervus_util.h>

#ifndef VFS_MAX_PATH
#define VFS_MAX_PATH 1024
#endif

#define TIOCGWINSZ  0x5413
#define TIOCGCURSOR 0x5480

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } cervus_winsize_t;
typedef struct { uint32_t row, col; } cervus_cursor_pos_t;

static inline int sh_ioctl(int fd, unsigned long req, void *arg) {
    return (int)syscall3(SYS_IOCTL, fd, req, arg);
}

static int g_cols = 80;
static int g_rows = 25;

static void term_update_size(void) {
    cervus_winsize_t ws;
    if (sh_ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 8 && ws.ws_row >= 2) {
        g_cols = (int)ws.ws_col;
        g_rows = (int)ws.ws_row;
    }
}

static int term_get_cursor_row(void) {
    cervus_cursor_pos_t cp;
    if (sh_ioctl(1, TIOCGCURSOR, &cp) == 0) return (int)cp.row;
    return 0;
}

static void vt_goto(int row, int col) {
    char b[24];
    snprintf(b, sizeof(b), "\x1b[%d;%dH", row + 1, col + 1);
    fputs(b, stdout);
}

static void vt_eol(void) { fputs("\x1b[K", stdout); }

#define HIST_MAX 1024
#define LINE_MAX 1024

static char history[HIST_MAX][LINE_MAX];
static int  hist_count = 0, hist_head = 0;

static const char *g_hist_file = NULL;

static void hist_load(const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return;
    char line[LINE_MAX];
    int li = 0;
    char ch;
    while (read(fd, &ch, 1) > 0) {
        if (ch == '\n' || li >= LINE_MAX - 1) {
            line[li] = '\0';
            if (li > 0) {
                int idx = (hist_head + hist_count) % HIST_MAX;
                strncpy(history[idx], line, LINE_MAX - 1);
                history[idx][LINE_MAX - 1] = '\0';
                if (hist_count < HIST_MAX) hist_count++;
                else hist_head = (hist_head + 1) % HIST_MAX;
            }
            li = 0;
        } else {
            line[li++] = ch;
        }
    }
    close(fd);
}

static void hist_save_entry(const char *path, const char *l) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    int n = 0;
    while (l[n]) n++;
    write(fd, l, n);
    write(fd, "\n", 1);
    close(fd);
}

static void hist_push(const char *l) {
    if (!l[0]) return;
    if (hist_count > 0) {
        int last = (hist_head + hist_count - 1) % HIST_MAX;
        if (strcmp(history[last], l) == 0) return;
    }
    int idx = (hist_head + hist_count) % HIST_MAX;
    strncpy(history[idx], l, LINE_MAX - 1);
    history[idx][LINE_MAX - 1] = '\0';
    if (hist_count < HIST_MAX) hist_count++;
    else hist_head = (hist_head + 1) % HIST_MAX;
    if (g_hist_file) hist_save_entry(g_hist_file, l);
}

static const char *hist_get(int n) {
    if (n < 1 || n > hist_count) return NULL;
    return history[(hist_head + hist_count - n) % HIST_MAX];
}

#define ENV_MAX_VARS 128
#define ENV_NAME_MAX  64
#define ENV_VAL_MAX  512

typedef struct { char name[ENV_NAME_MAX]; char value[ENV_VAL_MAX]; } env_var_t;

static env_var_t g_env[ENV_MAX_VARS];
static int       g_env_count = 0;

static int env_find(const char *name) {
    for (int i = 0; i < g_env_count; i++)
        if (strcmp(g_env[i].name, name) == 0) return i;
    return -1;
}
static const char *env_get(const char *name) {
    int i = env_find(name);
    return i >= 0 ? g_env[i].value : "";
}
static void env_set(const char *name, const char *value) {
    int i = env_find(name);
    if (i >= 0) { strncpy(g_env[i].value, value, ENV_VAL_MAX - 1); return; }
    if (g_env_count >= ENV_MAX_VARS) return;
    strncpy(g_env[g_env_count].name,  name,  ENV_NAME_MAX - 1);
    strncpy(g_env[g_env_count].value, value, ENV_VAL_MAX  - 1);
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
            char tmp[12];
            int len = snprintf(tmp, sizeof(tmp), "%d", g_last_rc);
            for (int x = 0; x < len && di + 1 < dsz; x++) dst[di++] = tmp[x];
            p++; continue;
        }
        int braced = (*p == '{');
        if (braced) p++;
        char name[ENV_NAME_MAX]; int ni = 0;
        while (*p && ni + 1 < (int)ENV_NAME_MAX) {
            char c = *p;
            if (braced) { if (c == '}') { p++; break; } }
            else if (!isalnum((unsigned char)c) && c != '_') break;
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
static int  prompt_len = 0;

static const char *display_path(void) {
    static char dpbuf[VFS_MAX_PATH];
    const char *home = env_get("HOME");
    size_t hlen = home ? strlen(home) : 0;
    if (hlen > 1 && strncmp(cwd, home, hlen) == 0 &&
        (cwd[hlen] == '/' || cwd[hlen] == '\0')) {
        dpbuf[0] = '~';
        strncpy(dpbuf + 1, cwd + hlen, sizeof(dpbuf) - 2);
        dpbuf[sizeof(dpbuf) - 1] = '\0';
        if (dpbuf[1] == '\0') { dpbuf[0] = '~'; dpbuf[1] = '\0'; }
        return dpbuf;
    }
    return cwd;
}

static void print_prompt(void) {
    const char *dp = display_path();
    fputs(C_GREEN "cervus" C_RESET ":" C_BLUE, stdout);
    fputs(dp, stdout);
    fputs(C_RESET "$ ", stdout);
    prompt_len = 9 + (int)strlen(dp);
}

static int g_start_row = 0;

static void sync_start_row(int cur_logical_pos) {
    int real_row = term_get_cursor_row();
    int row_offset = (prompt_len + cur_logical_pos) / g_cols;
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
    if (new_len > from) write(1, buf + from, new_len - from);
    sync_start_row(new_len);
    if (old_len > new_len) {
        int old_last = last_row_of(old_len);
        int new_last = last_row_of(new_len);
        cursor_to(new_len); vt_eol();
        for (int r = new_last + 1; r <= old_last; r++) {
            if (r >= g_rows) break;
            vt_goto(r, 0); vt_eol();
        }
    }
    cursor_to(pos);
}

static void replace_line(char *buf, int *len, int *pos, const char *newtext, int newlen) {
    int old_len = *len;
    for (int i = 0; i < newlen; i++) buf[i] = newtext[i];
    buf[newlen] = '\0';
    *len = newlen;
    *pos = newlen;
    redraw(buf, 0, newlen, old_len, newlen);
}

static void insert_str(char *buf, int *len, int *pos, int maxlen, const char *s, int slen) {
    if (*len + slen >= maxlen) return;
    int old_len = *len;
    for (int i = *len; i >= *pos; i--) buf[i + slen] = buf[i];
    for (int i = 0; i < slen; i++) buf[*pos + i] = s[i];
    *len += slen;
    buf[*len] = '\0';
    cursor_to(*pos);
    write(1, buf + *pos, *len - *pos);
    sync_start_row(*len);
    *pos += slen;
    cursor_to(*pos);
}

static int find_word_start(const char *buf, int pos) {
    int p = pos;
    while (p > 0 && buf[p - 1] != ' ') p--;
    return p;
}

static void list_dir_matches(const char *dir, const char *prefix, int plen,
                             char matches[][256], int *nmatch, int max) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while (*nmatch < max && (de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, prefix, plen) == 0) {
            strncpy(matches[*nmatch], de->d_name, 255);
            matches[*nmatch][255] = '\0';
            (*nmatch)++;
        }
    }
    closedir(d);
}

static void do_tab_complete(char *buf, int *len, int *pos, int maxlen) {
    int ws_start = find_word_start(buf, *pos);
    int wlen = *pos - ws_start;
    if (wlen <= 0) {
        insert_str(buf, len, pos, maxlen, "    ", 4);
        return;
    }
    char word[256];
    if (wlen > 255) wlen = 255;
    memcpy(word, buf + ws_start, wlen);
    word[wlen] = '\0';

    int is_first_word = 1;
    for (int i = 0; i < ws_start; i++) {
        if (buf[i] != ' ') { is_first_word = 0; break; }
    }

    char matches[32][256];
    int nmatch = 0;

    if (is_first_word) {
        const char *pathvar = env_get("PATH");
        char ptmp[ENV_VAL_MAX];
        strncpy(ptmp, pathvar, sizeof(ptmp) - 1);
        char *p = ptmp;
        while (*p && nmatch < 32) {
            char *seg = p;
            while (*p && *p != ':') p++;
            if (*p == ':') *p++ = '\0';
            if (seg[0]) list_dir_matches(seg, word, wlen, matches, &nmatch, 32);
        }
        const char *builtins[] = {"help","exit","cd","export","unset",NULL};
        for (int i = 0; builtins[i] && nmatch < 32; i++) {
            if (strncmp(builtins[i], word, wlen) == 0) {
                strncpy(matches[nmatch], builtins[i], 255);
                nmatch++;
            }
        }
    } else {
        char dirp[VFS_MAX_PATH];
        const char *prefix = word;
        char *last_slash = NULL;
        for (int i = 0; word[i]; i++) if (word[i] == '/') last_slash = &word[i];
        if (last_slash) {
            int dlen = (int)(last_slash - word);
            char raw_dir[256];
            memcpy(raw_dir, word, dlen);
            raw_dir[dlen] = '\0';
            if (raw_dir[0] == '\0') strcpy(raw_dir, "/");
            resolve_path(cwd, raw_dir, dirp, sizeof(dirp));
            prefix = last_slash + 1;
        } else {
            strncpy(dirp, cwd, sizeof(dirp) - 1);
        }
        int plen = (int)strlen(prefix);
        list_dir_matches(dirp, prefix, plen, matches, &nmatch, 32);
    }

    if (nmatch == 0) {
        insert_str(buf, len, pos, maxlen, "    ", 4);
        return;
    }
    if (nmatch == 1) {
        const char *m = matches[0];
        int mlen = (int)strlen(m);
        int tail = mlen - wlen;
        if (tail > 0) {
            const char *suffix = m + wlen;
            if (is_first_word) {
                int need = tail;
                if (*len + need >= maxlen) return;
                int old_len = *len;
                for (int i = *len; i >= *pos; i--) buf[i + need] = buf[i];
                for (int i = 0; i < tail; i++) buf[*pos + i] = suffix[i];
                *len += need;
                buf[*len] = '\0';
                cursor_to(*pos);
                write(1, buf + *pos, *len - *pos);
                sync_start_row(*len);
                *pos += need;
                cursor_to(*pos);
            } else {
                insert_str(buf, len, pos, maxlen, suffix, tail);
            }
        }
        return;
    }
    int common = (int)strlen(matches[0]);
    for (int i = 1; i < nmatch; i++) {
        int j = 0;
        while (j < common && matches[0][j] == matches[i][j]) j++;
        common = j;
    }
    int extra = common - wlen;
    if (extra > 0) {
        insert_str(buf, len, pos, maxlen, matches[0] + wlen, extra);
        return;
    }
    putchar(10);
    for (int i = 0; i < nmatch; i++) {
        fputs("  ", stdout); fputs(matches[i], stdout);
    }
    putchar(10);
    print_prompt();
    write(1, buf, *len);
    sync_start_row(*len);
    cursor_to(*pos);
}

static int readline_edit(char *buf, int maxlen) {
    term_update_size();
    {
        int real_row = term_get_cursor_row();
        g_start_row = real_row - prompt_len / g_cols;
        if (g_start_row < 0) g_start_row = 0;
    }
    int len = 0, pos = 0, hidx = 0;
    static char saved[LINE_MAX];
    saved[0] = '\0'; buf[0] = '\0';

    for (;;) {
        char c;
        if (read(0, &c, 1) <= 0) return -1;

        if (c == '\x1b') {
            char s[4];
            if (read(0, &s[0], 1) <= 0) continue;
            if (s[0] != '[') continue;
            if (read(0, &s[1], 1) <= 0) continue;
            if (s[1] == 'A') {
                if (hidx == 0) strncpy(saved, buf, LINE_MAX - 1);
                if (hidx < hist_count) {
                    hidx++;
                    const char *h = hist_get(hidx);
                    if (h) {
                        int hl = (int)strlen(h);
                        if (hl > maxlen - 1) hl = maxlen - 1;
                        replace_line(buf, &len, &pos, h, hl);
                    }
                }
                continue;
            }
            if (s[1] == 'B') {
                if (hidx > 0) {
                    hidx--;
                    const char *h = hidx == 0 ? saved : hist_get(hidx);
                    if (!h) h = "";
                    int hl = (int)strlen(h);
                    if (hl > maxlen - 1) hl = maxlen - 1;
                    replace_line(buf, &len, &pos, h, hl);
                }
                continue;
            }
            if (s[1] == 'C') { if (pos < len) { pos++; cursor_to(pos); } continue; }
            if (s[1] == 'D') { if (pos > 0)  { pos--; cursor_to(pos); } continue; }
            if (s[1] == 'H') { pos = 0; cursor_to(0); continue; }
            if (s[1] == 'F') { pos = len; cursor_to(len); continue; }
            if (s[1] >= '1' && s[1] <= '6') {
                char tilde; read(0, &tilde, 1);
                if (tilde != '~') continue;
                if (s[1] == '3' && pos < len) {
                    for (int i = pos; i < len - 1; i++) buf[i] = buf[i + 1];
                    len--; buf[len] = '\0';
                    redraw(buf, pos, len, len + 1, pos);
                } else if (s[1] == '1') { pos = 0; cursor_to(0); }
                else if (s[1] == '4') { pos = len; cursor_to(len); }
            }
            continue;
        }

        if (c == '\n' || c == '\r') { buf[len] = '\0'; cursor_to(len); putchar(10); return len; }
        if (c == 3)  { fputs("^C", stdout); putchar(10); buf[0] = '\0'; return 0; }
        if (c == 4)  { if (len == 0) return -1; continue; }
        if (c == 1)  { pos = 0; cursor_to(0); continue; }
        if (c == 5)  { pos = len; cursor_to(len); continue; }
        if (c == '\t') {
            do_tab_complete(buf, &len, &pos, maxlen);
            continue;
        }
        if (c == 11) {
            if (pos < len) { int old_len = len; len = pos; buf[len] = '\0'; redraw(buf, pos, len, old_len, pos); }
            continue;
        }
        if (c == 21) {
            if (pos > 0) {
                int old_len = len, del = pos;
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
                int old_len = len, del = pos - p;
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
            cursor_to(pos); write(1, buf + pos, len - pos);
            sync_start_row(len); pos++;
            cursor_to(pos);
        }
    }
}

#define MAX_ARGS 32

static int tokenize(char *line, char *argv[], int maxargs) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
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
                else if (isspace((unsigned char)*p)) { p++; break; }
                else { *out++ = *p++; }
            }
        }
        *out = '\0';
        if (in_dquote || in_squote) { argv[argc] = NULL; return -1; }
    }
    argv[argc] = NULL;
    return argc;
}

static void cmd_help(void) {
    putchar(10);
    fputs("  " C_CYAN "Cervus Shell" C_RESET " - commands\n", stdout);
    fputs("  " C_GRAY "-----------------------------------" C_RESET "\n", stdout);
    fputs("  " C_BOLD "help" C_RESET "          show this message\n", stdout);
    fputs("  " C_BOLD "cd" C_RESET " <dir>      change directory\n", stdout);
    fputs("  " C_BOLD "export" C_RESET " N=V    set variable N to value V\n", stdout);
    fputs("  " C_BOLD "unset" C_RESET " N       delete variable N\n", stdout);
    fputs("  " C_BOLD "env" C_RESET "           list variables\n", stdout);
    fputs("  " C_BOLD "exit" C_RESET "          quit shell\n", stdout);
    fputs("  " C_GRAY "-----------------------------------" C_RESET "\n", stdout);
    fputs("  " C_BOLD "Programs:" C_RESET " ls, cat, echo, pwd, clear, uname\n", stdout);
    fputs("  meminfo, cpuinfo, ps, kill, find, stat, wc, yes, sleep\n", stdout);
    fputs("  mount, umount, mkfs, lsblk, mv, rm, mkdir, touch\n", stdout);
    fputs("  " C_RED "shutdown" C_RESET ", " C_CYAN "reboot" C_RESET "\n", stdout);
    fputs("  " C_GRAY "-----------------------------------" C_RESET "\n", stdout);
    fputs("  " C_BOLD "Operators:" C_RESET "  cmd1 " C_YELLOW ";" C_RESET
          " cmd2   " C_YELLOW "&&" C_RESET "   " C_YELLOW "||" C_RESET "\n", stdout);
    fputs("  " C_BOLD "Tab" C_RESET "       auto-complete / 4 spaces\n", stdout);
    fputs("  " C_BOLD "Ctrl+C" C_RESET "    interrupt\n", stdout);
    fputs("  " C_BOLD "Ctrl+A/E" C_RESET "  beginning/end of line\n", stdout);
    fputs("  " C_BOLD "Ctrl+K/U" C_RESET "  delete to end/beginning\n", stdout);
    fputs("  " C_BOLD "Ctrl+W" C_RESET "    delete word\n", stdout);
    fputs("  " C_BOLD "Arrows" C_RESET "    cursor / history\n", stdout);
    fputs("  " C_GRAY "-----------------------------------" C_RESET "\n", stdout);
    putchar(10);
}

static int cmd_cd(const char *path) {
    if (!path || !path[0] || strcmp(path, "~") == 0) {
        const char *home = env_get("HOME");
        path = (home && home[0]) ? home : "/";
    }
    char np[VFS_MAX_PATH];
    resolve_path(cwd, path, np, sizeof(np));
    struct stat st;
    if (stat(np, &st) < 0) { fputs(C_RED "cd: not found: " C_RESET, stdout); fputs(path, stdout); putchar(10); return 1; }
    if (st.st_type != 1)   { fputs(C_RED "cd: not a dir: " C_RESET, stdout); fputs(path, stdout); putchar(10); return 1; }
    strncpy(cwd, np, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
    return 0;
}

static int valid_varname(const char *s) {
    if (!s || !*s) return 0;
    if (!isalpha((unsigned char)*s) && *s != '_') return 0;
    for (s++; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_') return 0;
    return 1;
}

static int cmd_export(int argc, char *argv[]) {
    if (argc < 2) { fputs(C_RED "export: usage: export NAME=VALUE\n" C_RESET, stdout); return 1; }
    if (argc > 2) { fputs(C_RED "export: invalid syntax\n" C_RESET, stdout); return 1; }
    char *arg = argv[1];
    char *eq_pos = strchr(arg, '=');
    if (!eq_pos) {
        if (!valid_varname(arg)) { fputs(C_RED "export: not a valid identifier\n" C_RESET, stdout); return 1; }
        env_set(arg, ""); return 0;
    }
    *eq_pos = '\0';
    const char *name = arg, *val = eq_pos + 1;
    if (!valid_varname(name)) { *eq_pos = '='; fputs(C_RED "export: not a valid identifier\n" C_RESET, stdout); return 1; }
    env_set(name, val);
    *eq_pos = '=';
    return 0;
}

static int cmd_unset(int argc, char *argv[]) {
    if (argc < 2) { fputs(C_RED "unset: usage: unset NAME\n" C_RESET, stdout); return 1; }
    for (int i = 1; i < argc; i++) env_unset(argv[i]);
    return 0;
}

static int find_in_path(const char *cmd, char *out, size_t outsz) {
    const char *pathvar = env_get("PATH");
    if (!pathvar || !pathvar[0]) {
        path_join("/bin", cmd, out, outsz);
        struct stat st;
        return stat(out, &st) == 0 && st.st_type != 1;
    }
    char tmp[ENV_VAL_MAX];
    strncpy(tmp, pathvar, sizeof(tmp) - 1);
    char *p = tmp;
    while (*p) {
        char *seg = p;
        while (*p && *p != ':') p++;
        if (*p == ':') *p++ = '\0';
        if (!seg[0]) continue;
        char candidate[VFS_MAX_PATH];
        path_join(seg, cmd, candidate, sizeof(candidate));
        struct stat st;
        if (stat(candidate, &st) == 0 && st.st_type != 1) { strncpy(out, candidate, outsz - 1); return 1; }
    }
    return 0;
}

typedef enum { REDIR_NONE = 0, REDIR_OUT, REDIR_APPEND, REDIR_IN } redir_type_t;

typedef struct {
    redir_type_t type;
    char path[VFS_MAX_PATH];
} redir_t;

static int parse_redirects(char *argv[], int *argc, redir_t redirs[], int max_redirs, int *nredirs) {
    *nredirs = 0;
    int new_argc = 0;
    for (int i = 0; i < *argc; i++) {
        const char *a = argv[i];
        redir_type_t rt = REDIR_NONE;
        const char *target = NULL;

        if (strcmp(a, ">>") == 0) {
            rt = REDIR_APPEND;
            if (i + 1 < *argc) target = argv[++i];
        } else if (strcmp(a, ">") == 0) {
            rt = REDIR_OUT;
            if (i + 1 < *argc) target = argv[++i];
        } else if (strcmp(a, "<") == 0) {
            rt = REDIR_IN;
            if (i + 1 < *argc) target = argv[++i];
        } else if (a[0] == '>' && a[1] == '>' && a[2] != '\0') {
            rt = REDIR_APPEND; target = a + 2;
        } else if (a[0] == '>' && a[1] != '>' && a[1] != '\0') {
            rt = REDIR_OUT; target = a + 1;
        } else if (a[0] == '<' && a[1] != '\0') {
            rt = REDIR_IN; target = a + 1;
        } else {
            argv[new_argc++] = argv[i];
            continue;
        }

        if (!target || !target[0]) {
            fputs(C_RED "syntax error: missing redirection target\n" C_RESET, stdout);
            return -1;
        }
        if (*nredirs < max_redirs) {
            redirs[*nredirs].type = rt;
            char resolved[VFS_MAX_PATH];
            resolve_path(cwd, target, resolved, sizeof(resolved));
            strncpy(redirs[*nredirs].path, resolved, VFS_MAX_PATH - 1);
            redirs[*nredirs].path[VFS_MAX_PATH - 1] = '\0';
            (*nredirs)++;
        }
    }
    argv[new_argc] = NULL;
    *argc = new_argc;
    return 0;
}

static int run_single(char *line) {
    char expanded[LINE_MAX];
    expand_vars(line, expanded, sizeof(expanded));
    char buf[LINE_MAX];
    strncpy(buf, expanded, LINE_MAX - 1);
    char *argv[MAX_ARGS];
    int argc = tokenize(buf, argv, MAX_ARGS);
    if (argc < 0) { fputs(C_RED "syntax error: unclosed quote\n" C_RESET, stdout); return 1; }
    if (!argc) return 0;

    redir_t redirs[8];
    int nredirs = 0;
    if (parse_redirects(argv, &argc, redirs, 8, &nredirs) < 0) return 1;
    if (!argc) return 0;

    const char *cmd = argv[0];

    if (strcmp(cmd, "help")   == 0) { cmd_help(); return 0; }
    if (strcmp(cmd, "exit")   == 0) { fputs("Goodbye!\n", stdout); exit(0); }
    if (strcmp(cmd, "cd")     == 0) return cmd_cd(argc > 1 ? argv[1] : NULL);
    if (strcmp(cmd, "export") == 0) return cmd_export(argc, argv);
    if (strcmp(cmd, "unset")  == 0) return cmd_unset(argc, argv);

    char binpath[VFS_MAX_PATH];
    if (cmd[0] == '/' || cmd[0] == '.') {
        strncpy(binpath, cmd, sizeof(binpath) - 1);
    } else {
        if (!find_in_path(cmd, binpath, sizeof(binpath))) {
            char t_cwd[VFS_MAX_PATH];
            resolve_path(cwd, cmd, t_cwd, sizeof(t_cwd));
            struct stat st;
            if (stat(t_cwd, &st) == 0 && st.st_type != 1) {
                strncpy(binpath, t_cwd, sizeof(binpath) - 1);
            } else {
                fputs(C_RED "not found: " C_RESET, stdout); fputs(cmd, stdout); putchar(10); return 127;
            }
        }
    }

#define REAL_ARGV_MAX (MAX_ARGS + ENV_MAX_VARS + 4)
    char *real_argv_buf[REAL_ARGV_MAX];
    static char _cwd_flag[VFS_MAX_PATH + 8];
    static char _env_flags[ENV_MAX_VARS][ENV_NAME_MAX + ENV_VAL_MAX + 8];

    int ri = 0;
    real_argv_buf[ri++] = binpath;
    for (int i = 1; i < argc; i++) real_argv_buf[ri++] = argv[i];
    snprintf(_cwd_flag, sizeof(_cwd_flag), "--cwd=%s", cwd);
    real_argv_buf[ri++] = _cwd_flag;
    for (int ei = 0; ei < g_env_count && ri < REAL_ARGV_MAX - 1; ei++) {
        snprintf(_env_flags[ei], sizeof(_env_flags[ei]), "--env:%s=%s",
                 g_env[ei].name, g_env[ei].value);
        real_argv_buf[ri++] = _env_flags[ei];
    }
    real_argv_buf[ri] = NULL;

    pid_t child = fork();
    if (child < 0) { fputs(C_RED "fork failed" C_RESET "\n", stdout); return 1; }
    if (child == 0) {
        for (int i = 0; i < nredirs; i++) {
            int fd = -1;
            int target_fd = -1;
            if (redirs[i].type == REDIR_OUT) {
                fd = open(redirs[i].path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                target_fd = 1;
            } else if (redirs[i].type == REDIR_APPEND) {
                fd = open(redirs[i].path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                target_fd = 1;
            } else if (redirs[i].type == REDIR_IN) {
                fd = open(redirs[i].path, O_RDONLY, 0);
                target_fd = 0;
            }
            if (fd < 0) {
                fputs(C_RED "redirect: cannot open: " C_RESET, stdout);
                fputs(redirs[i].path, stdout);
                putchar('\n');
                exit(1);
            }
            dup2(fd, target_fd);
            close(fd);
        }
        execve(binpath, (char *const *)real_argv_buf, NULL);
        fputs(C_RED "exec failed: " C_RESET, stdout); fputs(binpath, stdout); putchar(10); exit(127);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return (status >> 8) & 0xFF;
}

typedef enum { CH_NONE = 0, CH_SEQ, CH_AND, CH_OR } chain_t;

static void run_command(char *line) {
    char work[LINE_MAX];
    strncpy(work, line, LINE_MAX - 1);
    char *segs[64]; chain_t ops[64]; int ns = 1;
    segs[0] = work; ops[0] = CH_NONE;
    char *p = work;
    while (*p) {
        if (*p == '"')  { p++; while (*p && *p != '"')  p++; if (*p) p++; continue; }
        if (*p == '\'') { p++; while (*p && *p != '\'') p++; if (*p) p++; continue; }
        if (*p == '&' && *(p+1) == '&') { *p='\0'; p+=2; while(isspace((unsigned char)*p))p++; ops[ns]=CH_AND; segs[ns]=p; ns++; continue; }
        if (*p == '|' && *(p+1) == '|') { *p='\0'; p+=2; while(isspace((unsigned char)*p))p++; ops[ns]=CH_OR;  segs[ns]=p; ns++; continue; }
        if (*p == ';') { *p='\0'; p++;   while(isspace((unsigned char)*p))p++; ops[ns]=CH_SEQ; segs[ns]=p; ns++; continue; }
        p++;
    }
    int rc = 0;
    for (int i = 0; i < ns; i++) {
        char *s = segs[i];
        while (isspace((unsigned char)*s)) s++;
        size_t sl = strlen(s);
        while (sl > 0 && isspace((unsigned char)s[sl - 1])) s[--sl] = '\0';
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
    int fd = open("/mnt/etc/motd", O_RDONLY, 0);
    if (fd < 0) fd = open("/etc/motd", O_RDONLY, 0);
    if (fd < 0) { putchar(10); fputs("  Cervus OS v0.0.2\n  Type 'help' for commands.\n", stdout); putchar(10); return; }
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) { buf[n] = '\0'; write(1, buf, n); }
}


static int g_installed = 0;

static int sys_disk_mount(const char *dev, const char *path) {
    return (int)syscall2(SYS_DISK_MOUNT, dev, path);
}

static int launch_installer(void) {
    const char *path = "/bin/install-on-disk";
    struct stat st;
    if (stat(path, &st) != 0) {
        fputs(C_RED "  install-on-disk not found on system.\n" C_RESET, stdout);
        return -1;
    }

    const char *argv[4];
    argv[0] = path;
    argv[1] = "--env:MODE=live";
    argv[2] = "--cwd=/";
    argv[3] = NULL;

    pid_t child = fork();
    if (child < 0) {
        fputs(C_RED "  fork failed\n" C_RESET, stdout);
        return -1;
    }
    if (child == 0) {
        execve(path, (char *const *)argv, NULL);
        fputs(C_RED "  exec install-on-disk failed\n" C_RESET, stdout);
        exit(127);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return (status >> 8) & 0xFF;
}

static int ask_install_or_live(void) {
    fputs("\x1b[2J\x1b[H", stdout);
    fputs("\n", stdout);
    fputs(C_CYAN "  Cervus OS" C_RESET " - Live ISO\n", stdout);
    fputs(C_GRAY "  -----------------------------------" C_RESET "\n\n", stdout);
    fputs("  A disk has been detected on this machine.\n", stdout);
    fputs("  What would you like to do?\n\n", stdout);
    fputs("    [" C_BOLD "1" C_RESET "] Install Cervus to disk\n", stdout);
    fputs("    [" C_BOLD "2" C_RESET "] Continue in Live mode\n\n", stdout);
    fputs("  Choice [1-2]: ", stdout);

    char c = 0;
    while (1) {
        if (read(0, &c, 1) <= 0) continue;
        if (c == '1' || c == '2') { putchar(c); putchar(10); break; }
    }
    return (c == '1') ? 1 : 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    struct stat dev_st;
    int has_disk = (stat("/dev/hda", &dev_st) == 0);
    int has_hda2 = (stat("/dev/hda2", &dev_st) == 0);
    int has_hda_legacy = 0;

    int disk_mounted = 0;

    if (has_hda2) {
        int mr = sys_disk_mount("hda2", "/mnt");
        if (mr == 0) {
            disk_mounted = 1;
            g_installed = 1;
        }
    } else if (has_disk) {
        int mr = sys_disk_mount("hda", "/mnt");
        if (mr == 0) {
            disk_mounted = 1;
            g_installed = 1;
            has_hda_legacy = 1;
        }
    }
    (void)has_hda_legacy;

    if (!disk_mounted && has_disk) {
        if (ask_install_or_live() == 1) {
            launch_installer();
            struct stat retry_st;
            if (stat("/dev/hda2", &retry_st) == 0) {
                if (sys_disk_mount("hda2", "/mnt") == 0) {
                    disk_mounted = 1;
                    g_installed = 1;
                }
            }
        }
    }

    if (disk_mounted) {
        strncpy(cwd, "/mnt/home", sizeof(cwd));
        env_set("HOME", "/mnt/home");
        env_set("PATH", "/mnt/bin:/mnt/apps:/mnt/usr/bin");
        env_set("SHELL", "/mnt/bin/shell");
    } else {
        strncpy(cwd, "/", sizeof(cwd));
        env_set("HOME", "/");
        env_set("PATH", "/bin:/apps:/usr/bin");
        env_set("SHELL", "/bin/shell");
    }

    if (!disk_mounted && has_disk)    env_set("MODE", "live");
    else if (!has_disk)               env_set("MODE", "live");
    else                              env_set("MODE", "installed");

    print_motd();

    {
        static char hist_path[VFS_MAX_PATH];
        const char *h = env_get("HOME");
        if (h && h[0]) {
            path_join(h, ".history", hist_path, sizeof(hist_path));
            g_hist_file = hist_path;
            hist_load(hist_path);
        }
    }

    if (!has_disk) {
        fputs(C_YELLOW " [Live Mode]" C_RESET " No disk detected. All changes are in RAM.\n\n", stdout);
    } else if (!disk_mounted) {
        fputs(C_YELLOW " [Live Mode]" C_RESET " Disk not mounted.\n\n", stdout);
    }

    char line[LINE_MAX];
    for (;;) {
        print_prompt();
        int n = readline_edit(line, LINE_MAX);
        if (n < 0) {
            fputs("\nSession ended. Restarting shell...\n", stdout);
            const char *h = env_get("HOME");
            strncpy(cwd, (h && h[0]) ? h : "/", sizeof(cwd));
            print_motd();
            continue;
        }
        int len = (int)strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) line[--len] = '\0';
        if (len > 0) { hist_push(line); run_command(line); }
    }
}
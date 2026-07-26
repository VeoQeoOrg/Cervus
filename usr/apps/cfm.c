#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <tui.h>

#define PMAX        1024
#define MAX_ENTRIES 4096

typedef struct {
    char name[256];
    int  is_dir;
    long size;
} entry_t;

static char     g_cwd[PMAX];
static entry_t  g_ent[MAX_ENTRIES];
static int      g_n, g_sel, g_top;
static int      g_rows = 24, g_cols = 80;

static char     g_clip[PMAX];
static int      g_clip_cut = 0, g_clip_have = 0;
static int      g_show_hidden = 0;
static char     g_status[256];

static void set_status(const char *s) {
    strncpy(g_status, s, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = 0;
}

static void path_join(const char *dir, const char *name, char *out) {
    if (!strcmp(dir, "/"))
        snprintf(out, PMAX, "/%s", name);
    else
        snprintf(out, PMAX, "%s/%s", dir, name);
}

static void parent_dir(const char *dir, char *out) {
    strncpy(out, dir, PMAX - 1);
    out[PMAX - 1] = 0;
    if (!strcmp(out, "/")) return;
    char *slash = strrchr(out, '/');
    if (!slash) { strcpy(out, "/"); return; }
    if (slash == out) out[1] = 0;
    else              *slash = 0;
}

static int ent_cmp(const entry_t *a, const entry_t *b) {
    if (a->is_dir != b->is_dir) return b->is_dir - a->is_dir;
    return strcmp(a->name, b->name);
}

static void load_dir(void) {
    g_n = 0;
    if (strcmp(g_cwd, "/") != 0 && g_n < MAX_ENTRIES) {
        strcpy(g_ent[g_n].name, "..");
        g_ent[g_n].is_dir = 1;
        g_ent[g_n].size = 0;
        g_n++;
    }
    DIR *d = opendir(g_cwd);
    if (d) {
        struct dirent *e;
        int base = g_n;
        while ((e = readdir(d)) != NULL && g_n < MAX_ENTRIES) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (!g_show_hidden && e->d_name[0] == '.') continue;
            entry_t *t = &g_ent[g_n];
            strncpy(t->name, e->d_name, sizeof(t->name) - 1);
            t->name[sizeof(t->name) - 1] = 0;
            char full[PMAX];
            path_join(g_cwd, e->d_name, full);
            struct stat st;
            if (stat(full, &st) == 0) {
                t->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
                t->size   = (long)st.st_size;
            } else {
                t->is_dir = 0;
                t->size   = 0;
            }
            g_n++;
        }
        closedir(d);
        for (int i = base + 1; i < g_n; i++) {
            entry_t tmp = g_ent[i];
            int j = i - 1;
            while (j >= base && ent_cmp(&g_ent[j], &tmp) > 0) {
                g_ent[j + 1] = g_ent[j];
                j--;
            }
            g_ent[j + 1] = tmp;
        }
    }
    if (g_sel >= g_n) g_sel = g_n > 0 ? g_n - 1 : 0;
    g_top = 0;
}

static void fmt_size(long n, char *out, int cap) {
    if (n < 1024)               snprintf(out, cap, "%ldB", n);
    else if (n < 1024 * 1024)   snprintf(out, cap, "%ldK", n / 1024);
    else                        snprintf(out, cap, "%ldM", n / (1024 * 1024));
}

static void draw(void) {
    int listrows = g_rows - 3;
    if (listrows < 1) listrows = 1;
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + listrows) g_top = g_sel - listrows + 1;

    printf("\x1b[?25l");

    tui_move(1, 1);
    printf("\x1b[44m\x1b[97m cfm \x1b[0m\x1b[44m %-*.*s\x1b[0m",
           g_cols - 6, g_cols - 6, g_cwd);

    for (int i = 0; i < listrows; i++) {
        int idx = g_top + i;
        tui_move(2 + i, 1);
        if (idx < g_n) {
            entry_t *e = &g_ent[idx];
            int selrow = (idx == g_sel);
            char sz[24];
            if (e->is_dir) strcpy(sz, "<DIR>");
            else           fmt_size(e->size, sz, sizeof(sz));
            int namew = g_cols - 12;
            if (namew < 4) namew = 4;
            char row[600];
            snprintf(row, sizeof(row), " %c %-*.*s %7s ",
                     e->is_dir ? '/' : ' ', namew, namew, e->name, sz);
            if (selrow)         printf("\x1b[7m%s\x1b[0m", row);
            else if (e->is_dir) printf("\x1b[94m%s\x1b[0m", row);
            else                printf("%s", row);
            printf("\x1b[K");
        } else {
            printf("\x1b[K");
        }
    }

    tui_move(g_rows - 1, 1);
    printf("\x1b[K");
    if (g_clip_have) {
        char base[256]; const char *b = strrchr(g_clip, '/');
        b = b ? b + 1 : g_clip;
        strncpy(base, b, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
        printf("\x1b[90m[%s: %s]\x1b[0m ", g_clip_cut ? "cut" : "copy", base);
    }
    printf("%.*s", g_cols - 20, g_status);

    tui_move(g_rows, 1);
    printf("\x1b[46m\x1b[30m"
           " \x18\x19 nav  Enter view  e run  Bksp up  r rename  d del  "
           "c copy  x cut  v paste  n mkdir  . hidden  q quit \x1b[0m\x1b[K");

    fflush(stdout);
}

static int read_line_prompt(const char *label, char *buf, int cap) {
    int len = 0;
    buf[0] = 0;
    for (;;) {
        tui_move(g_rows, 1);
        printf("\x1b[43m\x1b[30m%s\x1b[0m %s\x1b[K", label, buf);
        fflush(stdout);
        int k = tui_read_key();
        if (k == TK_ENTER)  return len > 0 ? 1 : 0;
        if (k == TK_ESC)    return -1;
        if (k == TK_BACKSP || k == 8) { if (len > 0) buf[--len] = 0; continue; }
        if (k >= 32 && k < 127 && len < cap - 1) { buf[len++] = (char)k; buf[len] = 0; }
    }
}

static int confirm(const char *msg) {
    tui_move(g_rows, 1);
    printf("\x1b[41m\x1b[97m%s (y/n)\x1b[0m\x1b[K", msg);
    fflush(stdout);
    for (;;) {
        int k = tui_read_key();
        if (k == 'y' || k == 'Y') return 1;
        if (k == 'n' || k == 'N' || k == TK_ESC) return 0;
    }
}

static int copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { close(in); return -1; }
    char buf[8192];
    ssize_t r;
    int rc = 0;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        if (write(out, buf, (size_t)r) != r) { rc = -1; break; }
    }
    if (r < 0) rc = -1;
    close(in);
    close(out);
    return rc;
}

static int copy_tree(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return copy_file(src, dst);
    if (mkdir(dst, 0755) != 0) {  }
    DIR *d = opendir(src);
    if (!d) return -1;
    struct dirent *e;
    int rc = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char s2[PMAX], d2[PMAX];
        path_join(src, e->d_name, s2);
        path_join(dst, e->d_name, d2);
        if (copy_tree(s2, d2) != 0) rc = -1;
    }
    closedir(d);
    return rc;
}

static int remove_tree(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char c[PMAX];
            path_join(path, e->d_name, c);
            remove_tree(c);
        }
        closedir(d);
    }
    return rmdir(path);
}

static void view_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { set_status("cannot open file"); return; }
    static char buf[262144];
    ssize_t total = 0, r;
    while (total < (ssize_t)sizeof(buf) - 1 &&
           (r = read(fd, buf + total, sizeof(buf) - 1 - total)) > 0)
        total += r;
    close(fd);
    buf[total] = 0;

    int nlines = 1;
    for (ssize_t i = 0; i < total; i++) if (buf[i] == '\n') nlines++;
    char **lines = malloc(sizeof(char *) * nlines);
    int li = 0;
    lines[li++] = buf;
    for (ssize_t i = 0; i < total; i++) {
        if (buf[i] == '\n') { buf[i] = 0; if (li < nlines) lines[li++] = buf + i + 1; }
    }

    int off = 0, view = g_rows - 2;
    if (view < 1) view = 1;
    for (;;) {
        printf("\x1b[?25l");
        tui_move(1, 1);
        printf("\x1b[44m\x1b[97m view \x1b[0m\x1b[44m %-*.*s\x1b[0m",
               g_cols - 7, g_cols - 7, path);
        for (int i = 0; i < view; i++) {
            tui_move(2 + i, 1);
            int idx = off + i;
            if (idx < li) printf("%-.*s\x1b[K", g_cols, lines[idx]);
            else          printf("\x1b[K");
        }
        tui_move(g_rows, 1);
        printf("\x1b[46m\x1b[30m \x18\x19 scroll  PgUp/PgDn  line %d/%d  q back \x1b[0m\x1b[K",
               off + 1, li);
        fflush(stdout);

        int k = tui_read_key();
        if (k == 'q' || k == TK_ESC || k == TK_BACKSP) break;
        else if (k == TK_UP)   { if (off > 0) off--; }
        else if (k == TK_DOWN) { if (off < li - 1) off++; }
        else if (k == TK_PGUP) { off -= view; if (off < 0) off = 0; }
        else if (k == TK_PGDN) { off += view; if (off > li - 1) off = li - 1; if (off < 0) off = 0; }
        else if (k == TK_HOME) off = 0;
        else if (k == TK_END)  { off = li - view; if (off < 0) off = 0; }
    }
    free(lines);
}

static void do_open(void) {
    if (g_n == 0) return;
    entry_t *e = &g_ent[g_sel];
    if (e->is_dir) {
        char np[PMAX];
        if (!strcmp(e->name, "..")) parent_dir(g_cwd, np);
        else                        path_join(g_cwd, e->name, np);
        strncpy(g_cwd, np, sizeof(g_cwd) - 1);
        g_cwd[sizeof(g_cwd) - 1] = 0;
        g_sel = 0;
        load_dir();
        set_status("");
    } else {
        char full[PMAX];
        path_join(g_cwd, e->name, full);
        view_file(full);
    }
}

static void run_program(void) {
    if (g_n == 0) return;
    entry_t *e = &g_ent[g_sel];
    if (e->is_dir) { do_open(); return; }
    char full[PMAX];
    path_join(g_cwd, e->name, full);

    tui_end();
    printf("\r\n\x1b[36m[cfm] running %s\x1b[0m\r\n\r\n", full);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0) {
        char *argv[2] = { full, NULL };
        execvp(full, argv);
        printf("cfm: cannot exec %s\r\n", full);
        _exit(127);
    }
    int status = 0;
    if (pid > 0) waitpid(pid, &status, 0);

    printf("\r\n\x1b[90m[cfm] '%s' exited (%d) -- press Enter to return\x1b[0m", e->name, status);
    fflush(stdout);
    char tmp[16];
    read(0, tmp, sizeof(tmp));

    tui_begin();
    load_dir();
    set_status("");
}

static void do_rename(void) {
    if (g_n == 0 || !strcmp(g_ent[g_sel].name, "..")) return;
    char nn[256];
    strncpy(nn, g_ent[g_sel].name, sizeof(nn) - 1); nn[sizeof(nn) - 1] = 0;
    int len = strlen(nn);
    char label[64]; snprintf(label, sizeof(label), "Rename to:");

    char buf[256]; strncpy(buf, nn, sizeof(buf)); (void)len;
    if (read_line_prompt(label, buf, sizeof(buf)) == 1) {
        char src[PMAX], dst[PMAX];
        path_join(g_cwd, g_ent[g_sel].name, src);
        path_join(g_cwd, buf, dst);
        if (rename(src, dst) == 0) set_status("renamed");
        else                       set_status("rename failed");
        load_dir();
    } else set_status("");
}

static void do_delete(void) {
    if (g_n == 0 || !strcmp(g_ent[g_sel].name, "..")) return;
    char msg[300];
    snprintf(msg, sizeof(msg), "Delete '%s'?", g_ent[g_sel].name);
    if (confirm(msg)) {
        char full[PMAX];
        path_join(g_cwd, g_ent[g_sel].name, full);
        if (remove_tree(full) == 0) set_status("deleted");
        else                        set_status("delete failed");
        load_dir();
    } else set_status("");
}

static void do_mkdir(void) {
    char buf[256];
    if (read_line_prompt("New dir:", buf, sizeof(buf)) == 1) {
        char full[PMAX];
        path_join(g_cwd, buf, full);
        if (mkdir(full, 0755) == 0) set_status("created");
        else                        set_status("mkdir failed");
        load_dir();
    } else set_status("");
}

static void do_paste(void) {
    if (!g_clip_have) { set_status("clipboard empty"); return; }
    const char *b = strrchr(g_clip, '/');
    b = b ? b + 1 : g_clip;
    char dst[PMAX];
    path_join(g_cwd, b, dst);
    if (!strcmp(g_clip, dst)) { set_status("same location"); return; }
    if (g_clip_cut) {
        if (rename(g_clip, dst) == 0) { set_status("moved"); g_clip_have = 0; }
        else if (copy_tree(g_clip, dst) == 0 && remove_tree(g_clip) == 0) {
            set_status("moved"); g_clip_have = 0;
        } else set_status("move failed");
    } else {
        if (copy_tree(g_clip, dst) == 0) set_status("pasted");
        else                             set_status("paste failed");
    }
    load_dir();
}

int main(int argc, char **argv) {
    if (argc > 1) {
        strncpy(g_cwd, argv[1], sizeof(g_cwd) - 1);
        g_cwd[sizeof(g_cwd) - 1] = 0;
    } else if (!getcwd(g_cwd, sizeof(g_cwd))) {
        strcpy(g_cwd, "/");
    }

    tui_begin();
    tui_size(&g_rows, &g_cols);
    if (g_cols > 590) g_cols = 590;
    load_dir();
    set_status("");

    int running = 1;
    while (running) {
        draw();
        int k = tui_read_key();
        switch (k) {
        case TK_UP:    if (g_sel > 0) g_sel--; break;
        case TK_DOWN:  if (g_sel < g_n - 1) g_sel++; break;
        case TK_PGUP:  g_sel -= (g_rows - 3); if (g_sel < 0) g_sel = 0; break;
        case TK_PGDN:  g_sel += (g_rows - 3); if (g_sel > g_n - 1) g_sel = g_n - 1; break;
        case TK_HOME:  g_sel = 0; break;
        case TK_END:   g_sel = g_n - 1; break;
        case TK_ENTER:
        case TK_RIGHT: do_open(); break;
        case TK_LEFT:
        case TK_BACKSP: {
            char np[PMAX]; parent_dir(g_cwd, np);
            strncpy(g_cwd, np, sizeof(g_cwd) - 1); g_cwd[sizeof(g_cwd) - 1] = 0;
            g_sel = 0; load_dir(); set_status("");
            break;
        }
        case 'e': run_program(); break;
        case 'r': do_rename(); break;
        case 'd': do_delete(); break;
        case 'n': do_mkdir();  break;
        case 'c':
            if (g_n && strcmp(g_ent[g_sel].name, "..")) {
                path_join(g_cwd, g_ent[g_sel].name, g_clip);
                g_clip_cut = 0; g_clip_have = 1; set_status("copied to clipboard");
            }
            break;
        case 'x':
            if (g_n && strcmp(g_ent[g_sel].name, "..")) {
                path_join(g_cwd, g_ent[g_sel].name, g_clip);
                g_clip_cut = 1; g_clip_have = 1; set_status("cut to clipboard");
            }
            break;
        case 'v':
        case 'p': do_paste(); break;
        case 'g': load_dir(); set_status("refreshed"); break;
        case '.':
            g_show_hidden = !g_show_hidden;
            g_sel = 0; g_top = 0;
            load_dir();
            set_status(g_show_hidden ? "hidden files shown" : "hidden files hidden");
            break;
        case 'q': running = 0; break;
        default: break;
        }
    }

    tui_end();
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <tui.h>
#include <image.h>
#include <sys/cervus.h>

#define TIOCSNONBLOCK 0x5481

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
static int      g_preview = 1;
static int      g_confirm_del = 1;
static int      g_sort = 0;
static int      g_preview_pct = 46;
static int      g_gif_anim = 1;
static char     g_status[256];

static image_t  g_thumb;
static char     g_thumb_path[PMAX];

static gif_anim_t g_thumb_gif;
static int        g_thumb_gif_ok;
static int        g_thumb_frame;
static int        g_thumb_px0, g_thumb_py0, g_thumb_rw, g_thumb_rh;

static char     g_pvbuf[16384];
static char    *g_pvlines[1024];
static int      g_pvn;
static char     g_pv_path[PMAX];

static int has_ext_ci(const char *name, const char *ext) {
    size_t nl = strlen(name), el = strlen(ext);
    if (nl < el) return 0;
    const char *p = name + nl - el;
    for (size_t i = 0; i < el; i++) {
        char a = p[i]; if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != ext[i]) return 0;
    }
    return 1;
}

static int is_image(const char *name) {
    return has_ext_ci(name, ".png") || has_ext_ci(name, ".jpg") ||
           has_ext_ci(name, ".jpeg") || has_ext_ci(name, ".bmp") ||
           has_ext_ci(name, ".svg")  || has_ext_ci(name, ".gif");
}

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

static const char *ext_of(const char *n) {
    const char *dot = strrchr(n, '.');
    return dot ? dot + 1 : "";
}

static int ent_cmp(const entry_t *a, const entry_t *b) {
    if (a->is_dir != b->is_dir) return b->is_dir - a->is_dir;
    if (g_sort == 1) {
        if (a->size != b->size) return (a->size < b->size) ? 1 : -1;
    } else if (g_sort == 2) {
        int e = strcmp(ext_of(a->name), ext_of(b->name));
        if (e) return e;
    }
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

static void draw_inline_thumb(const char *path, int split);
static void thumb_drop(void);

static void fmt_size(long n, char *out, int cap) {
    if (n < 1024)               snprintf(out, cap, "%ldB", n);
    else if (n < 1024 * 1024)   snprintf(out, cap, "%ldK", n / 1024);
    else                        snprintf(out, cap, "%ldM", n / (1024 * 1024));
}

static void load_preview_text(const char *path) {
    if (!strcmp(g_pv_path, path)) return;
    snprintf(g_pv_path, sizeof g_pv_path, "%s", path);
    g_pvn = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) { g_pvlines[g_pvn++] = "(cannot read)"; return; }
    ssize_t total = 0, r;
    while (total < (ssize_t)sizeof(g_pvbuf) - 1 &&
           (r = read(fd, g_pvbuf + total, sizeof(g_pvbuf) - 1 - total)) > 0)
        total += r;
    close(fd);
    if (total < 0) total = 0;
    g_pvbuf[total] = 0;
    for (ssize_t i = 0; i < total && i < 512; i++)
        if (g_pvbuf[i] == 0) { g_pvlines[g_pvn++] = "(binary file)"; return; }
    g_pvlines[g_pvn++] = g_pvbuf;
    for (ssize_t i = 0; i < total && g_pvn < 1024; i++) {
        if (g_pvbuf[i] == '\n') { g_pvbuf[i] = 0; g_pvlines[g_pvn++] = g_pvbuf + i + 1; }
        else if (g_pvbuf[i] == '\t' || (unsigned char)g_pvbuf[i] == '\r') g_pvbuf[i] = ' ';
    }
}

static void load_preview_dir(const char *path) {
    if (!strcmp(g_pv_path, path)) return;
    snprintf(g_pv_path, sizeof g_pv_path, "%s", path);
    g_pvn = 0;
    DIR *d = opendir(path);
    if (!d) { g_pvlines[g_pvn++] = "(cannot open)"; return; }
    char *bp = g_pvbuf; size_t rem = sizeof g_pvbuf;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && g_pvn < 1024 && rem > 260) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!g_show_hidden && e->d_name[0] == '.') continue;
        int n = snprintf(bp, rem, "%s", e->d_name);
        if (n < 0 || (size_t)n >= rem) break;
        g_pvlines[g_pvn++] = bp;
        bp += n + 1; rem -= (size_t)n + 1;
    }
    closedir(d);
    if (g_pvn == 0) g_pvlines[g_pvn++] = "(empty)";
}

static void draw(void) {
    int listrows = g_rows - 3;
    if (listrows < 1) listrows = 1;
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + listrows) g_top = g_sel - listrows + 1;

    entry_t *sel = (g_n > 0) ? &g_ent[g_sel] : NULL;
    int split = g_cols, prev_img = 0, prev_txt = 0;
    if (g_preview && sel && g_cols >= 50) {
        split = g_cols * g_preview_pct / 100;
        if (is_image(sel->name) && !sel->is_dir) prev_img = 1;
        else                                     prev_txt = 1;
    }
    int leftw = (split < g_cols) ? split - 1 : g_cols;
    if (leftw < 8) { leftw = g_cols; split = g_cols; prev_img = prev_txt = 0; }

    if (prev_txt) {
        char full[PMAX]; path_join(g_cwd, sel->name, full);
        if (sel->is_dir) load_preview_dir(full);
        else             load_preview_text(full);
    }

    printf("\x1b[?25l");
    tui_move(1, 1);
    printf("\x1b[44m\x1b[97m cfm \x1b[0m\x1b[44m %-*.*s\x1b[0m", g_cols - 6, g_cols - 6, g_cwd);

    for (int i = 0; i < listrows; i++) {
        int idx = g_top + i;
        tui_move(2 + i, 1);
        if (idx < g_n) {
            entry_t *e = &g_ent[idx];
            int selrow = (idx == g_sel);
            char sz[24];
            if (e->is_dir) strcpy(sz, "<DIR>");
            else           fmt_size(e->size, sz, sizeof(sz));
            int namew = leftw - 11; if (namew < 4) namew = 4;
            char row[600];
            snprintf(row, sizeof(row), " %c %-*.*s %6s",
                     e->is_dir ? '/' : ' ', namew, namew, e->name, sz);
            if (selrow)         printf("\x1b[7m%-*.*s\x1b[0m", leftw, leftw, row);
            else if (e->is_dir) printf("\x1b[94m%-*.*s\x1b[0m", leftw, leftw, row);
            else                printf("%-*.*s", leftw, leftw, row);
        } else {
            printf("%*s", leftw, "");
        }
        if (split < g_cols) {
            printf("\x1b[90m|\x1b[0m");
            if (prev_txt && i < g_pvn) printf(" %-.*s", g_cols - split - 2, g_pvlines[i]);
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
           " \x18\x19 nav  Enter open  e run  r rename  d del  c/x/v  "
           "n mkdir  s settings  . hidden  q quit \x1b[0m\x1b[K");

    if (prev_img) {
        char full[PMAX]; path_join(g_cwd, sel->name, full);
        if (strcmp(g_thumb_path, full) != 0) {
            tui_move(2, split + 2);
            printf("\x1b[93m Loading image... \x1b[0m");
        }
        fflush(stdout);
        draw_inline_thumb(full, split);
    } else {
        fflush(stdout);
        if (g_thumb_path[0]) thumb_drop();
    }
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
            printf("\x1b[K");
            int idx = off + i;
            if (idx < li) printf("%-.*s", g_cols, lines[idx]);
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

static void blit_fit(const image_t *im, int px0, int py0, int rw, int rh) {
    if (rw <= 0 || rh <= 0) return;
    int dw = im->w, dh = im->h;
    double k = (double)rw / dw;
    if ((double)rh / dh < k) k = (double)rh / dh;
    dw = (int)(im->w * k); dh = (int)(im->h * k);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    image_t sc = (dw != im->w || dh != im->h) ? image_scale(im, dw, dh) : *im;
    if (!sc.px) return;
    uint32_t *frame = calloc((size_t)rw * rh, 4);
    if (frame) {
        int ox = (rw - dw) / 2, oy = (rh - dh) / 2;
        if (ox < 0) ox = 0;
        if (oy < 0) oy = 0;
        for (int y = 0; y < dh && oy + y < rh; y++) {
            const uint32_t *s = sc.px + (size_t)y * dw;
            uint32_t *d = frame + (size_t)(oy + y) * rw + ox;
            for (int x = 0; x < dw && ox + x < rw; x++) {
                uint32_t p = s[x];
                unsigned a = (p >> 24) & 0xFF, r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
                d[x] = ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
            }
        }
        cervus_fb_blit(frame, px0, py0, rw, rh);
        free(frame);
    }
    if (sc.px != im->px) image_free(&sc);
}

static uint8_t *read_whole(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 6) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz || memcmp(buf, "GIF8", 4) != 0) { free(buf); return NULL; }
    *len = got;
    return buf;
}

static int play_gif_fullscreen(const char *path, const cervus_fb_info_t *fbi) {
    size_t got = 0;
    uint8_t *buf = read_whole(path, &got);
    if (!buf) return -1;

    gif_anim_t a;
    if (gif_decode(buf, got, &a) != 0) { free(buf); return -1; }
    free(buf);
    if (a.nframes < 2) { gif_free(&a); return -1; }

    int v = 1;
    ioctl(0, TIOCSNONBLOCK, &v);
    cervus_fb_acquire();

    int quit = 0;
    while (!quit) {
        for (int i = 0; i < a.nframes && !quit; i++) {
            image_t fr = { a.w, a.h, a.frames[i] };
            blit_fit(&fr, 0, 0, (int)fbi->width, (int)fbi->height);
            int ms = a.delays_ms[i] < 20 ? 20 : a.delays_ms[i];
            for (int slept = 0; slept < ms && !quit; slept += 20) {
                usleep(20000);
                char c;
                if (read(0, &c, 1) == 1) quit = 1;
            }
        }
    }

    cervus_fb_release();
    v = 0;
    ioctl(0, TIOCSNONBLOCK, &v);
    gif_free(&a);
    return 0;
}

static void view_image_fullscreen(const char *path) {
    cervus_fb_info_t fbi;
    if (cervus_fb_info(&fbi) != 0) { view_file(path); return; }

    if (g_gif_anim && has_ext_ci(path, ".gif") &&
        play_gif_fullscreen(path, &fbi) == 0) return;

    image_t im;
    if (image_load(path, &im) != 0) { set_status("cannot decode image"); return; }
    cervus_fb_acquire();
    blit_fit(&im, 0, 0, (int)fbi.width, (int)fbi.height);
    tui_read_key();
    cervus_fb_release();
    image_free(&im);
}

static void thumb_drop(void) {
    if (g_thumb_gif_ok) { gif_free(&g_thumb_gif); g_thumb_gif_ok = 0; }
    image_free(&g_thumb);
    g_thumb.px = NULL;
    g_thumb_path[0] = 0;
    g_thumb_frame = 0;
    g_thumb_rw = 0;
}

static int thumb_load_gif(const char *path) {
    size_t len = 0;
    uint8_t *data = read_whole(path, &len);
    if (!data) return -1;
    int rc = gif_decode(data, len, &g_thumb_gif);
    free(data);
    if (rc != 0) return -1;
    if (g_thumb_gif.nframes < 2) { gif_free(&g_thumb_gif); return -1; }
    g_thumb_gif_ok = 1;
    g_thumb_frame = 0;
    return 0;
}

static void draw_inline_thumb(const char *path, int split) {
    cervus_fb_info_t fbi;
    if (cervus_fb_info(&fbi) != 0) return;
    int cell_w = (int)fbi.width / g_cols;
    int cell_h = (int)fbi.height / g_rows;
    if (cell_w < 2 || cell_h < 2) return;

    int px0 = (split + 1) * cell_w;
    int py0 = 1 * cell_h;
    int rw = (int)fbi.width - px0 - cell_w;
    int rh = (g_rows - 3) * cell_h;
    if (rw < 16 || rh < 16) return;

    if (strcmp(g_thumb_path, path) != 0) {
        thumb_drop();
        if (g_gif_anim && has_ext_ci(path, ".gif") && thumb_load_gif(path) == 0) {
            snprintf(g_thumb_path, sizeof g_thumb_path, "%s", path);
        } else {
            image_t im;
            if (image_load(path, &im) != 0) { g_thumb_path[0] = 0; return; }
            g_thumb = im;
            snprintf(g_thumb_path, sizeof g_thumb_path, "%s", path);
        }
    }

    g_thumb_px0 = px0; g_thumb_py0 = py0;
    g_thumb_rw = rw;   g_thumb_rh = rh;

    if (g_thumb_gif_ok) {
        image_t fr = { g_thumb_gif.w, g_thumb_gif.h, g_thumb_gif.frames[g_thumb_frame] };
        blit_fit(&fr, px0, py0, rw, rh);
        return;
    }
    if (!g_thumb.px) return;
    blit_fit(&g_thumb, px0, py0, rw, rh);
}

static int thumb_next_frame(void) {
    if (!g_thumb_gif_ok || g_thumb_rw <= 0) return 0;
    int delay = g_thumb_gif.delays_ms[g_thumb_frame];
    g_thumb_frame = (g_thumb_frame + 1) % g_thumb_gif.nframes;
    image_t fr = { g_thumb_gif.w, g_thumb_gif.h, g_thumb_gif.frames[g_thumb_frame] };
    blit_fit(&fr, g_thumb_px0, g_thumb_py0, g_thumb_rw, g_thumb_rh);
    return delay < 20 ? 20 : delay;
}

static int read_key_animated(void) {
    if (!g_thumb_gif_ok || g_thumb_rw <= 0) return tui_read_key();
    for (;;) {
        struct pollfd pfd = { 0, POLLIN, 0 };
        int delay = g_thumb_gif.delays_ms[g_thumb_frame];
        if (delay < 20) delay = 20;
        int r = poll(&pfd, 1, delay);
        if (r > 0) return tui_read_key();
        if (r < 0) return tui_read_key();
        thumb_next_frame();
    }
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
        if (is_image(e->name))
            view_image_fullscreen(full);
        else
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
    if (!g_confirm_del || confirm(msg)) {
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

static void cfm_config_path(char *out, size_t cap) {
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/root";
    snprintf(out, cap, "%s/.cfmrc", home);
}

static void cfm_config_load(void) {
    char path[PMAX];
    cfm_config_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        int v = atoi(eq + 1);
        if      (!strcmp(line, "preview"))     g_preview     = v ? 1 : 0;
        else if (!strcmp(line, "confirmdel"))  g_confirm_del = v ? 1 : 0;
        else if (!strcmp(line, "hidden"))      g_show_hidden = v ? 1 : 0;
        else if (!strcmp(line, "sort"))        g_sort        = (v >= 0 && v <= 2) ? v : 0;
        else if (!strcmp(line, "previewpct"))  g_preview_pct = (v >= 20 && v <= 80) ? v : 46;
        else if (!strcmp(line, "gifanim"))     g_gif_anim    = v ? 1 : 0;
    }
    fclose(f);
}

static int cfm_config_save(void) {
    char path[PMAX];
    cfm_config_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "preview=%d\n",    g_preview);
    fprintf(f, "confirmdel=%d\n", g_confirm_del);
    fprintf(f, "hidden=%d\n",     g_show_hidden);
    fprintf(f, "sort=%d\n",       g_sort);
    fprintf(f, "previewpct=%d\n", g_preview_pct);
    fprintf(f, "gifanim=%d\n",    g_gif_anim);
    fclose(f);
    return 0;
}

static void do_settings(void) {
    int sel = 0;
    const int N = 7;
    const char *names[7] = { "Preview pane", "Confirm delete", "Show hidden",
                             "Sort by", "Preview width", "Animate GIFs",
                             "Save settings" };
    static const char *sort_names[3] = { "name", "size", "type" };
    for (;;) {
        printf("\x1b[?25l\x1b[2J\x1b[H");
        printf("\x1b[44m\x1b[97m cfm settings \x1b[0m  \x18\x19 move   < > change   Esc close\r\n\r\n");
        char vals[7][32];
        snprintf(vals[0], sizeof vals[0], "%s", g_preview ? "ON" : "OFF");
        snprintf(vals[1], sizeof vals[1], "%s", g_confirm_del ? "ON" : "OFF");
        snprintf(vals[2], sizeof vals[2], "%s", g_show_hidden ? "ON" : "OFF");
        snprintf(vals[3], sizeof vals[3], "%s", sort_names[g_sort]);
        snprintf(vals[4], sizeof vals[4], "%d%%", g_preview_pct);
        snprintf(vals[5], sizeof vals[5], "%s", g_gif_anim ? "ON" : "OFF");
        snprintf(vals[6], sizeof vals[6], "%s", "<Enter>");
        for (int i = 0; i < N; i++)
            printf("%s  %-16s %-12s\x1b[0m\r\n", i == sel ? "\x1b[7m" : "  ", names[i], vals[i]);
        fflush(stdout);
        int k = tui_read_key();
        if (k == TK_ESC || k == 'q') break;
        else if (k == TK_UP)   sel = (sel + N - 1) % N;
        else if (k == TK_DOWN) sel = (sel + 1) % N;
        else if (k == TK_LEFT || k == TK_RIGHT || k == TK_ENTER || k == ' ') {
            int dir = (k == TK_LEFT) ? -1 : 1;
            switch (sel) {
                case 0: g_preview = !g_preview; break;
                case 1: g_confirm_del = !g_confirm_del; break;
                case 2: g_show_hidden = !g_show_hidden; g_sel = 0; load_dir(); break;
                case 3: g_sort = (g_sort + 3 + dir) % 3; load_dir(); break;
                case 4: g_preview_pct += dir * 2;
                        if (g_preview_pct < 20) g_preview_pct = 20;
                        if (g_preview_pct > 80) g_preview_pct = 80;
                        break;
                case 5: g_gif_anim = !g_gif_anim; break;
                case 6: cfm_config_save(); break;
            }
        }
    }
    g_pv_path[0] = 0;
    set_status("");
}

int main(int argc, char **argv) {
    if (argc > 1) {
        strncpy(g_cwd, argv[1], sizeof(g_cwd) - 1);
        g_cwd[sizeof(g_cwd) - 1] = 0;
    } else if (!getcwd(g_cwd, sizeof(g_cwd))) {
        strcpy(g_cwd, "/");
    }

    cfm_config_load();
    tui_begin();
    tui_size(&g_rows, &g_cols);
    if (g_cols > 590) g_cols = 590;
    load_dir();
    set_status("");

    int running = 1;
    while (running) {
        tui_size(&g_rows, &g_cols);
        if (g_cols > 590) g_cols = 590;
        draw();
        int k = read_key_animated();
        switch (k) {
        case TK_RESIZE: break;
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
        case 's': do_settings(); break;
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <http.h>
#include <tui.h>

static int g_insecure = 0;

static int fetch(const char *url, char **out, size_t *outlen) {
    int pfd[2];
    if (pipe(pfd) < 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]);
        http_opts o; memset(&o, 0, sizeof o);
        o.header_fd = -1; o.data_len = -1; o.follow = 1; o.max_redirs = 20;
        o.insecure = g_insecure; o.user_agent = "Cervus-browser/1.0";
        http_request(url, pfd[1], &o);
        close(pfd[1]);
        _exit(0);
    }
    close(pfd[1]);
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) { close(pfd[0]); return -1; }
    for (;;) {
        if (len + 8192 > cap) { cap *= 2; char *n = realloc(buf, cap); if (!n) break; buf = n; }
        long r = read(pfd[0], buf + len, cap - len);
        if (r <= 0) break;
        len += r;
    }
    close(pfd[0]);
    int st; waitpid(pid, &st, 0);
    buf[len] = 0;
    *out = buf; *outlen = len;
    return len > 0 ? 0 : -1;
}

typedef struct { char **v; int n, cap; } strv;
static void sv_push(strv *s, const char *str) {
    if (s->n >= s->cap) { s->cap = s->cap ? s->cap * 2 : 64; s->v = realloc(s->v, sizeof(char *) * s->cap); }
    s->v[s->n++] = strdup(str ? str : "");
}
static void sv_free(strv *s) { for (int i = 0; i < s->n; i++) free(s->v[i]); free(s->v); s->v = 0; s->n = s->cap = 0; }

static strv g_lines;
static strv g_links;
static char g_title[256];

static char *g_cur;
static int g_curlen, g_curcap, g_width, g_col;

static void cur_reset(void) { g_curlen = 0; g_col = 0; if (g_cur) g_cur[0] = 0; }
static void push_line(void) {
    if (g_cur) g_cur[g_curlen] = 0;
    sv_push(&g_lines, g_cur ? g_cur : "");
    cur_reset();
}
static void cur_putc(char c) {
    if (g_curlen + 2 > g_curcap) { g_curcap = g_curcap ? g_curcap * 2 : 256; g_cur = realloc(g_cur, g_curcap); }
    g_cur[g_curlen++] = c; g_col++;
}
static void emit_word(const char *w, int wl) {
    if (wl == 0) return;
    if (g_col > 0 && g_col + 1 + wl > g_width) push_line();
    if (g_col > 0) cur_putc(' ');
    for (int i = 0; i < wl; i++) cur_putc(w[i]);
}
static void emit_break(void) {
    push_line();
    int n = g_lines.n;
    if (n >= 2 && g_lines.v[n-1][0] == 0 && g_lines.v[n-2][0] == 0) { free(g_lines.v[--g_lines.n]); }
}

static int utf8_enc(int cp, char *o) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

static int ent_decode(const char *p, char *obuf, int *outn) {
    if (p[0] != '&') return 0;
    if (p[1] == '#') {
        int code = 0, i = 2, ni = 0, hex = (p[2] == 'x' || p[2] == 'X'); if (hex) i = 3;
        while (p[i] && p[i] != ';' && ni < 8) { char c = p[i]; int d; if (c >= '0' && c <= '9') d = c - '0'; else if (hex && (c|32) >= 'a' && (c|32) <= 'f') d = (c|32) - 'a' + 10; else break; code = hex ? code*16+d : code*10+d; i++; ni++; }
        if (p[i] != ';' || ni == 0) return 0;
        if (code == 160) { obuf[0] = ' '; *outn = 1; } else *outn = utf8_enc(code, obuf);
        return i + 1;
    }
    char name[16]; int i = 1, ni = 0;
    while (p[i] && p[i] != ';' && ni < 15) name[ni++] = p[i++];
    name[ni] = 0;
    if (p[i] != ';') return 0;
    int cp;
    if (!strcmp(name, "amp")) cp = '&';
    else if (!strcmp(name, "lt")) cp = '<';
    else if (!strcmp(name, "gt")) cp = '>';
    else if (!strcmp(name, "quot")) cp = '"';
    else if (!strcmp(name, "apos")) cp = '\'';
    else if (!strcmp(name, "nbsp")) cp = ' ';
    else if (!strcmp(name, "mdash") || !strcmp(name, "ndash")) cp = '-';
    else if (!strcmp(name, "copy")) cp = 0xA9;
    else if (!strcmp(name, "reg")) cp = 0xAE;
    else if (!strcmp(name, "trade")) cp = 0x2122;
    else if (!strcmp(name, "hellip")) cp = 0x2026;
    else if (!strcmp(name, "rsquo") || !strcmp(name, "lsquo")) cp = '\'';
    else if (!strcmp(name, "rdquo") || !strcmp(name, "ldquo")) cp = '"';
    else return 0;
    *outn = utf8_enc(cp, obuf);
    return i + 1;
}

static void decode_inplace(char *s) {
    char *d = s, *p = s;
    while (*p) {
        if (*p == '&') { char eb[4]; int en, a = ent_decode(p, eb, &en); if (a) { for (int k = 0; k < en; k++) *d++ = eb[k]; p += a; continue; } }
        *d++ = *p++;
    }
    *d = 0;
}

static int tag_is(const char *t, const char *name) {
    while (*name) { if ((*t | 32) != *name) return 0; t++; name++; }
    return (*t == 0 || *t == ' ' || *t == '\t' || *t == '>' || *t == '/');
}

static void render_html(const char *h, int width) {
    g_width = width < 20 ? 20 : width;
    g_title[0] = 0;
    int pre = 0, skip = 0;
    char word[512]; int wl = 0;
    const char *p = h;
    while (*p) {
        if (*p == '<') {
            if (wl) { emit_word(word, wl); wl = 0; }
            const char *t = p + 1;
            int close_tag = (*t == '/'); if (close_tag) t++;
            if (!strncmp(t, "!--", 3)) { const char *e = strstr(t, "-->"); p = e ? e + 3 : p + strlen(p); continue; }
            if (tag_is(t, "script") || tag_is(t, "style") || tag_is(t, "title") || tag_is(t, "head")) skip = !close_tag;
            if (tag_is(t, "body")) skip = 0;
            if (!close_tag) {
                if (tag_is(t, "title")) {
                    const char *e = strchr(t, '>');
                    if (e) { const char *te = strstr(e, "</"); int tn = 0; for (const char *q = e+1; te && q < te && tn < 255; q++) if (*q != '\n') g_title[tn++] = *q; g_title[tn] = 0; decode_inplace(g_title); }
                }
                if (tag_is(t, "br")) emit_break();
                else if (tag_is(t, "p") || tag_is(t, "div") || tag_is(t, "h1") || tag_is(t, "h2") || tag_is(t, "h3") ||
                         tag_is(t, "h4") || tag_is(t, "h5") || tag_is(t, "h6") || tag_is(t, "ul") || tag_is(t, "ol") ||
                         tag_is(t, "table") || tag_is(t, "tr") || tag_is(t, "section") || tag_is(t, "article") ||
                         tag_is(t, "header") || tag_is(t, "footer") || tag_is(t, "blockquote") || tag_is(t, "form")) emit_break();
                else if (tag_is(t, "li")) { emit_break(); emit_word("*", 1); }
                else if (tag_is(t, "hr")) { emit_break(); char bar[64]; int bn = width < 40 ? width : 40; for (int i=0;i<bn;i++) bar[i]='-'; emit_word(bar, bn); emit_break(); }
                else if (tag_is(t, "pre")) { emit_break(); pre = 1; }
                else if (tag_is(t, "a")) {
                    const char *hp = strstr(t, "href");
                    if (hp) { hp = strchr(hp, '='); if (hp) { hp++; while (*hp==' '||*hp=='"'||*hp=='\'') hp++; char url[1024]; int un=0; while (*hp && *hp!='"' && *hp!='\'' && *hp!=' ' && *hp!='>' && un<1023) url[un++]=*hp++; url[un]=0; sv_push(&g_links, url); char mk[16]; int mn=snprintf(mk,sizeof mk,"[%d]",g_links.n); emit_word(mk, mn); } }
                }
            } else {
                if (tag_is(t, "pre")) { pre = 0; emit_break(); }
                else if (tag_is(t, "p") || tag_is(t, "div") || tag_is(t, "li") || tag_is(t, "tr") ||
                         tag_is(t, "h1") || tag_is(t, "h2") || tag_is(t, "h3") || tag_is(t, "ul") || tag_is(t, "ol")) emit_break();
            }
            const char *e = strchr(p, '>');
            p = e ? e + 1 : p + strlen(p);
            continue;
        }
        if (skip) { p++; continue; }
        if (*p == '&') {
            char eb[4]; int en, adv = ent_decode(p, eb, &en);
            if (adv) { for (int k = 0; k < en && wl < 500; k++) word[wl++] = eb[k]; p += adv; continue; }
        }
        if (pre) {
            if (wl) { emit_word(word, wl); wl = 0; }
            if (*p == '\n') push_line(); else if (*p == '\t') { cur_putc(' '); cur_putc(' '); } else cur_putc(*p);
            p++;
            continue;
        }
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            if (wl) { emit_word(word, wl); wl = 0; }
            p++;
            continue;
        }
        if (wl < 500) word[wl++] = *p;
        p++;
    }
    if (wl) emit_word(word, wl);
    push_line();
}

static void resolve(const char *base, const char *rel, char *out, int cap) {
    if (!strncmp(rel, "http://", 7) || !strncmp(rel, "https://", 8)) { strncpy(out, rel, cap-1); out[cap-1]=0; return; }
    int https = !strncmp(base, "https://", 8);
    const char *scheme = https ? "https://" : "http://";
    const char *hs = base + (https ? 8 : 7);
    const char *pe = strchr(hs, '/');
    char hostport[300]; int hl = pe ? (int)(pe - hs) : (int)strlen(hs);
    if (hl > 299) hl = 299;
    memcpy(hostport, hs, hl); hostport[hl] = 0;
    if (rel[0] == '/') { snprintf(out, cap, "%s%s%s", scheme, hostport, rel); return; }
    if (!strncmp(rel, "//", 2)) { snprintf(out, cap, "%s%s", scheme, rel + 2); return; }
    char dir[1024]; if (pe) { int dl=(int)strlen(pe); if(dl>1023)dl=1023; memcpy(dir,pe,dl); dir[dl]=0; char *s=strrchr(dir,'/'); if(s) s[1]=0; } else strcpy(dir,"/");
    snprintf(out, cap, "%s%s%s%s", scheme, hostport, dir, rel);
}

static void load(const char *url) {
    sv_free(&g_lines); sv_free(&g_links);
    char *html = 0; size_t hl = 0;
    int rows, cols; tui_size(&rows, &cols);
    if (fetch(url, &html, &hl) != 0) { sv_push(&g_lines, "Failed to load:"); sv_push(&g_lines, url); if (html) free(html); return; }
    render_html(html, cols);
    free(html);
    if (g_lines.n == 0) sv_push(&g_lines, "(empty page)");
}

int main(int argc, char **argv) {
    const char *start = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-k")) g_insecure = 1;
        else start = argv[i];
    }
    if (!start) { printf("usage: browser [-k] <url>\n"); return 1; }

    char cur[2048];
    if (strncmp(start, "http", 4)) snprintf(cur, sizeof cur, "http://%s", start);
    else { strncpy(cur, start, sizeof cur - 1); cur[sizeof cur - 1] = 0; }

    strv hist; memset(&hist, 0, sizeof hist);

    tui_begin();
    load(cur);
    int top = 0;
    char numbuf[8]; int nb = 0; numbuf[0] = 0;

    for (;;) {
        int rows, cols; tui_size(&rows, &cols);
        int view = rows - 1;
        if (top > g_lines.n - view) top = g_lines.n - view;
        if (top < 0) top = 0;

        tui_hide_cursor();
        tui_home();
        for (int i = 0; i < view; i++) {
            tui_move(i + 1, 1);
            tui_puts("\x1b[K");
            int li = top + i;
            if (li < g_lines.n) {
                const char *s = g_lines.v[li];
                for (const char *q = s; *q; ) {
                    if (*q == '[' && q[1] >= '0' && q[1] <= '9') {
                        tui_puts("\x1b[38;2;97;175;239m");
                        while (*q && *q != ']') { char b[2]={*q,0}; tui_puts(b); q++; }
                        if (*q == ']') { tui_puts("]"); q++; }
                        tui_puts("\x1b[39m");
                    } else { char b[2]={*q,0}; tui_puts(b); q++; }
                }
            }
        }
        tui_move(rows, 1);
        tui_puts("\x1b[7m\x1b[K");
        char st[512];
        int pct = g_lines.n > view ? (top * 100 / (g_lines.n - view)) : 100;
        snprintf(st, sizeof st, " %.40s | %.60s | %d%% | [n]link g:go b:back r:reload q:quit%s%s",
                 g_title[0] ? g_title : "(no title)", cur, pct, nb ? " #" : "", numbuf);
        int sl = strlen(st); if (sl > cols) sl = cols;
        char tmp[600]; memcpy(tmp, st, sl); tmp[sl] = 0;
        tui_puts(tmp);
        tui_puts("\x1b[27m");

        int k = tui_read_key();
        if (k == 'q') break;
        else if (k == TK_DOWN) top++;
        else if (k == TK_UP) { if (top > 0) top--; }
        else if (k == TK_PGDN || k == ' ') top += view - 1;
        else if (k == TK_PGUP) top -= view - 1;
        else if (k == TK_HOME) top = 0;
        else if (k == TK_END) top = g_lines.n;
        else if (k >= '0' && k <= '9') { if (nb < 6) { numbuf[nb++] = (char)k; numbuf[nb] = 0; } }
        else if (k == TK_BACKSP) { if (nb > 0) numbuf[--nb] = 0; }
        else if (k == TK_ESC) { nb = 0; numbuf[0] = 0; }
        else if (k == TK_ENTER) {
            if (nb > 0) {
                int idx = atoi(numbuf); nb = 0; numbuf[0] = 0;
                if (idx >= 1 && idx <= g_links.n) {
                    char nu[2048]; resolve(cur, g_links.v[idx-1], nu, sizeof nu);
                    sv_push(&hist, cur);
                    strncpy(cur, nu, sizeof cur - 1); cur[sizeof cur - 1] = 0;
                    load(cur); top = 0;
                }
            }
        }
        else if (k == 'b') {
            if (hist.n > 0) { strncpy(cur, hist.v[hist.n-1], sizeof cur - 1); cur[sizeof cur-1]=0; free(hist.v[--hist.n]); load(cur); top = 0; }
        }
        else if (k == 'r') load(cur);
        else if (k == 'g') {
            tui_move(rows, 1); tui_puts("\x1b[K\x1b[27mgo to url: "); tui_show_cursor();
            char u[1024]; int ul = 0;
            for (;;) { int c = tui_read_key(); if (c == TK_ENTER) break; if (c == TK_ESC) { ul = 0; break; } if (c == TK_BACKSP) { if (ul>0){ul--; tui_puts("\b \b"); } continue; } if (c >= 32 && c < 127 && ul < 1022) { u[ul++]=(char)c; char b[2]={(char)c,0}; tui_puts(b); } }
            u[ul] = 0;
            if (ul) { sv_push(&hist, cur); if (strncmp(u,"http",4)) snprintf(cur,sizeof cur,"http://%s",u); else { strncpy(cur,u,sizeof cur-1); cur[sizeof cur-1]=0; } load(cur); top = 0; }
        }
    }

    tui_end();
    sv_free(&g_lines); sv_free(&g_links); sv_free(&hist);
    return 0;
}

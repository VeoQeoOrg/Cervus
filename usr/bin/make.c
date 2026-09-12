#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cervus_util.h>

extern char **environ;

#define MK_MAX_DEPTH 128

static int g_dry_run;
static int g_keep_going;
static int g_silent;
static int g_ignore_errors;
static int g_always_make;
static int g_question;
static int g_failed;

static void fatal(const char *m)
{
    fprintf(stderr, "make: %s\n", m);
    exit(2);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) fatal("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) fatal("out of memory");
    return q;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

typedef struct {
    char  *p;
    size_t n, cap;
} sbuf;

static void sb_init(sbuf *b) { b->p = NULL; b->n = 0; b->cap = 0; }

static void sb_putn(sbuf *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 64;
        while (cap < b->n + n + 1) cap *= 2;
        b->p = xrealloc(b->p, cap);
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

static void sb_puts(sbuf *b, const char *s) { sb_putn(b, s, strlen(s)); }
static void sb_putc(sbuf *b, char c) { sb_putn(b, &c, 1); }

static char *sb_take(sbuf *b)
{
    char *r = b->p ? b->p : xstrdup("");
    b->p = NULL; b->n = 0; b->cap = 0;
    return r;
}

typedef struct {
    char **v;
    int    n, cap;
} svec;

static void sv_init(svec *v) { v->v = NULL; v->n = 0; v->cap = 0; }

static void sv_push(svec *v, char *s)
{
    if (v->n + 1 >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->v = xrealloc(v->v, (size_t)v->cap * sizeof(char *));
    }
    v->v[v->n++] = s;
    v->v[v->n] = NULL;
}

static void sv_free(svec *v)
{
    for (int i = 0; i < v->n; i++) free(v->v[i]);
    free(v->v);
    sv_init(v);
}

static void split_words(const char *s, svec *out)
{
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\n') s++;
        if (!*s) break;
        const char *start = s;
        while (*s && *s != ' ' && *s != '\t' && *s != '\n') s++;
        sv_push(out, xstrndup(start, (size_t)(s - start)));
    }
}

static char *join_words(svec *v, const char *sep)
{
    sbuf b;
    sb_init(&b);
    for (int i = 0; i < v->n; i++) {
        if (i) sb_puts(&b, sep);
        sb_puts(&b, v->v[i]);
    }
    return sb_take(&b);
}

typedef struct var {
    char *name;
    char *value;
    int   simple;
    int   from_env;
    int   exported;
    struct var *next;
} var_t;

static var_t *g_vars;

static var_t *var_find(const char *name)
{
    for (var_t *v = g_vars; v; v = v->next)
        if (!strcmp(v->name, name)) return v;
    return NULL;
}

static void var_set(const char *name, const char *value, int simple)
{
    var_t *v = var_find(name);
    if (!v) {
        v = xmalloc(sizeof(var_t));
        memset(v, 0, sizeof(*v));
        v->name = xstrdup(name);
        v->next = g_vars;
        g_vars = v;
    }
    free(v->value);
    v->value = xstrdup(value);
    v->simple = simple;
    v->from_env = 0;
}

typedef struct rule {
    svec targets;
    svec prereqs;
    svec order_only;
    svec recipe;
    int  is_pattern;
    int  double_colon;
    struct rule *next;
} rule_t;

static rule_t *g_rules;
static rule_t *g_rules_tail;
static svec    g_phony;
static svec    g_goals;
static char   *g_default_goal;

static void rule_add(rule_t *r)
{
    r->next = NULL;
    if (g_rules_tail) g_rules_tail->next = r;
    else g_rules = r;
    g_rules_tail = r;
}

static int is_phony(const char *t)
{
    for (int i = 0; i < g_phony.n; i++)
        if (!strcmp(g_phony.v[i], t)) return 1;
    return 0;
}

static char *expand(const char *s);

static char *call_function(const char *name, svec *args);

static char *trim_inplace(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

static const char *find_close(const char *s, char open, char close)
{
    int depth = 1;
    for (; *s; s++) {
        if (*s == '$' && (s[1] == open)) { s++; depth++; continue; }
        if (*s == open) depth++;
        else if (*s == close) {
            depth--;
            if (!depth) return s;
        }
    }
    return NULL;
}

static void split_args(const char *s, svec *out)
{
    int depth = 0;
    const char *start = s;
    for (const char *p = s;; p++) {
        if (*p == '(' || *p == '{') depth++;
        else if (*p == ')' || *p == '}') depth--;
        if (*p == 0 || (*p == ',' && depth == 0)) {
            sv_push(out, xstrndup(start, (size_t)(p - start)));
            if (!*p) break;
            start = p + 1;
        }
    }
}

static char *expand(const char *s)
{
    sbuf out;
    sb_init(&out);
    if (!s) return sb_take(&out);

    while (*s) {
        if (*s != '$') { sb_putc(&out, *s++); continue; }
        s++;
        if (!*s) { sb_putc(&out, '$'); break; }
        if (*s == '$') { sb_putc(&out, '$'); s++; continue; }

        char open = *s, close = 0;
        if (open == '(') close = ')';
        else if (open == '{') close = '}';

        if (!close) {
            char nm[2] = { *s, 0 };
            var_t *v = var_find(nm);
            if (v) {
                char *e = v->simple ? xstrdup(v->value) : expand(v->value);
                sb_puts(&out, e);
                free(e);
            }
            s++;
            continue;
        }

        const char *body = s + 1;
        const char *end = find_close(body, open, close);
        if (!end) { sb_putc(&out, '$'); continue; }

        char *inner = xstrndup(body, (size_t)(end - body));
        s = end + 1;

        const char *sp = inner;
        while (*sp && !isspace((unsigned char)*sp)) sp++;
        char *fname = xstrndup(inner, (size_t)(sp - inner));

        static const char *const FUNCS[] = {
            "subst", "patsubst", "strip", "findstring", "filter", "filter-out",
            "sort", "word", "words", "wordlist", "firstword", "lastword",
            "dir", "notdir", "suffix", "basename", "addsuffix", "addprefix",
            "join", "wildcard", "shell", "if", "foreach", "call", "error",
            "warning", "info", "abspath", "realpath", NULL
        };
        int isfunc = 0;
        for (int i = 0; FUNCS[i]; i++)
            if (!strcmp(fname, FUNCS[i])) { isfunc = 1; break; }

        if (isfunc && *sp) {
            while (*sp && isspace((unsigned char)*sp)) sp++;
            svec args;
            sv_init(&args);
            split_args(sp, &args);
            char *r = call_function(fname, &args);
            sv_free(&args);
            if (r) { sb_puts(&out, r); free(r); }
            free(fname);
            free(inner);
            continue;
        }
        free(fname);

        char *colon = strchr(inner, ':');
        if (colon && strchr(colon, '=')) {
            *colon = 0;
            char *name = expand(inner);
            char *pat = colon + 1;
            char *eq = strchr(pat, '=');
            *eq = 0;
            char *from = expand(pat);
            char *to = expand(eq + 1);

            var_t *v = var_find(name);
            char *val = v ? (v->simple ? xstrdup(v->value) : expand(v->value)) : xstrdup("");
            svec words;
            sv_init(&words);
            split_words(val, &words);
            free(val);

            size_t flen = strlen(from);
            for (int i = 0; i < words.n; i++) {
                if (i) sb_putc(&out, ' ');
                size_t wl = strlen(words.v[i]);
                if (flen && wl >= flen && !strcmp(words.v[i] + wl - flen, from)) {
                    sb_putn(&out, words.v[i], wl - flen);
                    sb_puts(&out, to);
                } else {
                    sb_puts(&out, words.v[i]);
                }
            }
            sv_free(&words);
            free(name); free(from); free(to);
            free(inner);
            continue;
        }

        char *name = expand(inner);
        var_t *v = var_find(name);
        if (v) {
            char *e = v->simple ? xstrdup(v->value) : expand(v->value);
            sb_puts(&out, e);
            free(e);
        } else {
            const char *env = getenv(name);
            if (env) sb_puts(&out, env);
        }
        free(name);
        free(inner);
    }
    return sb_take(&out);
}

static int pattern_match(const char *pat, const char *str, char *stem, size_t cap)
{
    const char *pc = strchr(pat, '%');
    if (!pc) {
        if (strcmp(pat, str)) return 0;
        if (stem && cap) stem[0] = 0;
        return 1;
    }
    size_t plen = (size_t)(pc - pat);
    size_t slen = strlen(pc + 1);
    size_t tlen = strlen(str);
    if (tlen < plen + slen) return 0;
    if (strncmp(pat, str, plen)) return 0;
    if (slen && strcmp(str + tlen - slen, pc + 1)) return 0;
    size_t stem_len = tlen - plen - slen;
    if (stem) {
        if (stem_len >= cap) stem_len = cap - 1;
        memcpy(stem, str + plen, stem_len);
        stem[stem_len] = 0;
    }
    return 1;
}

static char *pattern_subst(const char *pat, const char *stem)
{
    const char *pc = strchr(pat, '%');
    if (!pc) return xstrdup(pat);
    sbuf b;
    sb_init(&b);
    sb_putn(&b, pat, (size_t)(pc - pat));
    sb_puts(&b, stem);
    sb_puts(&b, pc + 1);
    return sb_take(&b);
}

static char *run_shell_capture(const char *cmd)
{
    int pfd[2];
    if (pipe(pfd) < 0) return xstrdup("");
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return xstrdup(""); }
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], 1);
        if (pfd[1] != 1) close(pfd[1]);
        char *argv[4] = { "/bin/sh", "-c", (char *)cmd, NULL };
        execve("/bin/sh", argv, environ);
        _exit(127);
    }
    close(pfd[1]);

    sbuf b;
    sb_init(&b);
    char tmp[1024];
    ssize_t r;
    while ((r = read(pfd[0], tmp, sizeof tmp)) > 0) sb_putn(&b, tmp, (size_t)r);
    close(pfd[0]);
    int st = 0;
    waitpid(pid, &st, 0);

    char *s = sb_take(&b);
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
    for (char *p = s; *p; p++) if (*p == '\n') *p = ' ';
    return s;
}

static void glob_into(const char *pattern, svec *out)
{
    const char *slash = strrchr(pattern, '/');
    char dir[512];
    const char *base;
    if (slash) {
        size_t dl = (size_t)(slash - pattern);
        if (dl >= sizeof dir) dl = sizeof dir - 1;
        memcpy(dir, pattern, dl);
        dir[dl] = 0;
        if (!dir[0]) strcpy(dir, "/");
        base = slash + 1;
    } else {
        strcpy(dir, ".");
        base = pattern;
    }

    if (!strchr(base, '*') && !strchr(base, '?') && !strchr(base, '[')) {
        struct stat st;
        if (stat(pattern, &st) == 0) sv_push(out, xstrdup(pattern));
        return;
    }

    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    svec hits;
    sv_init(&hits);
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' && base[0] != '.') continue;
        if (fnmatch(base, de->d_name, 0) != 0) continue;
        char full[1024];
        if (slash) snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        else       snprintf(full, sizeof full, "%s", de->d_name);
        sv_push(&hits, xstrdup(full));
    }
    closedir(d);

    for (int i = 0; i < hits.n; i++)
        for (int j = i + 1; j < hits.n; j++)
            if (strcmp(hits.v[i], hits.v[j]) > 0) {
                char *t = hits.v[i]; hits.v[i] = hits.v[j]; hits.v[j] = t;
            }
    for (int i = 0; i < hits.n; i++) sv_push(out, hits.v[i]);
    free(hits.v);
}

static char *call_function(const char *name, svec *args)
{
    sbuf out;
    sb_init(&out);

    if (!strcmp(name, "subst") && args->n >= 3) {
        char *from = expand(args->v[0]);
        char *to = expand(args->v[1]);
        char *text = expand(args->v[2]);
        size_t fl = strlen(from);
        if (!fl) { sb_puts(&out, text); }
        else {
            for (const char *p = text; *p; ) {
                if (!strncmp(p, from, fl)) { sb_puts(&out, to); p += fl; }
                else sb_putc(&out, *p++);
            }
        }
        free(from); free(to); free(text);
        return sb_take(&out);
    }

    if (!strcmp(name, "patsubst") && args->n >= 3) {
        char *pat = expand(args->v[0]);
        char *rep = expand(args->v[1]);
        char *text = expand(args->v[2]);
        svec w; sv_init(&w);
        split_words(text, &w);
        for (int i = 0; i < w.n; i++) {
            if (i) sb_putc(&out, ' ');
            char stem[512];
            if (pattern_match(pat, w.v[i], stem, sizeof stem)) {
                char *r = pattern_subst(rep, stem);
                sb_puts(&out, r);
                free(r);
            } else sb_puts(&out, w.v[i]);
        }
        sv_free(&w);
        free(pat); free(rep); free(text);
        return sb_take(&out);
    }

    if (!strcmp(name, "strip") && args->n >= 1) {
        char *text = expand(args->v[0]);
        svec w; sv_init(&w);
        split_words(text, &w);
        char *j = join_words(&w, " ");
        sv_free(&w);
        free(text);
        return j;
    }

    if (!strcmp(name, "findstring") && args->n >= 2) {
        char *a = expand(args->v[0]);
        char *b = expand(args->v[1]);
        if (strstr(b, a)) sb_puts(&out, a);
        free(a); free(b);
        return sb_take(&out);
    }

    if ((!strcmp(name, "filter") || !strcmp(name, "filter-out")) && args->n >= 2) {
        int keep = !strcmp(name, "filter");
        char *pats = expand(args->v[0]);
        char *text = expand(args->v[1]);
        svec pw, tw;
        sv_init(&pw); sv_init(&tw);
        split_words(pats, &pw);
        split_words(text, &tw);
        int first = 1;
        for (int i = 0; i < tw.n; i++) {
            int hit = 0;
            for (int j = 0; j < pw.n && !hit; j++)
                hit = pattern_match(pw.v[j], tw.v[i], NULL, 0);
            if (hit == keep) {
                if (!first) sb_putc(&out, ' ');
                sb_puts(&out, tw.v[i]);
                first = 0;
            }
        }
        sv_free(&pw); sv_free(&tw);
        free(pats); free(text);
        return sb_take(&out);
    }

    if (!strcmp(name, "sort") && args->n >= 1) {
        char *text = expand(args->v[0]);
        svec w; sv_init(&w);
        split_words(text, &w);
        for (int i = 0; i < w.n; i++)
            for (int j = i + 1; j < w.n; j++)
                if (strcmp(w.v[i], w.v[j]) > 0) {
                    char *t = w.v[i]; w.v[i] = w.v[j]; w.v[j] = t;
                }
        int first = 1;
        for (int i = 0; i < w.n; i++) {
            if (i && !strcmp(w.v[i], w.v[i - 1])) continue;
            if (!first) sb_putc(&out, ' ');
            sb_puts(&out, w.v[i]);
            first = 0;
        }
        sv_free(&w);
        free(text);
        return sb_take(&out);
    }

    if (!strcmp(name, "words") && args->n >= 1) {
        char *text = expand(args->v[0]);
        svec w; sv_init(&w);
        split_words(text, &w);
        char n[32];
        snprintf(n, sizeof n, "%d", w.n);
        sb_puts(&out, n);
        sv_free(&w);
        free(text);
        return sb_take(&out);
    }

    if ((!strcmp(name, "word") || !strcmp(name, "wordlist")) && args->n >= 2) {
        int wl = !strcmp(name, "wordlist");
        char *a = expand(args->v[0]);
        char *b = wl && args->n >= 2 ? expand(args->v[1]) : NULL;
        char *text = expand(args->v[wl ? 2 : 1]);
        svec w; sv_init(&w);
        split_words(text, &w);
        int from = atoi(a);
        int to = wl ? atoi(b) : from;
        int first = 1;
        for (int i = from; i <= to; i++) {
            if (i < 1 || i > w.n) continue;
            if (!first) sb_putc(&out, ' ');
            sb_puts(&out, w.v[i - 1]);
            first = 0;
        }
        sv_free(&w);
        free(a); free(b); free(text);
        return sb_take(&out);
    }

    if ((!strcmp(name, "firstword") || !strcmp(name, "lastword")) && args->n >= 1) {
        char *text = expand(args->v[0]);
        svec w; sv_init(&w);
        split_words(text, &w);
        if (w.n) sb_puts(&out, !strcmp(name, "firstword") ? w.v[0] : w.v[w.n - 1]);
        sv_free(&w);
        free(text);
        return sb_take(&out);
    }

    if ((!strcmp(name, "dir") || !strcmp(name, "notdir") ||
         !strcmp(name, "suffix") || !strcmp(name, "basename")) && args->n >= 1) {
        char *text = expand(args->v[0]);
        svec w; sv_init(&w);
        split_words(text, &w);
        int first = 1;
        for (int i = 0; i < w.n; i++) {
            const char *s = w.v[i];
            const char *slash = strrchr(s, '/');
            char piece[1024];
            piece[0] = 0;
            if (!strcmp(name, "dir")) {
                if (slash) snprintf(piece, sizeof piece, "%.*s", (int)(slash - s) + 1, s);
                else snprintf(piece, sizeof piece, "./");
            } else if (!strcmp(name, "notdir")) {
                snprintf(piece, sizeof piece, "%s", slash ? slash + 1 : s);
            } else {
                const char *base = slash ? slash + 1 : s;
                const char *dot = strrchr(base, '.');
                if (!strcmp(name, "suffix")) {
                    if (dot) snprintf(piece, sizeof piece, "%s", dot);
                } else {
                    if (dot) snprintf(piece, sizeof piece, "%.*s", (int)(dot - s), s);
                    else snprintf(piece, sizeof piece, "%s", s);
                }
            }
            if (!piece[0] && (!strcmp(name, "suffix"))) continue;
            if (!first) sb_putc(&out, ' ');
            sb_puts(&out, piece);
            first = 0;
        }
        sv_free(&w);
        free(text);
        return sb_take(&out);
    }

    if ((!strcmp(name, "addprefix") || !strcmp(name, "addsuffix")) && args->n >= 2) {
        char *fix = expand(args->v[0]);
        char *text = expand(args->v[1]);
        svec w; sv_init(&w);
        split_words(text, &w);
        for (int i = 0; i < w.n; i++) {
            if (i) sb_putc(&out, ' ');
            if (!strcmp(name, "addprefix")) { sb_puts(&out, fix); sb_puts(&out, w.v[i]); }
            else { sb_puts(&out, w.v[i]); sb_puts(&out, fix); }
        }
        sv_free(&w);
        free(fix); free(text);
        return sb_take(&out);
    }

    if (!strcmp(name, "join") && args->n >= 2) {
        char *a = expand(args->v[0]);
        char *b = expand(args->v[1]);
        svec aw, bw;
        sv_init(&aw); sv_init(&bw);
        split_words(a, &aw);
        split_words(b, &bw);
        int n = aw.n > bw.n ? aw.n : bw.n;
        for (int i = 0; i < n; i++) {
            if (i) sb_putc(&out, ' ');
            if (i < aw.n) sb_puts(&out, aw.v[i]);
            if (i < bw.n) sb_puts(&out, bw.v[i]);
        }
        sv_free(&aw); sv_free(&bw);
        free(a); free(b);
        return sb_take(&out);
    }

    if (!strcmp(name, "wildcard") && args->n >= 1) {
        char *text = expand(args->v[0]);
        svec w, hits;
        sv_init(&w); sv_init(&hits);
        split_words(text, &w);
        for (int i = 0; i < w.n; i++) glob_into(w.v[i], &hits);
        char *j = join_words(&hits, " ");
        sv_free(&w); sv_free(&hits);
        free(text);
        return j;
    }

    if (!strcmp(name, "shell") && args->n >= 1) {
        char *cmd = expand(args->v[0]);
        char *r = run_shell_capture(cmd);
        free(cmd);
        return r;
    }

    if (!strcmp(name, "if") && args->n >= 2) {
        char *cond = expand(args->v[0]);
        svec w; sv_init(&w);
        split_words(cond, &w);
        int t = w.n > 0;
        sv_free(&w);
        free(cond);
        if (t) return expand(args->v[1]);
        if (args->n >= 3) return expand(args->v[2]);
        return sb_take(&out);
    }

    if (!strcmp(name, "foreach") && args->n >= 3) {
        char *var = expand(args->v[0]);
        char *list = expand(args->v[1]);
        svec w; sv_init(&w);
        split_words(list, &w);
        var_t *saved = var_find(var);
        char *old = saved ? xstrdup(saved->value) : NULL;
        int had = saved != NULL;
        for (int i = 0; i < w.n; i++) {
            var_set(var, w.v[i], 1);
            char *piece = expand(args->v[2]);
            if (i) sb_putc(&out, ' ');
            sb_puts(&out, piece);
            free(piece);
        }
        if (had) var_set(var, old, 1);
        free(old);
        sv_free(&w);
        free(var); free(list);
        return sb_take(&out);
    }

    if ((!strcmp(name, "error") || !strcmp(name, "warning") || !strcmp(name, "info"))
        && args->n >= 1) {
        char *msg = expand(args->v[0]);
        if (!strcmp(name, "info")) printf("%s\n", msg);
        else fprintf(stderr, "make: %s\n", msg);
        if (!strcmp(name, "error")) exit(2);
        free(msg);
        return sb_take(&out);
    }

    if (!strcmp(name, "call") && args->n >= 1) {
        char *fn = expand(args->v[0]);
        char *tfn = trim_inplace(fn);
        var_t *v = var_find(tfn);
        if (!v) { free(fn); return sb_take(&out); }

        char saved[10][256];
        int had[10];
        for (int i = 1; i <= 9; i++) {
            char nm[4];
            snprintf(nm, sizeof nm, "%d", i);
            var_t *old_v = var_find(nm);
            had[i] = old_v != NULL;
            if (old_v) snprintf(saved[i], sizeof saved[i], "%s", old_v->value);
            char *val = (i < args->n) ? expand(args->v[i]) : xstrdup("");
            var_set(nm, val, 1);
            free(val);
        }

        char *r = expand(v->value);

        for (int i = 1; i <= 9; i++) {
            char nm[4];
            snprintf(nm, sizeof nm, "%d", i);
            var_set(nm, had[i] ? saved[i] : "", 1);
        }
        free(fn);
        return r;
    }

    if ((!strcmp(name, "abspath") || !strcmp(name, "realpath")) && args->n >= 1)
        return expand(args->v[0]);

    return sb_take(&out);
}

static char *g_text;
static size_t g_pos, g_len;

static int read_logical_line(sbuf *out, int *is_recipe)
{
    sb_init(out);
    *is_recipe = 0;
    if (g_pos >= g_len) return 0;

    int first = 1;
    for (;;) {
        size_t start = g_pos;
        while (g_pos < g_len && g_text[g_pos] != '\n') g_pos++;
        size_t end = g_pos;
        if (g_pos < g_len) g_pos++;

        if (first) {
            *is_recipe = (end > start && g_text[start] == '\t');
            first = 0;
        }

        int cont = 0;
        if (end > start && g_text[end - 1] == '\\') {
            int bs = 0;
            size_t k = end;
            while (k > start && g_text[k - 1] == '\\') { bs++; k--; }
            if (bs & 1) { cont = 1; end--; }
        }

        sb_putn(out, g_text + start, end - start);
        if (!cont) break;
        if (*is_recipe) sb_putc(out, '\n');
        else sb_putc(out, ' ');
        if (g_pos >= g_len) break;
        if (!*is_recipe)
            while (g_pos < g_len && (g_text[g_pos] == ' ' || g_text[g_pos] == '\t')) g_pos++;
    }
    return 1;
}

static void strip_comment(char *s)
{
    for (char *p = s; *p; p++) {
        if (*p == '\\' && p[1]) { p++; continue; }
        if (*p == '#') { *p = 0; return; }
    }
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static void parse_text(char *text, const char *origin);

static void load_makefile(const char *path, int required)
{
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        if (required) {
            fprintf(stderr, "make: %s: no such file\n", path);
            exit(2);
        }
        return;
    }
    sbuf b;
    sb_init(&b);
    char tmp[4096];
    ssize_t r;
    while ((r = read(fd, tmp, sizeof tmp)) > 0) sb_putn(&b, tmp, (size_t)r);
    close(fd);

    char *save_text = g_text;
    size_t save_pos = g_pos, save_len = g_len;
    char *body = sb_take(&b);
    parse_text(body, path);
    free(body);
    g_text = save_text;
    g_pos = save_pos;
    g_len = save_len;
}

typedef struct cond {
    int taken;
    int active;
    struct cond *prev;
} cond_t;

static void parse_text(char *text, const char *origin)
{
    (void)origin;
    g_text = text;
    g_len = strlen(text);
    g_pos = 0;

    rule_t *cur = NULL;
    cond_t *conds = NULL;
    int skipping = 0;

    sbuf line;
    int is_recipe;

    while (read_logical_line(&line, &is_recipe)) {
        char *raw = line.p ? line.p : (char *)"";

        if (is_recipe && cur && !skipping) {
            char *body = raw + 1;
            char *t = body;
            while (*t == ' ' || *t == '\t') t++;
            if (*t == '#' || !*t) { free(line.p); continue; }
            sv_push(&cur->recipe, xstrdup(body));
            free(line.p);
            continue;
        }

        char *work = xstrdup(raw);
        free(line.p);
        strip_comment(work);
        char *s = trim(work);

        if (!*s) { free(work); continue; }

        if (!strncmp(s, "ifeq", 4) || !strncmp(s, "ifneq", 5) ||
            !strncmp(s, "ifdef", 5) || !strncmp(s, "ifndef", 6)) {
            int neg = (!strncmp(s, "ifneq", 5) || !strncmp(s, "ifndef", 6));
            int isdef = (!strncmp(s, "ifdef", 5) || !strncmp(s, "ifndef", 6));
            char *arg = s;
            while (*arg && !isspace((unsigned char)*arg) && *arg != '(') arg++;
            arg = trim(arg);

            int result = 0;
            if (isdef) {
                char *nm = expand(arg);
                char *n2 = trim(nm);
                var_t *v = var_find(n2);
                result = (v && v->value && v->value[0]) || (getenv(n2) != NULL);
                free(nm);
            } else {
                char a[1024] = "", b[1024] = "";
                if (*arg == '(') {
                    char *inner = xstrndup(arg + 1, strlen(arg + 1));
                    char *cl = strrchr(inner, ')');
                    if (cl) *cl = 0;
                    char *comma = NULL;
                    int depth = 0;
                    for (char *p = inner; *p; p++) {
                        if (*p == '(') depth++;
                        else if (*p == ')') depth--;
                        else if (*p == ',' && !depth) { comma = p; break; }
                    }
                    if (comma) {
                        *comma = 0;
                        char *ea = expand(inner);
                        char *eb = expand(comma + 1);
                        snprintf(a, sizeof a, "%s", trim(ea));
                        snprintf(b, sizeof b, "%s", trim(eb));
                        free(ea); free(eb);
                    }
                    free(inner);
                } else {
                    svec w; sv_init(&w);
                    char *e = expand(arg);
                    split_words(e, &w);
                    if (w.n > 0) snprintf(a, sizeof a, "%s", w.v[0]);
                    if (w.n > 1) snprintf(b, sizeof b, "%s", w.v[1]);
                    sv_free(&w);
                    free(e);
                }
                result = !strcmp(a, b);
            }
            if (neg) result = !result;

            cond_t *c = xmalloc(sizeof(cond_t));
            c->prev = conds;
            c->active = !skipping && result;
            c->taken = c->active;
            conds = c;
            skipping = !c->active || skipping;
            if (conds->prev && !conds->prev->active) skipping = 1;
            free(work);
            continue;
        }

        if (!strcmp(s, "else") || !strncmp(s, "else ", 5)) {
            if (conds) {
                int parent_ok = !conds->prev || conds->prev->active;
                conds->active = parent_ok && !conds->taken;
                if (conds->active) conds->taken = 1;
                skipping = !conds->active;
                if (!parent_ok) skipping = 1;
            }
            free(work);
            continue;
        }

        if (!strcmp(s, "endif")) {
            if (conds) {
                cond_t *c = conds;
                conds = c->prev;
                free(c);
            }
            skipping = conds ? !conds->active : 0;
            free(work);
            continue;
        }

        if (skipping) { free(work); continue; }

        if (!strncmp(s, "include", 7) && (s[7] == ' ' || s[7] == '\t')) {
            char *e = expand(s + 8);
            svec w; sv_init(&w);
            split_words(e, &w);
            for (int i = 0; i < w.n; i++) load_makefile(w.v[i], 0);
            sv_free(&w);
            free(e);
            free(work);
            continue;
        }
        if (!strncmp(s, "-include", 8) || !strncmp(s, "sinclude", 8)) {
            char *e = expand(s + 8);
            svec w; sv_init(&w);
            split_words(e, &w);
            for (int i = 0; i < w.n; i++) load_makefile(w.v[i], 0);
            sv_free(&w);
            free(e);
            free(work);
            continue;
        }
        if (!strncmp(s, "export", 6) && (s[6] == 0 || isspace((unsigned char)s[6]))) {
            s = trim(s + 6);
            if (!*s) { free(work); continue; }
        }
        if (!strncmp(s, "unexport", 8) && (s[8] == 0 || isspace((unsigned char)s[8]))) {
            free(work);
            continue;
        }

        char *eq = NULL;
        int simple = 0, append = 0, cond_assign = 0;
        {
            int depth = 0;
            for (char *p = s; *p; p++) {
                if (*p == '(' || *p == '{') depth++;
                else if (*p == ')' || *p == '}') depth--;
                else if (*p == ':' && depth == 0) {
                    if (p[1] == '=') { eq = p; simple = 1; break; }
                    break;
                } else if (*p == '=' && depth == 0) {
                    eq = p;
                    if (p > s && p[-1] == '+') { append = 1; }
                    if (p > s && p[-1] == '?') { cond_assign = 1; }
                    break;
                }
            }
        }

        if (eq) {
            char *name_end = eq;
            if (simple) { }
            else if (append || cond_assign) name_end = eq - 1;
            char *name_raw = xstrndup(s, (size_t)(name_end - s));
            char *name = trim(name_raw);
            char *valstart = eq + (simple ? 2 : 1);
            char *val = trim(valstart);

            char *ename = expand(name);
            char *tname = trim(ename);

            if (cond_assign) {
                if (!var_find(tname) && !getenv(tname)) var_set(tname, val, 0);
            } else if (append) {
                var_t *v = var_find(tname);
                if (!v) {
                    var_set(tname, val, 0);
                } else {
                    sbuf b;
                    sb_init(&b);
                    sb_puts(&b, v->value);
                    if (v->value[0]) sb_putc(&b, ' ');
                    if (v->simple) {
                        char *e = expand(val);
                        sb_puts(&b, e);
                        free(e);
                    } else sb_puts(&b, val);
                    char *merged = sb_take(&b);
                    var_set(tname, merged, v->simple);
                    free(merged);
                }
            } else if (simple) {
                char *e = expand(val);
                var_set(tname, e, 1);
                free(e);
            } else {
                var_set(tname, val, 0);
            }
            free(ename);
            free(name_raw);
            free(work);
            continue;
        }

        char *colon = NULL;
        {
            int depth = 0;
            for (char *p = s; *p; p++) {
                if (*p == '(' || *p == '{') depth++;
                else if (*p == ')' || *p == '}') depth--;
                else if (*p == ':' && depth == 0) { colon = p; break; }
            }
        }
        if (!colon) { free(work); continue; }

        int dcolon = (colon[1] == ':');
        *colon = 0;
        char *tpart = expand(s);
        char *ppart = expand(colon + (dcolon ? 2 : 1));

        rule_t *r = xmalloc(sizeof(rule_t));
        memset(r, 0, sizeof(*r));
        sv_init(&r->targets);
        sv_init(&r->prereqs);
        sv_init(&r->order_only);
        sv_init(&r->recipe);
        r->double_colon = dcolon;

        split_words(tpart, &r->targets);

        char *semi = strchr(ppart, ';');
        if (semi) {
            *semi = 0;
            char *cmd = trim(semi + 1);
            if (*cmd) sv_push(&r->recipe, xstrdup(cmd));
        }
        {
            char *bar = NULL;
            for (char *p = ppart; *p; p++)
                if (*p == '|' && (p == ppart || p[-1] == ' ' || p[-1] == '\t') &&
                    (p[1] == 0 || p[1] == ' ' || p[1] == '\t')) { bar = p; break; }
            if (bar) {
                *bar = 0;
                split_words(bar + 1, &r->order_only);
            }
            split_words(ppart, &r->prereqs);
        }

        for (int i = 0; i < r->targets.n; i++)
            if (strchr(r->targets.v[i], '%')) r->is_pattern = 1;

        int special = 0;
        for (int i = 0; i < r->targets.n; i++) {
            if (!strcmp(r->targets.v[i], ".PHONY")) {
                for (int k = 0; k < r->prereqs.n; k++)
                    sv_push(&g_phony, xstrdup(r->prereqs.v[k]));
                special = 1;
            } else if (!strcmp(r->targets.v[i], ".SUFFIXES") ||
                       !strcmp(r->targets.v[i], ".DEFAULT") ||
                       !strcmp(r->targets.v[i], ".SECONDARY") ||
                       !strcmp(r->targets.v[i], ".NOTPARALLEL")) {
                special = 1;
            }
        }

        if (special) {
            sv_free(&r->targets);
            sv_free(&r->prereqs);
            sv_free(&r->order_only);
            sv_free(&r->recipe);
            free(r);
            cur = NULL;
        } else {
            rule_add(r);
            cur = r;
            if (!g_default_goal && !r->is_pattern && r->targets.n &&
                r->targets.v[0][0] != '.')
                g_default_goal = xstrdup(r->targets.v[0]);
        }

        free(tpart);
        free(ppart);
        free(work);
    }

    while (conds) {
        cond_t *c = conds;
        conds = c->prev;
        free(c);
    }
}

static long file_mtime(const char *path, int *exists)
{
    struct stat st;
    if (stat(path, &st) != 0) { *exists = 0; return 0; }
    *exists = 1;
    return (long)st.st_mtime;
}

static int run_recipe_line(const char *cmdline, const char *target)
{
    const char *p = cmdline;
    int silent = g_silent, ignore = g_ignore_errors;
    for (;;) {
        if (*p == '@') { silent = 1; p++; }
        else if (*p == '-') { ignore = 1; p++; }
        else if (*p == '+') { p++; }
        else break;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 0;

    if (!silent || g_dry_run) printf("%s\n", p);
    fflush(stdout);
    if (g_dry_run) return 0;

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "make: cannot fork\n");
        return 1;
    }
    if (pid == 0) {
        char *argv[4] = { "/bin/sh", "-c", (char *)p, NULL };
        execve("/bin/sh", argv, environ);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    if (code && !ignore) {
        fprintf(stderr, "make: *** [%s] Error %d\n", target, code);
        return code;
    }
    return 0;
}

static int build(const char *target, int depth, int *updated);

static void set_auto_vars(const char *target, svec *prereqs, svec *newer, const char *stem)
{
    var_set("@", target, 1);
    var_set("<", prereqs->n ? prereqs->v[0] : "", 1);
    char *all = join_words(prereqs, " ");
    var_set("^", all, 1);
    free(all);
    char *nw = join_words(newer, " ");
    var_set("?", nw, 1);
    free(nw);
    var_set("*", stem ? stem : "", 1);
}

static int rule_matches_target(rule_t *r, const char *target, char *stem, size_t cap)
{
    for (int i = 0; i < r->targets.n; i++) {
        if (r->is_pattern) {
            if (pattern_match(r->targets.v[i], target, stem, cap)) return 1;
        } else if (!strcmp(r->targets.v[i], target)) {
            if (stem && cap) stem[0] = 0;
            return 1;
        }
    }
    return 0;
}

static int build(const char *target, int depth, int *updated)
{
    if (updated) *updated = 0;
    if (depth > MK_MAX_DEPTH) {
        fprintf(stderr, "make: %s: dependencies too deep\n", target);
        return 2;
    }

    int exists = 0;
    long tstamp = file_mtime(target, &exists);
    int phony = is_phony(target);

    svec prereqs, newer, recipe, order_only;
    sv_init(&prereqs);
    sv_init(&newer);
    sv_init(&recipe);
    sv_init(&order_only);
    char stem[512] = "";
    int have_rule = 0;

    for (rule_t *r = g_rules; r; r = r->next) {
        if (r->is_pattern) continue;
        if (!rule_matches_target(r, target, stem, sizeof stem)) continue;
        have_rule = 1;
        for (int i = 0; i < r->prereqs.n; i++) sv_push(&prereqs, xstrdup(r->prereqs.v[i]));
        for (int i = 0; i < r->order_only.n; i++) sv_push(&order_only, xstrdup(r->order_only.v[i]));
        if (r->recipe.n && recipe.n == 0)
            for (int i = 0; i < r->recipe.n; i++) sv_push(&recipe, xstrdup(r->recipe.v[i]));
    }

    if (recipe.n == 0) {
        for (rule_t *r = g_rules; r; r = r->next) {
            if (!r->is_pattern || r->recipe.n == 0) continue;
            char s2[512] = "";
            if (!rule_matches_target(r, target, s2, sizeof s2)) continue;

            svec cand;
            sv_init(&cand);
            int ok = 1;
            for (int i = 0; i < r->prereqs.n; i++) {
                char *p = pattern_subst(r->prereqs.v[i], s2);
                sv_push(&cand, p);
            }
            for (int i = 0; i < cand.n && ok; i++) {
                int ex = 0;
                file_mtime(cand.v[i], &ex);
                if (ex) continue;
                int found = 0;
                for (rule_t *q = g_rules; q && !found; q = q->next)
                    if (!q->is_pattern && rule_matches_target(q, cand.v[i], NULL, 0))
                        found = 1;
                if (!found) ok = 0;
            }
            if (!ok) { sv_free(&cand); continue; }

            snprintf(stem, sizeof stem, "%s", s2);
            for (int i = 0; i < cand.n; i++) sv_push(&prereqs, xstrdup(cand.v[i]));
            for (int i = 0; i < r->order_only.n; i++) {
                char *p = pattern_subst(r->order_only.v[i], s2);
                sv_push(&order_only, p);
            }
            for (int i = 0; i < r->recipe.n; i++) sv_push(&recipe, xstrdup(r->recipe.v[i]));
            sv_free(&cand);
            have_rule = 1;
            break;
        }
    }

    if (!have_rule && !exists) {
        fprintf(stderr, "make: *** No rule to make target '%s'.  Stop.\n", target);
        sv_free(&prereqs); sv_free(&newer); sv_free(&recipe); sv_free(&order_only);
        return 2;
    }

    for (int i = 0; i < order_only.n; i++) {
        int dummy = 0;
        int r = build(order_only.v[i], depth + 1, &dummy);
        if (r) {
            sv_free(&prereqs); sv_free(&newer); sv_free(&recipe); sv_free(&order_only);
            return r;
        }
    }

    int rc = 0;
    int need = (!exists && !phony) || phony || g_always_make;
    int any_child_updated = 0;

    for (int i = 0; i < prereqs.n; i++) {
        int child_updated = 0;
        int r = build(prereqs.v[i], depth + 1, &child_updated);
        if (r) {
            rc = r;
            if (!g_keep_going) break;
            g_failed = 1;
            continue;
        }
        if (child_updated) any_child_updated = 1;
        int pex = 0;
        long pstamp = file_mtime(prereqs.v[i], &pex);
        if (child_updated || !exists || (pex && pstamp > tstamp)) {
            sv_push(&newer, xstrdup(prereqs.v[i]));
            need = 1;
        }
    }

    if (rc && !g_keep_going) {
        sv_free(&prereqs); sv_free(&newer); sv_free(&recipe); sv_free(&order_only);
        return rc;
    }

    if (!need) {
        if (updated) *updated = any_child_updated;
        sv_free(&prereqs); sv_free(&newer); sv_free(&recipe); sv_free(&order_only);
        return rc;
    }

    if (recipe.n == 0) {
        if (!exists && !phony && !have_rule) {
            fprintf(stderr, "make: *** No rule to make target '%s'.  Stop.\n", target);
            rc = 2;
        }
        if (updated) *updated = any_child_updated;
        sv_free(&prereqs); sv_free(&newer); sv_free(&recipe); sv_free(&order_only);
        return rc;
    }

    if (g_question) {
        sv_free(&prereqs); sv_free(&newer); sv_free(&recipe); sv_free(&order_only);
        if (updated) *updated = 1;
        return 1;
    }

    set_auto_vars(target, &prereqs, &newer, stem);

    for (int i = 0; i < recipe.n; i++) {
        char *cmd = expand(recipe.v[i]);
        char *nl = cmd;
        while (nl) {
            char *next = strchr(nl, '\n');
            if (next) *next = 0;
            int r = run_recipe_line(nl, target);
            if (r) {
                rc = r;
                free(cmd);
                sv_free(&prereqs); sv_free(&newer); sv_free(&recipe); sv_free(&order_only);
                if (updated) *updated = 1;
                return rc;
            }
            nl = next ? next + 1 : NULL;
        }
        free(cmd);
    }

    if (updated) *updated = 1;
    sv_free(&prereqs); sv_free(&newer); sv_free(&recipe); sv_free(&order_only);
    return rc;
}

static void set_builtin_vars(void)
{
    var_set("CC", "gcc", 0);
    var_set("CXX", "g++", 0);
    var_set("AR", "ar", 0);
    var_set("AS", "as", 0);
    var_set("LD", "ld", 0);
    var_set("RM", "rm -f", 0);
    var_set("SHELL", "/bin/sh", 0);
    var_set("MAKE", "make", 0);
    var_set("CFLAGS", "", 0);
    var_set("CPPFLAGS", "", 0);
    var_set("LDFLAGS", "", 0);
    var_set("LDLIBS", "", 0);
    var_set("ARFLAGS", "rv", 0);
}

static void add_builtin_rules(void)
{
    static const char *const BUILTIN =
        "%.o: %.c\n"
        "\t$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<\n"
        "%.o: %.S\n"
        "\t$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<\n"
        "%.o: %.s\n"
        "\t$(AS) -o $@ $<\n"
        "%: %.c\n"
        "\t$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)\n"
        "%: %.o\n"
        "\t$(CC) $(LDFLAGS) -o $@ $< $(LDLIBS)\n";
    char *copy = xstrdup(BUILTIN);
    char *save_goal = g_default_goal;
    g_default_goal = NULL;
    parse_text(copy, "<builtin>");
    free(g_default_goal);
    g_default_goal = save_goal;
    free(copy);
}

static const char USAGE[] =
    "Usage: make [-f makefile] [-C dir] [options] [VAR=value] [target...]\n"
    "Build the targets a makefile describes, skipping what is already current.\n"
    "\n"
    "  -f FILE   read FILE instead of Makefile\n"
    "  -C DIR    change to DIR first\n"
    "  -n        print the recipes without running them\n"
    "  -s        run the recipes without printing them\n"
    "  -k        keep going past a target that fails\n"
    "  -i        ignore errors from every recipe line\n"
    "  -B        rebuild everything, whatever the timestamps say\n"
    "  -q        say nothing; exit 1 if a target needs remaking\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "make")) return 0;
    argc = cervus_end_of_options(argc, argv);

    const char *makefile = NULL;
    svec cmdline_vars;
    sv_init(&cmdline_vars);

    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || !a[1]) break;
        if (!strcmp(a, "-f") && i + 1 < argc) { makefile = argv[++i]; continue; }
        if (!strncmp(a, "-f", 2) && a[2]) { makefile = a + 2; continue; }
        if (!strcmp(a, "-C") && i + 1 < argc) {
            if (chdir(argv[++i]) != 0) {
                fprintf(stderr, "make: cannot enter %s\n", argv[i]);
                return 2;
            }
            continue;
        }
        int bad = 0;
        for (const char *p = a + 1; *p && !bad; p++) {
            switch (*p) {
                case 'n': g_dry_run = 1; break;
                case 's': g_silent = 1; break;
                case 'k': g_keep_going = 1; break;
                case 'i': g_ignore_errors = 1; break;
                case 'B': g_always_make = 1; break;
                case 'q': g_question = 1; break;
                case 'w': break;
                case 'r': break;
                default: bad = 1; break;
            }
        }
        if (bad) {
            fprintf(stderr, "make: unknown option %s\n", a);
            return 2;
        }
    }

    set_builtin_vars();

    for (; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq && eq != argv[i]) sv_push(&cmdline_vars, xstrdup(argv[i]));
        else sv_push(&g_goals, xstrdup(argv[i]));
    }

    if (makefile) {
        load_makefile(makefile, 1);
    } else {
        static const char *const NAMES[] = { "GNUmakefile", "makefile", "Makefile", NULL };
        int found = 0;
        for (int k = 0; NAMES[k] && !found; k++) {
            struct stat st;
            if (stat(NAMES[k], &st) == 0) { load_makefile(NAMES[k], 1); found = 1; }
        }
        if (!found) {
            fprintf(stderr, "make: *** No makefile found.  Stop.\n");
            return 2;
        }
    }

    add_builtin_rules();

    for (int k = 0; k < cmdline_vars.n; k++) {
        char *eq = strchr(cmdline_vars.v[k], '=');
        *eq = 0;
        char *val = expand(eq + 1);
        var_set(cmdline_vars.v[k], val, 1);
        free(val);
    }

    if (g_goals.n == 0) {
        if (!g_default_goal) {
            fprintf(stderr, "make: *** No targets.  Stop.\n");
            return 2;
        }
        sv_push(&g_goals, xstrdup(g_default_goal));
    }

    int rc = 0;
    for (int k = 0; k < g_goals.n; k++) {
        int upd = 0;
        int r = build(g_goals.v[k], 0, &upd);
        if (r) {
            rc = r;
            if (!g_keep_going) break;
        }
        if (!r && !upd && !g_question && !g_dry_run) {
            int ex = 0;
            file_mtime(g_goals.v[k], &ex);
            if (ex && !is_phony(g_goals.v[k]))
                printf("make: '%s' is up to date.\n", g_goals.v[k]);
            else
                printf("make: Nothing to be done for '%s'.\n", g_goals.v[k]);
        }
    }

    if (g_failed && !rc) rc = 2;
    return rc;
}

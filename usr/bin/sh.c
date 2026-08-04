#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>

extern char **environ;

static int   g_status = 0;
static int   g_argc;
static char **g_argv;
static int   g_exit_flag = 0;
static int   g_exit_code = 0;

static char *xstrdup(const char *s) { char *p = malloc(strlen(s) + 1); if (p) strcpy(p, s); return p; }
static void *xrealloc(void *p, size_t n) { void *r = realloc(p, n); if (!r) { fprintf(stderr, "sh: out of memory\n"); exit(1); } return r; }

typedef struct { char **v; int n, cap; } vec;
static void vec_init(vec *a) { a->v = 0; a->n = 0; a->cap = 0; }
static void vec_push(vec *a, char *s) { if (a->n >= a->cap) { a->cap = a->cap ? a->cap * 2 : 8; a->v = xrealloc(a->v, sizeof(char *) * a->cap); } a->v[a->n++] = s; }
static void vec_free(vec *a) { for (int i = 0; i < a->n; i++) free(a->v[i]); free(a->v); vec_init(a); }

typedef struct var { char *name; char *val; int exported; struct var *next; } var;
static var *g_vars;

static char *var_get(const char *name) {
    for (var *v = g_vars; v; v = v->next) if (!strcmp(v->name, name)) return v->val;
    return getenv(name);
}
static void var_set(const char *name, const char *val, int export) {
    for (var *v = g_vars; v; v = v->next) if (!strcmp(v->name, name)) {
        free(v->val); v->val = xstrdup(val);
        if (export) { v->exported = 1; setenv(name, val, 1); }
        else if (v->exported) setenv(name, val, 1);
        return;
    }
    var *v = malloc(sizeof *v);
    v->name = xstrdup(name); v->val = xstrdup(val); v->exported = export;
    v->next = g_vars; g_vars = v;
    if (export) setenv(name, val, 1);
}
static void var_unset(const char *name) {
    var **pp = &g_vars;
    while (*pp) { if (!strcmp((*pp)->name, name)) { var *d = *pp; *pp = d->next; free(d->name); free(d->val); free(d); break; } pp = &(*pp)->next; }
    unsetenv(name);
}

enum { T_WORD, T_PIPE, T_AND, T_OR, T_SEMI, T_DSEMI, T_AMP, T_LT, T_GT, T_DGT, T_DLT, T_LP, T_RP, T_NL, T_EOF };

typedef struct { int type; char *text; } tok;
static tok  *g_toks;
static int   g_ntok, g_tokcap, g_tp;

static void tok_add(int type, const char *text) {
    if (g_ntok >= g_tokcap) { g_tokcap = g_tokcap ? g_tokcap * 2 : 64; g_toks = xrealloc(g_toks, sizeof(tok) * g_tokcap); }
    g_toks[g_ntok].type = type;
    g_toks[g_ntok].text = text ? xstrdup(text) : 0;
    g_ntok++;
}

static int is_op_char(char c) { return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')' || c == '\n'; }

static void lex(const char *s) {
    g_ntok = 0; g_tp = 0;
    while (*s) {
        if (*s == ' ' || *s == '\t') { s++; continue; }
        if (*s == '#') { while (*s && *s != '\n') s++; continue; }
        if (*s == '\n') { tok_add(T_NL, 0); s++; continue; }
        if (*s == '\\' && s[1] == '\n') { s += 2; continue; }
        if (*s == '|') { if (s[1] == '|') { tok_add(T_OR, 0); s += 2; } else { tok_add(T_PIPE, 0); s++; } continue; }
        if (*s == '&') { if (s[1] == '&') { tok_add(T_AND, 0); s += 2; } else { tok_add(T_AMP, 0); s++; } continue; }
        if (*s == ';') { if (s[1] == ';') { tok_add(T_DSEMI, 0); s += 2; } else { tok_add(T_SEMI, 0); s++; } continue; }
        if (*s == '(') { tok_add(T_LP, 0); s++; continue; }
        if (*s == ')') { tok_add(T_RP, 0); s++; continue; }
        if (*s == '<') { if (s[1] == '<') { tok_add(T_DLT, 0); s += 2; } else { tok_add(T_LT, 0); s++; } if (*s == '&') { char b[8]; int k = 0; b[k++] = *s++; while (((*s >= '0' && *s <= '9') || *s == '-') && k < 7) b[k++] = *s++; b[k] = 0; tok_add(T_WORD, b); } continue; }
        if (*s == '>') { if (s[1] == '>') { tok_add(T_DGT, 0); s += 2; } else { tok_add(T_GT, 0); s++; } if (*s == '&') { char b[8]; int k = 0; b[k++] = *s++; while (((*s >= '0' && *s <= '9') || *s == '-') && k < 7) b[k++] = *s++; b[k] = 0; tok_add(T_WORD, b); } continue; }

        char buf[8192]; int bi = 0;
        int had_redir_fd = 0;
        while (*s && *s != ' ' && *s != '\t' && !(*s == '\n')) {
            if (*s == '\'') { buf[bi++] = *s++; while (*s && *s != '\'') buf[bi++] = *s++; if (*s) buf[bi++] = *s++; continue; }
            if (*s == '"') { buf[bi++] = *s++; while (*s && *s != '"') { if (*s == '\\' && s[1]) buf[bi++] = *s++; buf[bi++] = *s++; } if (*s) buf[bi++] = *s++; continue; }
            if (*s == '\\') { buf[bi++] = *s++; if (*s) buf[bi++] = *s++; continue; }
            if (*s == '$' && s[1] == '(') { buf[bi++] = *s++; int depth = 0; do { if (*s == '(') depth++; else if (*s == ')') depth--; buf[bi++] = *s++; } while (*s && depth > 0); continue; }
            if (*s == '`') { buf[bi++] = *s++; while (*s && *s != '`') buf[bi++] = *s++; if (*s) buf[bi++] = *s++; continue; }
            if (is_op_char(*s)) {
                if ((*s == '<' || *s == '>') && bi > 0) {
                    int alldig = 1; for (int k = 0; k < bi; k++) if (buf[k] < '0' || buf[k] > '9') alldig = 0;
                    if (alldig) { had_redir_fd = 1; break; }
                }
                break;
            }
            buf[bi++] = *s++;
        }
        buf[bi] = 0;
        if (bi > 0) tok_add(T_WORD, buf);
        (void)had_redir_fd;
    }
    tok_add(T_EOF, 0);
}

static int cur(void) { return g_toks[g_tp].type; }
static char *curtext(void) { return g_toks[g_tp].text; }
static void adv(void) { if (g_tp < g_ntok - 1) g_tp++; }
static void skip_nl(void) { while (cur() == T_NL || cur() == T_SEMI) adv(); }

enum { N_CMD, N_PIPE, N_AND, N_OR, N_SEQ, N_BG, N_IF, N_FOR, N_WHILE, N_UNTIL, N_CASE, N_GROUP, N_SUBSHELL, N_FUNC, N_NOT };

typedef struct redir { int type; int fd; char *target; int dupfd; struct redir *next; } redir;

typedef struct node {
    int type;
    vec words;
    vec assigns;
    redir *redirs;
    struct node *left, *right, *body, *body2;
    char *var;
    vec casewords;
    struct caseitem *cases;
    char *fname;
} node;

typedef struct caseitem { vec pats; node *body; struct caseitem *next; } caseitem;

static node *parse_list(void);
static node *parse_andor(void);

static node *newnode(int t) { node *n = calloc(1, sizeof *n); n->type = t; vec_init(&n->words); vec_init(&n->assigns); vec_init(&n->casewords); return n; }

static int is_reserved(const char *w) {
    static const char *r[] = { "if","then","elif","else","fi","for","while","until","do","done","case","esac","in","{","}","!", 0 };
    for (int i = 0; r[i]; i++) if (!strcmp(w, r[i])) return 1;
    return 0;
}
static int word_is(const char *w) { return cur() == T_WORD && curtext() && !strcmp(curtext(), w); }

static void add_redir(node *n, int type, int fd, const char *target, int dupfd) {
    redir *r = calloc(1, sizeof *r);
    r->type = type; r->fd = fd; r->target = target ? xstrdup(target) : 0; r->dupfd = dupfd;
    redir **pp = &n->redirs; while (*pp) pp = &(*pp)->next; *pp = r;
}

static int parse_redir(node *n) {
    int fd = -1;
    if (cur() == T_WORD && curtext()) {
        const char *w = curtext(); int alldig = *w != 0;
        for (const char *p = w; *p; p++) if (*p < '0' || *p > '9') alldig = 0;
        if (alldig && (g_toks[g_tp + 1].type == T_GT || g_toks[g_tp + 1].type == T_DGT || g_toks[g_tp + 1].type == T_LT)) { fd = atoi(w); adv(); }
    }
    int t = cur();
    if (t == T_GT || t == T_DGT || t == T_LT || t == T_DLT) {
        adv();
        char *tgt = (cur() == T_WORD) ? curtext() : "";
        if (t == T_GT) {
            if (tgt[0] == '&') add_redir(n, 4, fd < 0 ? 1 : fd, 0, atoi(tgt + 1));
            else add_redir(n, 1, fd < 0 ? 1 : fd, tgt, 0);
        } else if (t == T_DGT) add_redir(n, 2, fd < 0 ? 1 : fd, tgt, 0);
        else if (t == T_LT) add_redir(n, 0, fd < 0 ? 0 : fd, tgt, 0);
        else if (t == T_DLT) add_redir(n, 3, fd < 0 ? 0 : fd, tgt, 0);
        if (cur() == T_WORD) adv();
        return 1;
    }
    return 0;
}

static node *parse_simple(void) {
    node *n = newnode(N_CMD);
    int allow_assign = 1;
    for (;;) {
        if (cur() == T_LT || cur() == T_GT || cur() == T_DGT || cur() == T_DLT) { parse_redir(n); continue; }
        if (cur() != T_WORD) break;
        char *w = curtext();
        int alldig = *w != 0; for (char *p = w; *p; p++) if (*p < '0' || *p > '9') alldig = 0;
        if (alldig && (g_toks[g_tp + 1].type == T_GT || g_toks[g_tp + 1].type == T_DGT || g_toks[g_tp + 1].type == T_LT)) { parse_redir(n); continue; }
        if (allow_assign && strchr(w, '=') && w[0] != '=') {
            const char *eq = strchr(w, '='); int isname = 1;
            for (const char *p = w; p < eq; p++) if (!(*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) isname = 0;
            if (isname && !(w[0] >= '0' && w[0] <= '9')) { vec_push(&n->assigns, xstrdup(w)); adv(); continue; }
        }
        allow_assign = 0;
        vec_push(&n->words, xstrdup(w)); adv();
    }
    return n;
}

static node *parse_command(void);

static node *parse_brace_group(void) {
    adv();
    node *n = newnode(N_GROUP);
    n->body = parse_list();
    if (word_is("}")) adv();
    return n;
}
static node *parse_subshell(void) {
    adv();
    node *n = newnode(N_SUBSHELL);
    n->body = parse_list();
    if (cur() == T_RP) adv();
    return n;
}

static node *parse_if(void) {
    adv();
    node *n = newnode(N_IF);
    n->left = parse_list();
    if (word_is("then")) adv();
    n->body = parse_list();
    if (word_is("elif")) { n->body2 = parse_if(); return n; }
    if (word_is("else")) { adv(); n->body2 = parse_list(); }
    if (word_is("fi")) adv();
    return n;
}

static node *parse_for(void) {
    adv();
    node *n = newnode(N_FOR);
    if (cur() == T_WORD) { n->var = xstrdup(curtext()); adv(); }
    if (word_is("in")) { adv(); while (cur() == T_WORD && !is_reserved(curtext())) { vec_push(&n->words, xstrdup(curtext())); adv(); } }
    else { vec_push(&n->words, xstrdup("\"$@\"")); }
    skip_nl();
    if (word_is("do")) adv();
    n->body = parse_list();
    if (word_is("done")) adv();
    return n;
}

static node *parse_while(int until) {
    adv();
    node *n = newnode(until ? N_UNTIL : N_WHILE);
    n->left = parse_list();
    if (word_is("do")) adv();
    n->body = parse_list();
    if (word_is("done")) adv();
    return n;
}

static node *parse_case(void) {
    adv();
    node *n = newnode(N_CASE);
    if (cur() == T_WORD) { vec_push(&n->casewords, xstrdup(curtext())); adv(); }
    if (word_is("in")) adv();
    skip_nl();
    caseitem **pp = &n->cases;
    while (cur() != T_EOF && !word_is("esac")) {
        caseitem *ci = calloc(1, sizeof *ci); vec_init(&ci->pats);
        if (cur() == T_LP) adv();
        while (cur() == T_WORD) { vec_push(&ci->pats, xstrdup(curtext())); adv(); if (cur() == T_PIPE) { adv(); continue; } break; }
        if (cur() == T_RP) adv();
        ci->body = parse_list();
        if (cur() == T_DSEMI) adv();
        skip_nl();
        *pp = ci; pp = &ci->next;
    }
    if (word_is("esac")) adv();
    return n;
}

static node *parse_command(void) {
    if (cur() == T_WORD && curtext()) {
        char *w = curtext();
        if (!strcmp(w, "if")) return parse_if();
        if (!strcmp(w, "for")) return parse_for();
        if (!strcmp(w, "while")) return parse_while(0);
        if (!strcmp(w, "until")) return parse_while(1);
        if (!strcmp(w, "case")) return parse_case();
        if (!strcmp(w, "{")) return parse_brace_group();
        if (!strcmp(w, "!")) { adv(); node *n = newnode(N_NOT); n->body = parse_command(); return n; }
        if (g_toks[g_tp + 1].type == T_LP) {
            node *n = newnode(N_FUNC); n->fname = xstrdup(w); adv(); adv();
            if (cur() == T_RP) adv();
            skip_nl();
            n->body = parse_command();
            return n;
        }
    }
    if (cur() == T_LP) return parse_subshell();
    return parse_simple();
}

static node *parse_pipeline(void) {
    node *n = parse_command();
    while (cur() == T_PIPE) {
        adv(); skip_nl();
        node *p = newnode(N_PIPE); p->left = n; p->right = parse_command(); n = p;
    }
    return n;
}

static node *parse_andor(void) {
    node *n = parse_pipeline();
    for (;;) {
        if (cur() == T_AND) { adv(); skip_nl(); node *a = newnode(N_AND); a->left = n; a->right = parse_pipeline(); n = a; }
        else if (cur() == T_OR) { adv(); skip_nl(); node *a = newnode(N_OR); a->left = n; a->right = parse_pipeline(); n = a; }
        else break;
    }
    return n;
}

static int at_list_end(void) {
    if (cur() == T_EOF || cur() == T_RP || cur() == T_DSEMI) return 1;
    if (cur() == T_WORD && curtext()) { const char *w = curtext();
        if (!strcmp(w, "then") || !strcmp(w, "else") || !strcmp(w, "elif") || !strcmp(w, "fi") ||
            !strcmp(w, "do") || !strcmp(w, "done") || !strcmp(w, "esac") || !strcmp(w, "}")) return 1; }
    return 0;
}

static node *parse_list(void) {
    skip_nl();
    if (at_list_end()) return 0;
    node *n = parse_andor();
    for (;;) {
        if (cur() == T_AMP) { adv(); node *b = newnode(N_BG); b->left = n; n = b; }
        if (cur() == T_SEMI || cur() == T_NL) {
            while (cur() == T_SEMI || cur() == T_NL) adv();
            if (at_list_end()) break;
            node *s = newnode(N_SEQ); s->left = n; s->right = parse_andor(); n = s; continue;
        }
        break;
    }
    return n;
}

static void expand_into(const char *raw, vec *out, int split);
static int exec_node(node *n);
static int fnmatch_simple(const char *pat, const char *str);

static int glob_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void sh_glob(const char *pat, vec *out) {
    const char *slash = strrchr(pat, '/');
    char dir[1024]; const char *base;
    if (slash) { int n = (int)(slash - pat); if (n == 0) { dir[0] = '/'; dir[1] = 0; } else { if (n > 1023) n = 1023; memcpy(dir, pat, n); dir[n] = 0; } base = slash + 1; }
    else { dir[0] = '.'; dir[1] = 0; base = pat; }
    DIR *d = opendir(dir);
    if (!d) { vec_push(out, xstrdup(pat)); return; }
    vec m; vec_init(&m);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (e->d_name[0] == '.' && base[0] != '.') continue;
        if (fnmatch_simple(base, e->d_name)) {
            char full[2048];
            if (slash) { if (dir[0] == '/' && dir[1] == 0) snprintf(full, sizeof full, "/%s", e->d_name); else snprintf(full, sizeof full, "%s/%s", dir, e->d_name); }
            else snprintf(full, sizeof full, "%s", e->d_name);
            vec_push(&m, xstrdup(full));
        }
    }
    closedir(d);
    if (m.n == 0) { vec_push(out, xstrdup(pat)); vec_free(&m); return; }
    qsort(m.v, m.n, sizeof(char *), glob_cmp);
    for (int i = 0; i < m.n; i++) vec_push(out, m.v[i]);
    free(m.v);
}

static char *run_capture(const char *cmd) {
    int pfd[2]; if (pipe(pfd) < 0) return xstrdup("");
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) { close(pfd[0]); dup2(pfd[1], 1); close(pfd[1]);
        lex(cmd); g_tp = 0; node *n = parse_list(); if (n) exec_node(n); exit(g_status); }
    close(pfd[1]);
    size_t cap = 1024, len = 0; char *buf = malloc(cap);
    for (;;) { if (len + 256 > cap) { cap *= 2; buf = xrealloc(buf, cap); } long r = read(pfd[0], buf + len, cap - len); if (r <= 0) break; len += r; }
    close(pfd[0]); int st; waitpid(pid, &st, 0);
    while (len > 0 && buf[len - 1] == '\n') len--;
    buf[len] = 0; return buf;
}

static long arith(const char *e);

typedef struct { char *b; char *q; int len, cap; } obuf;
static void ob_init(obuf *o) { o->cap = 64; o->b = malloc(o->cap); o->q = malloc(o->cap); o->len = 0; o->b[0] = 0; }
static void ob_ch(obuf *o, char c, int quoted) {
    if (o->len + 2 > o->cap) { o->cap *= 2; o->b = xrealloc(o->b, o->cap); o->q = xrealloc(o->q, o->cap); }
    o->b[o->len] = c; o->q[o->len] = (char)quoted; o->len++; o->b[o->len] = 0;
}
static void ob_str(obuf *o, const char *s, int quoted) { while (*s) ob_ch(o, *s++, quoted); }

static void expand_dollar(const char **pp, obuf *o, int quoted) {
    const char *p = *pp; p++;
    char tmp[64];
    if (*p == '{') {
        p++; char name[128]; int ni = 0;
        while (*p && (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) && ni < 127) name[ni++] = *p++;
        name[ni] = 0;
        char op = 0;
        if (*p == ':' && (p[1] == '-' || p[1] == '+' || p[1] == '=')) { op = p[1]; p += 2; }
        else if (*p == '-' || *p == '+' || *p == '=') { op = *p; p++; }
        char word[512]; int wi = 0;
        while (*p && *p != '}' && wi < 511) word[wi++] = *p++;
        word[wi] = 0; if (*p == '}') p++;
        const char *val = var_get(name);
        int empty = (!val || !*val);
        if (op == '-') { if (empty) val = word; }
        else if (op == '+') { val = empty ? "" : word; }
        else if (op == '=') { if (empty) { var_set(name, word, 0); val = var_get(name); } }
        if (!val) val = "";
        ob_str(o, val, quoted); *pp = p; return;
    }
    if (*p == '(') {
        if (p[1] == '(') {
            const char *q = p + 2; int depth = 2; char ex[512]; int ei = 0;
            while (*q && depth > 0 && ei < 511) { if (*q == '(') depth++; else if (*q == ')') { depth--; if (depth == 0) break; } ex[ei++] = *q++; }
            ex[ei] = 0; if (*q == ')') q++; if (*q == ')') q++;
            vec ev; vec_init(&ev); expand_into(ex, &ev, 0);
            snprintf(tmp, sizeof tmp, "%ld", arith(ev.n ? ev.v[0] : ex)); vec_free(&ev);
            ob_str(o, tmp, quoted); *pp = q; return;
        }
        const char *q = p + 1; int depth = 1; char cmd[8192]; int ci = 0;
        while (*q && depth > 0 && ci < 8191) { if (*q == '(') depth++; else if (*q == ')') { depth--; if (depth == 0) break; } cmd[ci++] = *q++; }
        cmd[ci] = 0; if (*q == ')') q++;
        char *out = run_capture(cmd); ob_str(o, out, quoted); free(out); *pp = q; return;
    }
    if (*p == '?') { snprintf(tmp, sizeof tmp, "%d", g_status); ob_str(o, tmp, quoted); *pp = p + 1; return; }
    if (*p == '#') { snprintf(tmp, sizeof tmp, "%d", g_argc > 0 ? g_argc - 1 : 0); ob_str(o, tmp, quoted); *pp = p + 1; return; }
    if (*p == '$') { snprintf(tmp, sizeof tmp, "%d", (int)getpid()); ob_str(o, tmp, quoted); *pp = p + 1; return; }
    if (*p == '@' || *p == '*') {
        for (int i = 1; i < g_argc; i++) { if (i > 1) ob_ch(o, ' ', 0); ob_str(o, g_argv[i], quoted); }
        *pp = p + 1; return;
    }
    if (*p >= '0' && *p <= '9') { int idx = *p - '0'; const char *val = (idx < g_argc) ? g_argv[idx] : ""; ob_str(o, val, quoted); *pp = p + 1; return; }
    if (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
        char name[128]; int ni = 0;
        while (*p && (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) && ni < 127) name[ni++] = *p++;
        name[ni] = 0; const char *val = var_get(name); if (!val) val = "";
        ob_str(o, val, quoted); *pp = p; return;
    }
    ob_ch(o, '$', quoted); *pp = p; return;
}

static void expand_into(const char *raw, vec *out, int split) {
    obuf o; ob_init(&o);
    const char *p = raw;
    int sq = 0, dq = 0;
    while (*p) {
        if (!sq && *p == '"') { dq = !dq; p++; continue; }
        if (!dq && *p == '\'') { sq = !sq; p++; continue; }
        if (!sq && *p == '\\' && p[1]) {
            p++;
            if (dq && !(*p == '$' || *p == '`' || *p == '"' || *p == '\\')) ob_ch(&o, '\\', 1);
            ob_ch(&o, *p++, 1); continue;
        }
        if (sq) { ob_ch(&o, *p++, 1); continue; }
        if (*p == '`') { p++; char cmd[8192]; int ci = 0; while (*p && *p != '`' && ci < 8191) cmd[ci++] = *p++; cmd[ci] = 0; if (*p) p++; char *co = run_capture(cmd); ob_str(&o, co, dq); free(co); continue; }
        if (*p == '$') { expand_dollar(&p, &o, dq); continue; }
        ob_ch(&o, *p++, dq);
    }

    if (!split) { vec_push(out, xstrdup(o.b)); free(o.b); free(o.q); return; }

    int i = 0;
    while (i < o.len) {
        while (i < o.len && !o.q[i] && (o.b[i] == ' ' || o.b[i] == '\t' || o.b[i] == '\n')) i++;
        if (i >= o.len) break;
        char field[8192]; int fi = 0; int has_glob = 0;
        while (i < o.len && !(!o.q[i] && (o.b[i] == ' ' || o.b[i] == '\t' || o.b[i] == '\n'))) {
            if (!o.q[i] && (o.b[i] == '*' || o.b[i] == '?' || o.b[i] == '[')) has_glob = 1;
            if (fi < 8190) field[fi++] = o.b[i];
            i++;
        }
        field[fi] = 0;
        if (has_glob) { sh_glob(field, out); continue; }
        vec_push(out, xstrdup(field));
    }
    free(o.b); free(o.q);
}

static long arith_expr(const char **s);
static long arith_prim(const char **s) {
    while (**s == ' ') (*s)++;
    if (**s == '(') { (*s)++; long v = arith_expr(s); while (**s == ' ') (*s)++; if (**s == ')') (*s)++; return v; }
    if (**s == '-') { (*s)++; return -arith_prim(s); }
    if (**s >= '0' && **s <= '9') { long v = 0; while (**s >= '0' && **s <= '9') v = v * 10 + (*(*s)++ - '0'); return v; }
    char name[128]; int ni = 0;
    while ((**s == '_' || (**s >= 'a' && **s <= 'z') || (**s >= 'A' && **s <= 'Z') || (**s >= '0' && **s <= '9')) && ni < 127) name[ni++] = *(*s)++;
    name[ni] = 0; const char *v = var_get(name); return v ? atol(v) : 0;
}
static long arith_mul(const char **s) { long v = arith_prim(s); for (;;) { while (**s == ' ') (*s)++; char o = **s; if (o == '*' || o == '/' || o == '%') { (*s)++; long r = arith_prim(s); if (o == '*') v *= r; else if (o == '/') v = r ? v / r : 0; else v = r ? v % r : 0; } else break; } return v; }
static long arith_expr(const char **s) { long v = arith_mul(s); for (;;) { while (**s == ' ') (*s)++; char o = **s; if (o == '+' || o == '-') { (*s)++; long r = arith_mul(s); if (o == '+') v += r; else v -= r; } else break; } return v; }
static long arith(const char *e) { const char *s = e; return arith_expr(&s); }

static int fnmatch_simple(const char *pat, const char *str) {
    if (*pat == 0) return *str == 0;
    if (*pat == '*') { if (fnmatch_simple(pat + 1, str)) return 1; if (*str && fnmatch_simple(pat, str + 1)) return 1; return 0; }
    if (*pat == '?') return *str ? fnmatch_simple(pat + 1, str + 1) : 0;
    if (*pat == '[') { const char *p = pat + 1; int neg = 0, m = 0; if (*p == '!') { neg = 1; p++; } while (*p && *p != ']') { if (p[1] == '-' && p[2] != ']') { if (*str >= p[0] && *str <= p[2]) m = 1; p += 3; } else { if (*str == *p) m = 1; p++; } } if (*p == ']') p++; if (m != neg && *str) return fnmatch_simple(p, str + 1); return 0; }
    if (*pat == *str && *str) return fnmatch_simple(pat + 1, str + 1);
    return 0;
}

typedef struct func { char *name; node *body; struct func *next; } func;
static func *g_funcs;
static node *func_find(const char *name) { for (func *f = g_funcs; f; f = f->next) if (!strcmp(f->name, name)) return f->body; return 0; }

static int run_builtin(char **argv, int argc, int *is_builtin);

static void apply_redirs(redir *r) {
    for (; r; r = r->next) {
        int fd;
        if (r->type == 4) { dup2(r->dupfd, r->fd); continue; }
        vec ex; vec_init(&ex); expand_into(r->target, &ex, 0);
        const char *tgt = ex.n > 0 ? ex.v[0] : "";
        if (r->type == 1) fd = open(tgt, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        else if (r->type == 2) fd = open(tgt, O_WRONLY | O_CREAT | O_APPEND, 0644);
        else if (r->type == 0) fd = open(tgt, O_RDONLY);
        else fd = -1;
        if (fd >= 0) { dup2(fd, r->fd); if (fd != r->fd) close(fd); }
        vec_free(&ex);
    }
}

static int exec_simple(node *n, int forked) {
    vec args; vec_init(&args);
    for (int i = 0; i < n->words.n; i++) expand_into(n->words.v[i], &args, 1);

    if (args.n == 0) {
        for (int i = 0; i < n->assigns.n; i++) { char *a = n->assigns.v[i]; char *eq = strchr(a, '='); *eq = 0; vec val; vec_init(&val); expand_into(eq + 1, &val, 0); var_set(a, val.n ? val.v[0] : "", 0); vec_free(&val); *eq = '='; }
        vec_free(&args);
        return 0;
    }
    vec_push(&args, 0);

    node *fb = func_find(args.v[0]);
    if (fb) {
        int save_argc = g_argc; char **save_argv = g_argv;
        vec fa; vec_init(&fa); vec_push(&fa, xstrdup(g_argv ? g_argv[0] : "sh"));
        for (int i = 1; i < args.n - 1; i++) vec_push(&fa, xstrdup(args.v[i]));
        g_argc = fa.n; g_argv = fa.v;
        int r = exec_node(fb);
        g_argc = save_argc; g_argv = save_argv; vec_free(&fa);
        vec_free(&args);
        return r;
    }

    int is_b = 0;
    if (!forked) {
        int save[3] = { -1, -1, -1 };
        if (n->redirs) { save[0] = dup(0); save[1] = dup(1); save[2] = dup(2); apply_redirs(n->redirs); }
        for (int i = 0; i < n->assigns.n; i++) { char *a = xstrdup(n->assigns.v[i]); char *eq = strchr(a, '='); *eq = 0; vec val; vec_init(&val); expand_into(eq + 1, &val, 0); var_set(a, val.n ? val.v[0] : "", 1); vec_free(&val); free(a); }
        int rc = run_builtin(args.v, args.n - 1, &is_b);
        if (n->redirs) { dup2(save[0], 0); dup2(save[1], 1); dup2(save[2], 2); close(save[0]); close(save[1]); close(save[2]); }
        if (is_b) { vec_free(&args); return rc; }
    }

    if (!forked) fflush(NULL);
    pid_t pid = forked ? 0 : fork();
    if (pid == 0) {
        for (int i = 0; i < n->assigns.n; i++) { char *a = xstrdup(n->assigns.v[i]); char *eq = strchr(a, '='); *eq = 0; vec val; vec_init(&val); expand_into(eq + 1, &val, 0); setenv(a, val.n ? val.v[0] : "", 1); vec_free(&val); free(a); }
        apply_redirs(n->redirs);
        execvp(args.v[0], args.v);
        fprintf(stderr, "sh: %s: not found\n", args.v[0]);
        _exit(127);
    }
    int st = 0; waitpid(pid, &st, 0);
    vec_free(&args);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
}

static int exec_node(node *n) {
    if (!n || g_exit_flag) return g_status;
    switch (n->type) {
        case N_CMD: return g_status = exec_simple(n, 0);
        case N_SEQ: exec_node(n->left); return exec_node(n->right);
        case N_AND: { int r = exec_node(n->left); if (r == 0) return exec_node(n->right); return g_status = r; }
        case N_OR: { int r = exec_node(n->left); if (r != 0) return exec_node(n->right); return g_status = r; }
        case N_NOT: { int r = exec_node(n->body); return g_status = (r == 0) ? 1 : 0; }
        case N_GROUP: return exec_node(n->body);
        case N_SUBSHELL: { fflush(NULL); pid_t p = fork(); if (p == 0) { exec_node(n->body); exit(g_status); } int st; waitpid(p, &st, 0); return g_status = WIFEXITED(st) ? WEXITSTATUS(st) : 1; }
        case N_BG: { fflush(NULL); pid_t p = fork(); if (p == 0) { exec_node(n->left); exit(g_status); } printf("[bg] %d\n", (int)p); return g_status = 0; }
        case N_PIPE: {
            int pfd[2]; pipe(pfd);
            fflush(NULL);
            pid_t l = fork();
            if (l == 0) { dup2(pfd[1], 1); close(pfd[0]); close(pfd[1]); if (n->left->type == N_CMD) exit(exec_simple(n->left, 1)); exit(exec_node(n->left)); }
            pid_t r = fork();
            if (r == 0) { dup2(pfd[0], 0); close(pfd[0]); close(pfd[1]); if (n->right->type == N_CMD) exit(exec_simple(n->right, 1)); exit(exec_node(n->right)); }
            close(pfd[0]); close(pfd[1]);
            int st; waitpid(l, &st, 0); waitpid(r, &st, 0);
            return g_status = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
        }
        case N_IF: { int c = exec_node(n->left); if (c == 0) return exec_node(n->body); else if (n->body2) return exec_node(n->body2); return g_status = 0; }
        case N_WHILE: { while (!g_exit_flag) { if (exec_node(n->left) != 0) break; exec_node(n->body); } return g_status; }
        case N_UNTIL: { while (!g_exit_flag) { if (exec_node(n->left) == 0) break; exec_node(n->body); } return g_status; }
        case N_FOR: {
            vec words; vec_init(&words);
            for (int i = 0; i < n->words.n; i++) expand_into(n->words.v[i], &words, 1);
            for (int i = 0; i < words.n && !g_exit_flag; i++) { var_set(n->var, words.v[i], 0); exec_node(n->body); }
            vec_free(&words); return g_status;
        }
        case N_CASE: {
            vec w; vec_init(&w); expand_into(n->casewords.v[0], &w, 0);
            const char *subject = w.n ? w.v[0] : "";
            for (caseitem *ci = n->cases; ci; ci = ci->next) {
                for (int i = 0; i < ci->pats.n; i++) { vec pv; vec_init(&pv); expand_into(ci->pats.v[i], &pv, 0); int m = fnmatch_simple(pv.n ? pv.v[0] : "", subject); vec_free(&pv); if (m) { exec_node(ci->body); vec_free(&w); return g_status; } }
            }
            vec_free(&w); return g_status = 0;
        }
        case N_FUNC: { func *f = malloc(sizeof *f); f->name = xstrdup(n->fname); f->body = n->body; f->next = g_funcs; g_funcs = f; return g_status = 0; }
    }
    return g_status;
}

static int builtin_test(char **a, int argc);

static int run_builtin(char **argv, int argc, int *is_builtin) {
    *is_builtin = 1;
    const char *c = argv[0];
    if (!strcmp(c, ":")) return 0;
    if (!strcmp(c, "true")) return 0;
    if (!strcmp(c, "false")) return 1;
    if (!strcmp(c, "cd")) { const char *d = argc > 1 ? argv[1] : var_get("HOME"); if (!d) d = "/"; if (chdir(d) < 0) { fprintf(stderr, "cd: %s: cannot\n", d); return 1; } char cwd[1024]; if (getcwd(cwd, sizeof cwd)) var_set("PWD", cwd, 1); return 0; }
    if (!strcmp(c, "pwd")) { char cwd[1024]; if (getcwd(cwd, sizeof cwd)) printf("%s\n", cwd); return 0; }
    if (!strcmp(c, "exit")) { g_exit_flag = 1; g_exit_code = argc > 1 ? atoi(argv[1]) : g_status; return g_exit_code; }
    if (!strcmp(c, "return")) { g_exit_flag = 0; return argc > 1 ? atoi(argv[1]) : g_status; }
    if (!strcmp(c, "export")) { for (int i = 1; i < argc; i++) { char *eq = strchr(argv[i], '='); if (eq) { *eq = 0; var_set(argv[i], eq + 1, 1); *eq = '='; } else { const char *v = var_get(argv[i]); var_set(argv[i], v ? v : "", 1); } } return 0; }
    if (!strcmp(c, "unset")) { for (int i = 1; i < argc; i++) var_unset(argv[i]); return 0; }
    if (!strcmp(c, "set")) { return 0; }
    if (!strcmp(c, "shift")) { int k = argc > 1 ? atoi(argv[1]) : 1; for (int i = 1; i + k < g_argc; i++) g_argv[i] = g_argv[i + k]; g_argc -= k; if (g_argc < 1) g_argc = 1; return 0; }
    if (!strcmp(c, "read")) { char line[4096]; int ln = 0; char ch; while (read(0, &ch, 1) == 1 && ch != '\n') { if (ln < 4095) line[ln++] = ch; } line[ln] = 0; if (ln == 0 && ch != '\n') { var_set(argc > 1 ? argv[1] : "REPLY", "", 0); return 1; } var_set(argc > 1 ? argv[1] : "REPLY", line, 0); return 0; }
    if (!strcmp(c, "echo")) { int nl = 1, i = 1; if (argc > 1 && !strcmp(argv[1], "-n")) { nl = 0; i = 2; } for (; i < argc; i++) { fputs(argv[i], stdout); if (i < argc - 1) fputc(' ', stdout); } if (nl) fputc('\n', stdout); return 0; }
    if (!strcmp(c, "test") || !strcmp(c, "[")) return builtin_test(argv, argc);
    if (!strcmp(c, "eval")) { char buf[8192]; buf[0] = 0; for (int i = 1; i < argc; i++) { if (i > 1) strcat(buf, " "); strcat(buf, argv[i]); } int st = g_tp, snt = g_ntok; tok *sv = g_toks; g_toks = 0; g_ntok = g_tokcap = 0; lex(buf); g_tp = 0; node *nn = parse_list(); if (nn) exec_node(nn); free(g_toks); g_toks = sv; g_ntok = snt; g_tp = st; return g_status; }
    if (!strcmp(c, "command")) {
        if (argc >= 3 && !strcmp(argv[1], "-v")) {
            const char *name = argv[2];
            static const char *bi[] = { "cd","pwd","exit","export","unset","echo","test","[",":","true","false","read","eval",".","source","return","set","shift","command",0 };
            for (int i = 0; bi[i]; i++) if (!strcmp(name, bi[i])) { printf("%s\n", name); return 0; }
            if (strchr(name, '/')) { if (access(name, X_OK) == 0) { printf("%s\n", name); return 0; } return 1; }
            const char *path = var_get("PATH"); if (!path) path = "/bin:/usr/bin";
            char pd[1024]; const char *ps = path;
            while (*ps) { int k = 0; while (*ps && *ps != ':' && k < 1000) pd[k++] = *ps++; if (*ps == ':') ps++; if (k == 0) continue; if (pd[k - 1] != '/') pd[k++] = '/'; strncpy(pd + k, name, 1023 - k); pd[1023] = 0; if (access(pd, X_OK) == 0) { printf("%s\n", pd); return 0; } }
            return 1;
        }
        if (argc >= 2) { fflush(NULL); pid_t p = fork(); if (p == 0) { execvp(argv[1], argv + 1); _exit(127); } int st; waitpid(p, &st, 0); return WIFEXITED(st) ? WEXITSTATUS(st) : 127; }
        return 0;
    }
    if (!strcmp(c, ".") || !strcmp(c, "source")) { if (argc > 1) { int fd = open(argv[1], O_RDONLY); if (fd >= 0) { char *b = malloc(65536); long n = read(fd, b, 65535); if (n < 0) n = 0; b[n] = 0; close(fd); tok *sv = g_toks; int snt = g_ntok, stp = g_tp; g_toks = 0; g_ntok = g_tokcap = 0; lex(b); g_tp = 0; node *nn = parse_list(); if (nn) exec_node(nn); free(b); free(g_toks); g_toks = sv; g_ntok = snt; g_tp = stp; } } return g_status; }
    *is_builtin = 0;
    return 0;
}

static int is_num(const char *s) { if (!*s) return 0; if (*s == '-') s++; while (*s) { if (*s < '0' || *s > '9') return 0; s++; } return 1; }
static int builtin_test(char **a, int argc) {
    int n = argc;
    if (!strcmp(a[0], "[")) { if (n > 0 && !strcmp(a[n - 1], "]")) n--; }
    char **v = a; int base = 1;
    int c = n - base;
    if (c == 0) return 1;
    if (c == 1) return v[base][0] ? 0 : 1;
    if (c == 2) { if (!strcmp(v[base], "!")) return v[base + 1][0] ? 1 : 0; if (!strcmp(v[base], "-z")) return v[base + 1][0] == 0 ? 0 : 1; if (!strcmp(v[base], "-n")) return v[base + 1][0] ? 0 : 1;
        if (!strcmp(v[base], "-e") || !strcmp(v[base], "-f") || !strcmp(v[base], "-d") || !strcmp(v[base], "-r") || !strcmp(v[base], "-x") || !strcmp(v[base], "-s")) { struct stat st; if (stat(v[base + 1], &st) != 0) return 1; if (!strcmp(v[base], "-d")) return S_ISDIR(st.st_mode) ? 0 : 1; if (!strcmp(v[base], "-f")) return S_ISREG(st.st_mode) ? 0 : 1; if (!strcmp(v[base], "-s")) return st.st_size > 0 ? 0 : 1; return 0; } }
    if (c == 3) { const char *l = v[base], *op = v[base + 1], *r = v[base + 2];
        if (!strcmp(op, "=") || !strcmp(op, "==")) return strcmp(l, r) == 0 ? 0 : 1;
        if (!strcmp(op, "!=")) return strcmp(l, r) != 0 ? 0 : 1;
        if (is_num(l) && is_num(r)) { long li = atol(l), ri = atol(r); if (!strcmp(op, "-eq")) return li == ri ? 0 : 1; if (!strcmp(op, "-ne")) return li != ri ? 0 : 1; if (!strcmp(op, "-lt")) return li < ri ? 0 : 1; if (!strcmp(op, "-le")) return li <= ri ? 0 : 1; if (!strcmp(op, "-gt")) return li > ri ? 0 : 1; if (!strcmp(op, "-ge")) return li >= ri ? 0 : 1; } }
    return 1;
}

static void run_text(const char *text) {
    tok *sv = g_toks; int a = g_ntok, b = g_tokcap, c = g_tp;
    g_toks = 0; g_ntok = g_tokcap = g_tp = 0;
    lex(text);
    g_tp = 0;
    while (cur() != T_EOF && !g_exit_flag) {
        node *n = parse_list();
        if (!n) { if (cur() != T_EOF) adv(); continue; }
        exec_node(n);
    }
    free(g_toks);
    g_toks = sv; g_ntok = a; g_tokcap = b; g_tp = c;
}

int main(int argc, char **argv) {
    g_argc = argc; g_argv = argv;
    char cwd[1024]; if (getcwd(cwd, sizeof cwd)) var_set("PWD", cwd, 1);

    if (argc >= 3 && !strcmp(argv[1], "-c")) {
        char *script = argv[2];
        static char *dummy[1];
        if (argc >= 4) { g_argv = argv + 3; g_argc = argc - 3; }
        else { dummy[0] = argv[0]; g_argv = dummy; g_argc = 1; }
        run_text(script);
        return g_exit_code ? g_exit_code : g_status;
    }

    int fd = 0;
    if (argc >= 2 && strcmp(argv[1], "-")) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) { fprintf(stderr, "sh: cannot open %s\n", argv[1]); return 1; }
        g_argv = argv + 1; g_argc = argc - 1;
    }

    size_t cap = 65536, len = 0; char *buf = malloc(cap);
    for (;;) { if (len + 4096 > cap) { cap *= 2; buf = xrealloc(buf, cap); } long r = read(fd, buf + len, cap - len); if (r <= 0) break; len += r; }
    buf[len] = 0;
    if (fd) close(fd);
    run_text(buf);
    free(buf);
    return g_exit_code ? g_exit_code : g_status;
}

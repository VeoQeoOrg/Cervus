#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <readline.h>

extern char **environ;

#define SH_MAX_NEST 64

static const char *g_shname = "sh";
static pid_t g_shell_pid;
static int   g_lineno;
static int  g_status;
static int  g_opt_e, g_opt_u, g_opt_x, g_opt_n;
static int  g_interactive;
static int  g_exiting, g_exit_code;
static int  g_break, g_continue, g_return;
static int  g_loop_depth, g_func_depth;
static int  g_cond_depth;
static int  g_last_bg;
static char *g_exit_trap;

static void fatal(const char *m)
{
    fprintf(stderr, "%s: %s\n", g_shname, m);
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
    char  *q;
    size_t n, cap;
} sbuf;

static void sb_init(sbuf *b)
{
    b->p = NULL; b->q = NULL; b->n = 0; b->cap = 0;
}

static void sb_free(sbuf *b)
{
    free(b->p); free(b->q);
    sb_init(b);
}

static void sb_room(sbuf *b, size_t extra)
{
    if (b->n + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap * 2 : 64;
    while (cap < b->n + extra + 1) cap *= 2;
    b->p = xrealloc(b->p, cap);
    b->q = xrealloc(b->q, cap);
    b->cap = cap;
}

static void sb_putc(sbuf *b, char c, char quoted)
{
    sb_room(b, 1);
    b->p[b->n] = c;
    b->q[b->n] = quoted;
    b->n++;
    b->p[b->n] = 0;
}

static void sb_puts(sbuf *b, const char *s, char quoted)
{
    while (*s) sb_putc(b, *s++, quoted);
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

typedef struct {
    char *name;
    char *val;
    int   exported;
    int   readonly;
} var_t;

static var_t *g_vars;
static int    g_nvars, g_cvars;

static int var_index(const char *name)
{
    for (int i = 0; i < g_nvars; i++)
        if (!strcmp(g_vars[i].name, name)) return i;
    return -1;
}

static const char *var_get(const char *name)
{
    int i = var_index(name);
    if (i >= 0) return g_vars[i].val ? g_vars[i].val : "";
    const char *e = getenv(name);
    return e ? e : NULL;
}

static void var_set(const char *name, const char *val)
{
    int i = var_index(name);
    if (i < 0) {
        if (g_nvars + 1 > g_cvars) {
            g_cvars = g_cvars ? g_cvars * 2 : 32;
            g_vars = xrealloc(g_vars, (size_t)g_cvars * sizeof(var_t));
        }
        i = g_nvars++;
        memset(&g_vars[i], 0, sizeof(var_t));
        g_vars[i].name = xstrdup(name);
        g_vars[i].exported = getenv(name) != NULL;
    }
    if (g_vars[i].readonly) return;
    free(g_vars[i].val);
    g_vars[i].val = xstrdup(val);
    if (g_vars[i].exported) setenv(name, val, 1);
}

static void var_export(const char *name)
{
    int i = var_index(name);
    if (i < 0) {
        const char *e = getenv(name);
        var_set(name, e ? e : "");
        i = var_index(name);
    }
    g_vars[i].exported = 1;
    setenv(name, g_vars[i].val ? g_vars[i].val : "", 1);
}

static void var_unset(const char *name)
{
    int i = var_index(name);
    if (i >= 0) {
        if (g_vars[i].readonly) return;
        free(g_vars[i].name);
        free(g_vars[i].val);
        g_vars[i] = g_vars[--g_nvars];
    }
    unsetenv(name);
}

static svec g_argv;

static const char *positional(int i)
{
    if (i == 0) return g_shname;
    if (i - 1 < g_argv.n) return g_argv.v[i - 1];
    return NULL;
}

enum {
    N_SIMPLE, N_PIPE, N_AND, N_OR, N_LIST, N_BANG,
    N_SUBSHELL, N_BRACE, N_IF, N_WHILE, N_UNTIL, N_FOR, N_CASE, N_FUNC
};

enum {
    R_IN, R_OUT, R_APPEND, R_HEREDOC, R_DUPIN, R_DUPOUT, R_RW, R_CLOSE
};

typedef struct redir {
    int   fd;
    int   type;
    char *word;
    char *here;
    int   here_raw;
    struct redir *next;
} redir_t;

typedef struct node node_t;

typedef struct casearm {
    svec  pats;
    node_t *body;
    struct casearm *next;
} casearm_t;

struct node {
    int      t;
    svec     words;
    svec     assigns;
    redir_t *redirs;
    node_t  *a, *b, *c;
    node_t **kids;
    int      nkids;
    char    *name;
    svec     items;
    int      has_items;
    casearm_t *arms;
    int      bg;
    int      line;
};

static node_t *node_new(int t)
{
    node_t *n = xmalloc(sizeof(node_t));
    memset(n, 0, sizeof(node_t));
    n->t = t;
    sv_init(&n->words);
    sv_init(&n->assigns);
    sv_init(&n->items);
    return n;
}

static void redir_free(redir_t *r)
{
    while (r) {
        redir_t *nx = r->next;
        free(r->word);
        free(r->here);
        free(r);
        r = nx;
    }
}

static void node_free(node_t *n)
{
    if (!n) return;
    sv_free(&n->words);
    sv_free(&n->assigns);
    sv_free(&n->items);
    redir_free(n->redirs);
    node_free(n->a);
    node_free(n->b);
    node_free(n->c);
    for (int i = 0; i < n->nkids; i++) node_free(n->kids[i]);
    free(n->kids);
    free(n->name);
    casearm_t *arm = n->arms;
    while (arm) {
        casearm_t *nx = arm->next;
        sv_free(&arm->pats);
        node_free(arm->body);
        free(arm);
        arm = nx;
    }
    free(n);
}

typedef struct func {
    char   *name;
    node_t *body;
    struct func *next;
} func_t;

static func_t *g_funcs;

static func_t *func_find(const char *name)
{
    for (func_t *f = g_funcs; f; f = f->next)
        if (!strcmp(f->name, name)) return f;
    return NULL;
}

static void func_define(const char *name, node_t *body)
{
    func_t *f = func_find(name);
    if (f) {
        node_free(f->body);
        f->body = body;
        return;
    }
    f = xmalloc(sizeof(func_t));
    f->name = xstrdup(name);
    f->body = body;
    f->next = g_funcs;
    g_funcs = f;
}

enum {
    T_EOF, T_WORD, T_NEWLINE, T_SEMI, T_DSEMI, T_AMP, T_PIPE,
    T_ANDAND, T_OROR, T_LPAREN, T_RPAREN,
    T_LESS, T_GREAT, T_DGREAT, T_DLESS, T_DLESSDASH,
    T_LESSAND, T_GREATAND, T_LESSGREAT, T_CLOBBER
};

typedef struct {
    const char *s;
    size_t      p, n;
    int         type;
    char       *text;
    int         quoted;
    int         had_io_number;
    int         io_number;
    int         unterminated;
    int         spaced;
    redir_t    *pending[16];
    int         npending;
    int         error;
    size_t      line, counted_to, tok_line;
    unsigned long tokno;
} lexer_t;

static void lx_init(lexer_t *lx, const char *s)
{
    memset(lx, 0, sizeof(*lx));
    lx->s = s;
    lx->n = strlen(s);
    lx->line = 1;
    lx->tok_line = 1;
}

static int lx_at(lexer_t *lx, size_t off)
{
    return (lx->p + off < lx->n) ? (unsigned char)lx->s[lx->p + off] : -1;
}

static int is_op_char(int c)
{
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' ||
           c == '(' || c == ')';
}

static int is_name_char(int c) { return isalnum(c) || c == '_'; }

static void read_heredocs(lexer_t *lx)
{
    for (int i = 0; i < lx->npending; i++) {
        redir_t *r = lx->pending[i];
        sbuf body;
        sb_init(&body);
        const char *delim = r->word ? r->word : "";
        int strip = (r->type == R_HEREDOC && r->here_raw == 2);
        if (strip) r->here_raw = 0;
        for (;;) {
            size_t start = lx->p;
            while (lx->p < lx->n && lx->s[lx->p] != '\n') lx->p++;
            size_t len = lx->p - start;
            if (lx->p < lx->n) lx->p++;
            const char *line = lx->s + start;
            size_t off = 0;
            if (strip) while (off < len && line[off] == '\t') off++;
            if (len - off == strlen(delim) && !memcmp(line + off, delim, len - off))
                break;
            for (size_t k = off; k < len; k++) sb_putc(&body, line[k], 0);
            sb_putc(&body, '\n', 0);
            if (start >= lx->n) { lx->unterminated = 1; break; }
        }
        r->here = body.p ? body.p : xstrdup("");
        body.p = NULL;
        sb_free(&body);
    }
    lx->npending = 0;
}

static void lx_skip_blanks(lexer_t *lx)
{
    for (;;) {
        while (lx->p < lx->n && (lx->s[lx->p] == ' ' || lx->s[lx->p] == '\t')) {
            lx->p++;
            lx->spaced = 1;
        }
        if (lx->p + 1 < lx->n && lx->s[lx->p] == '\\' && lx->s[lx->p + 1] == '\n') {
            lx->p += 2;
            lx->spaced = 1;
            continue;
        }
        if (lx->p < lx->n && lx->s[lx->p] == '#') {
            while (lx->p < lx->n && lx->s[lx->p] != '\n') lx->p++;
            continue;
        }
        break;
    }
}

static void scan_dollar(lexer_t *lx, sbuf *b);

static void scan_dquote(lexer_t *lx, sbuf *b)
{
    sb_putc(b, '"', 0);
    lx->p++;
    while (lx->p < lx->n && lx->s[lx->p] != '"') {
        char c = lx->s[lx->p];
        if (c == '\\' && lx->p + 1 < lx->n) {
            sb_putc(b, c, 0);
            sb_putc(b, lx->s[lx->p + 1], 0);
            lx->p += 2;
            continue;
        }
        if (c == '$') { scan_dollar(lx, b); continue; }
        if (c == '`') {
            sb_putc(b, c, 0);
            lx->p++;
            while (lx->p < lx->n && lx->s[lx->p] != '`') {
                if (lx->s[lx->p] == '\\' && lx->p + 1 < lx->n) {
                    sb_putc(b, lx->s[lx->p], 0);
                    lx->p++;
                }
                sb_putc(b, lx->s[lx->p], 0);
                lx->p++;
            }
            if (lx->p < lx->n) { sb_putc(b, '`', 0); lx->p++; }
            continue;
        }
        sb_putc(b, c, 0);
        lx->p++;
    }
    if (lx->p < lx->n) lx->p++;
    sb_putc(b, '"', 0);
}

static void scan_balanced(lexer_t *lx, sbuf *b, char open, char close)
{
    int depth = 0;
    while (lx->p < lx->n) {
        char c = lx->s[lx->p];
        if (c == '\\' && lx->p + 1 < lx->n) {
            sb_putc(b, c, 0);
            sb_putc(b, lx->s[lx->p + 1], 0);
            lx->p += 2;
            continue;
        }
        if (c == '\'') {
            sb_putc(b, c, 0);
            lx->p++;
            while (lx->p < lx->n && lx->s[lx->p] != '\'') { sb_putc(b, lx->s[lx->p], 0); lx->p++; }
            if (lx->p < lx->n) { sb_putc(b, '\'', 0); lx->p++; }
            continue;
        }
        if (c == '"') { scan_dquote(lx, b); continue; }
        if (c == open) depth++;
        if (c == close) {
            depth--;
            sb_putc(b, c, 0);
            lx->p++;
            if (depth == 0) return;
            continue;
        }
        sb_putc(b, c, 0);
        lx->p++;
    }
}

static void scan_dollar(lexer_t *lx, sbuf *b)
{
    sb_putc(b, '$', 0);
    lx->p++;
    if (lx->p >= lx->n) return;
    char c = lx->s[lx->p];
    if (c == '(') {
        if (lx->p + 1 < lx->n && lx->s[lx->p + 1] == '(') {
            sb_putc(b, '(', 0);
            lx->p++;
            scan_balanced(lx, b, '(', ')');
            if (lx->p < lx->n && lx->s[lx->p] == ')') { sb_putc(b, ')', 0); lx->p++; }
            return;
        }
        scan_balanced(lx, b, '(', ')');
        return;
    }
    if (c == '{') {
        scan_balanced(lx, b, '{', '}');
        return;
    }
    sb_putc(b, c, 0);
    lx->p++;
    if (is_name_char(c) && !isdigit((unsigned char)c))
        while (lx->p < lx->n && is_name_char((unsigned char)lx->s[lx->p])) {
            sb_putc(b, lx->s[lx->p], 0);
            lx->p++;
        }
}

static void lx_next(lexer_t *lx)
{
    free(lx->text);
    lx->text = NULL;
    lx->quoted = 0;
    lx->had_io_number = 0;
    lx->spaced = 0;

    lx->tokno++;

    lx_skip_blanks(lx);

    while (lx->counted_to < lx->p && lx->counted_to < lx->n) {
        if (lx->s[lx->counted_to] == '\n') lx->line++;
        lx->counted_to++;
    }
    lx->tok_line = lx->line;

    if (lx->p >= lx->n) { lx->type = T_EOF; return; }

    char c = lx->s[lx->p];

    if (c == '\n') {
        lx->p++;
        if (lx->npending) read_heredocs(lx);
        lx->type = T_NEWLINE;
        return;
    }

    if (c == '&' && lx_at(lx, 1) == '&') { lx->p += 2; lx->type = T_ANDAND; return; }
    if (c == '|' && lx_at(lx, 1) == '|') { lx->p += 2; lx->type = T_OROR;   return; }
    if (c == ';' && lx_at(lx, 1) == ';') { lx->p += 2; lx->type = T_DSEMI;  return; }
    if (c == '&') { lx->p++; lx->type = T_AMP;    return; }
    if (c == '|') { lx->p++; lx->type = T_PIPE;   return; }
    if (c == ';') { lx->p++; lx->type = T_SEMI;   return; }
    if (c == '(') { lx->p++; lx->type = T_LPAREN; return; }
    if (c == ')') { lx->p++; lx->type = T_RPAREN; return; }

    if (isdigit((unsigned char)c)) {
        size_t q = lx->p;
        while (q < lx->n && isdigit((unsigned char)lx->s[q])) q++;
        if (q < lx->n && (lx->s[q] == '<' || lx->s[q] == '>')) {
            lx->io_number = (int)strtol(lx->s + lx->p, NULL, 10);
            lx->had_io_number = 1;
            lx->p = q;
            c = lx->s[lx->p];
        }
    }

    if (c == '<') {
        if (lx_at(lx, 1) == '<') {
            if (lx_at(lx, 2) == '-') { lx->p += 3; lx->type = T_DLESSDASH; return; }
            lx->p += 2; lx->type = T_DLESS; return;
        }
        if (lx_at(lx, 1) == '&') { lx->p += 2; lx->type = T_LESSAND;   return; }
        if (lx_at(lx, 1) == '>') { lx->p += 2; lx->type = T_LESSGREAT; return; }
        lx->p++; lx->type = T_LESS; return;
    }
    if (c == '>') {
        if (lx_at(lx, 1) == '>') { lx->p += 2; lx->type = T_DGREAT;  return; }
        if (lx_at(lx, 1) == '&') { lx->p += 2; lx->type = T_GREATAND; return; }
        if (lx_at(lx, 1) == '|') { lx->p += 2; lx->type = T_CLOBBER; return; }
        lx->p++; lx->type = T_GREAT; return;
    }

    sbuf b;
    sb_init(&b);
    while (lx->p < lx->n) {
        c = lx->s[lx->p];
        if (c == ' ' || c == '\t' || c == '\n' || is_op_char(c)) break;
        if (c == '\\') {
            if (lx->p + 1 < lx->n && lx->s[lx->p + 1] == '\n') { lx->p += 2; continue; }
            lx->quoted = 1;
            sb_putc(&b, c, 0);
            lx->p++;
            if (lx->p < lx->n) { sb_putc(&b, lx->s[lx->p], 0); lx->p++; }
            continue;
        }
        if (c == '\'') {
            lx->quoted = 1;
            sb_putc(&b, c, 0);
            lx->p++;
            while (lx->p < lx->n && lx->s[lx->p] != '\'') { sb_putc(&b, lx->s[lx->p], 0); lx->p++; }
            if (lx->p < lx->n) { sb_putc(&b, '\'', 0); lx->p++; }
            continue;
        }
        if (c == '"') { lx->quoted = 1; scan_dquote(lx, &b); continue; }
        if (c == '`') {
            sb_putc(&b, c, 0);
            lx->p++;
            while (lx->p < lx->n && lx->s[lx->p] != '`') {
                if (lx->s[lx->p] == '\\' && lx->p + 1 < lx->n) { sb_putc(&b, lx->s[lx->p], 0); lx->p++; }
                sb_putc(&b, lx->s[lx->p], 0);
                lx->p++;
            }
            if (lx->p < lx->n) { sb_putc(&b, '`', 0); lx->p++; }
            continue;
        }
        if (c == '$') { scan_dollar(lx, &b); continue; }
        sb_putc(&b, c, 0);
        lx->p++;
    }
    lx->text = b.p ? b.p : xstrdup("");
    b.p = NULL;
    sb_free(&b);
    lx->type = T_WORD;
}

static int is_word(lexer_t *lx, const char *w)
{
    return lx->type == T_WORD && !lx->quoted && lx->text && !strcmp(lx->text, w);
}

static int is_reserved_text(const char *t)
{
    static const char *const R[] = {
        "if", "then", "elif", "else", "fi", "while", "until", "for",
        "do", "done", "case", "esac", "in", "{", "}", "!", NULL
    };
    for (int i = 0; R[i]; i++) if (!strcmp(t, R[i])) return 1;
    return 0;
}

static node_t *parse_list(lexer_t *lx, int allow_reserved_stop);
static node_t *parse_and_or(lexer_t *lx);

static void skip_newlines(lexer_t *lx)
{
    while (lx->type == T_NEWLINE) lx_next(lx);
}

static int redir_type_of(int tok, int *deffd)
{
    switch (tok) {
        case T_LESS:      *deffd = 0; return R_IN;
        case T_GREAT:     *deffd = 1; return R_OUT;
        case T_CLOBBER:   *deffd = 1; return R_OUT;
        case T_DGREAT:    *deffd = 1; return R_APPEND;
        case T_DLESS:     *deffd = 0; return R_HEREDOC;
        case T_DLESSDASH: *deffd = 0; return R_HEREDOC;
        case T_LESSAND:   *deffd = 0; return R_DUPIN;
        case T_GREATAND:  *deffd = 1; return R_DUPOUT;
        case T_LESSGREAT: *deffd = 0; return R_RW;
    }
    return -1;
}

static int parse_redir(lexer_t *lx, redir_t **list)
{
    int deffd = 0;
    int strip = (lx->type == T_DLESSDASH);
    int type = redir_type_of(lx->type, &deffd);
    if (type < 0) return 0;

    redir_t *r = xmalloc(sizeof(redir_t));
    memset(r, 0, sizeof(*r));
    r->type = type;
    r->fd = lx->had_io_number ? lx->io_number : deffd;
    if (type == R_HEREDOC && strip) r->here_raw = 2;

    lx_next(lx);
    if (lx->type != T_WORD) {
        fprintf(stderr, "%s: line %zu: syntax error after redirection near '%s'\n",
                g_shname, lx->tok_line, lx->text ? lx->text : "");
        free(r);
        lx->error = 1;
        return 0;
    }
    r->word = xstrdup(lx->text);
    if (type == R_HEREDOC) {
        int q = lx->quoted;
        char *clean = xmalloc(strlen(r->word) + 1);
        size_t o = 0;
        for (const char *s = r->word; *s; s++) {
            if (*s == '"' || *s == '\'') continue;
            if (*s == '\\' && s[1]) { clean[o++] = *++s; continue; }
            clean[o++] = *s;
        }
        clean[o] = 0;
        free(r->word);
        r->word = clean;
        if (r->here_raw != 2) r->here_raw = q ? 1 : 0;
        else if (q) r->here_raw = 2;
        if (lx->npending < 16) lx->pending[lx->npending++] = r;
    }
    lx_next(lx);

    redir_t **tail = list;
    while (*tail) tail = &(*tail)->next;
    *tail = r;
    return 1;
}

static int looks_like_assign(const char *w)
{
    if (!w || !isalpha((unsigned char)w[0]) ) {
        if (!w || w[0] != '_') return 0;
    }
    for (const char *s = w; *s; s++) {
        if (*s == '=') return s != w;
        if (!is_name_char((unsigned char)*s)) return 0;
    }
    return 0;
}

static node_t *parse_compound(lexer_t *lx);

static node_t *parse_simple(lexer_t *lx)
{
    node_t *n = node_new(N_SIMPLE);
    n->line = (int)lx->tok_line;
    int seen_word = 0;

    for (;;) {
        if (lx->type == T_WORD) {
            if (!seen_word && looks_like_assign(lx->text)) {
                sv_push(&n->assigns, xstrdup(lx->text));
                lx_next(lx);
                continue;
            }
            sv_push(&n->words, xstrdup(lx->text));
            seen_word = 1;
            lx_next(lx);
            continue;
        }
        if (redir_type_of(lx->type, &(int){0}) >= 0) {
            if (!parse_redir(lx, &n->redirs)) { node_free(n); return NULL; }
            continue;
        }
        break;
    }

    if (n->words.n == 0 && n->assigns.n == 0 && !n->redirs) {
        node_free(n);
        return NULL;
    }
    return n;
}

static node_t *parse_case(lexer_t *lx)
{
    lx_next(lx);
    if (lx->type != T_WORD) { lx->error = 1; return NULL; }
    node_t *n = node_new(N_CASE);
    n->name = xstrdup(lx->text);
    lx_next(lx);
    skip_newlines(lx);
    if (!is_word(lx, "in")) { lx->error = 1; node_free(n); return NULL; }
    lx_next(lx);
    skip_newlines(lx);

    casearm_t **tail = &n->arms;
    while (!is_word(lx, "esac") && lx->type != T_EOF) {
        unsigned long before = lx->tokno;
        casearm_t *arm = xmalloc(sizeof(casearm_t));
        memset(arm, 0, sizeof(*arm));
        sv_init(&arm->pats);

        if (lx->type == T_LPAREN) lx_next(lx);
        for (;;) {
            if (lx->type != T_WORD) { lx->error = 1; break; }
            sv_push(&arm->pats, xstrdup(lx->text));
            lx_next(lx);
            if (lx->type == T_PIPE) { lx_next(lx); continue; }
            break;
        }
        if (lx->type == T_RPAREN) lx_next(lx);
        skip_newlines(lx);

        if (!is_word(lx, "esac") && lx->type != T_DSEMI)
            arm->body = parse_list(lx, 1);

        *tail = arm;
        tail = &arm->next;

        if (lx->type == T_DSEMI) { lx_next(lx); skip_newlines(lx); }
        else break;
        if (lx->tokno == before) { lx->error = 1; break; }
    }
    if (lx->error || !is_word(lx, "esac")) { lx->error = 1; node_free(n); return NULL; }
    lx_next(lx);
    return n;
}

static node_t *parse_for(lexer_t *lx)
{
    lx_next(lx);
    if (lx->type != T_WORD) { lx->error = 1; return NULL; }
    node_t *n = node_new(N_FOR);
    n->name = xstrdup(lx->text);
    lx_next(lx);
    if (lx->type == T_SEMI) lx_next(lx);
    skip_newlines(lx);
    if (is_word(lx, "in")) {
        n->has_items = 1;
        lx_next(lx);
        while (lx->type == T_WORD) {
            sv_push(&n->items, xstrdup(lx->text));
            lx_next(lx);
        }
        if (lx->type == T_SEMI) lx_next(lx);
        skip_newlines(lx);
    }
    if (!is_word(lx, "do")) { lx->error = 1; node_free(n); return NULL; }
    lx_next(lx);
    n->a = parse_list(lx, 1);
    if (!is_word(lx, "done")) { lx->error = 1; node_free(n); return NULL; }
    lx_next(lx);
    return n;
}

static node_t *parse_if(lexer_t *lx)
{
    lx_next(lx);
    node_t *n = node_new(N_IF);
    n->a = parse_list(lx, 1);
    if (!is_word(lx, "then")) { lx->error = 1; node_free(n); return NULL; }
    lx_next(lx);
    n->b = parse_list(lx, 1);
    if (is_word(lx, "elif")) {
        n->c = parse_if(lx);
        return n;
    }
    if (is_word(lx, "else")) {
        lx_next(lx);
        n->c = parse_list(lx, 1);
    }
    if (!is_word(lx, "fi")) { lx->error = 1; node_free(n); return NULL; }
    lx_next(lx);
    return n;
}

static node_t *parse_compound(lexer_t *lx)
{
    if (is_word(lx, "if")) return parse_if(lx);
    if (is_word(lx, "case")) return parse_case(lx);
    if (is_word(lx, "for")) return parse_for(lx);

    if (is_word(lx, "while") || is_word(lx, "until")) {
        int until = is_word(lx, "until");
        lx_next(lx);
        node_t *n = node_new(until ? N_UNTIL : N_WHILE);
        n->a = parse_list(lx, 1);
        if (!is_word(lx, "do")) { lx->error = 1; node_free(n); return NULL; }
        lx_next(lx);
        n->b = parse_list(lx, 1);
        if (!is_word(lx, "done")) { lx->error = 1; node_free(n); return NULL; }
        lx_next(lx);
        return n;
    }

    if (is_word(lx, "{")) {
        lx_next(lx);
        node_t *n = node_new(N_BRACE);
        n->a = parse_list(lx, 1);
        if (!is_word(lx, "}")) { lx->error = 1; node_free(n); return NULL; }
        lx_next(lx);
        return n;
    }

    if (lx->type == T_LPAREN) {
        lx_next(lx);
        node_t *n = node_new(N_SUBSHELL);
        n->a = parse_list(lx, 1);
        if (lx->type != T_RPAREN) { lx->error = 1; node_free(n); return NULL; }
        lx_next(lx);
        return n;
    }

    return NULL;
}

static int lx_lparen_follows(lexer_t *lx)
{
    size_t q = lx->p;
    while (q < lx->n && (lx->s[q] == ' ' || lx->s[q] == '\t')) q++;
    return q < lx->n && lx->s[q] == '(';
}

static node_t *parse_command(lexer_t *lx)
{
    if (lx->type == T_WORD && !lx->quoted && !is_reserved_text(lx->text) &&
        lx_lparen_follows(lx)) {
        char *name = xstrdup(lx->text);
        lx_next(lx);
        lx_next(lx);
        if (lx->type != T_RPAREN) { lx->error = 1; free(name); return NULL; }
        lx_next(lx);
        skip_newlines(lx);
        node_t *body = parse_compound(lx);
        if (!body) { lx->error = 1; free(name); return NULL; }
        node_t *n = node_new(N_FUNC);
        n->name = name;
        n->a = body;
        return n;
    }

    node_t *c = parse_compound(lx);
    if (c) {
        while (redir_type_of(lx->type, &(int){0}) >= 0)
            if (!parse_redir(lx, &c->redirs)) break;
        return c;
    }
    return parse_simple(lx);
}

static node_t *parse_pipeline(lexer_t *lx)
{
    int bang = 0;
    while (is_word(lx, "!")) { bang = !bang; lx_next(lx); }

    node_t *first = parse_command(lx);
    if (!first) return NULL;

    if (lx->type != T_PIPE) {
        if (!bang) return first;
        node_t *n = node_new(N_BANG);
        n->a = first;
        return n;
    }

    node_t *p = node_new(N_PIPE);
    p->kids = xmalloc(sizeof(node_t *) * 2);
    p->kids[p->nkids++] = first;
    int cap = 2;
    while (lx->type == T_PIPE) {
        lx_next(lx);
        skip_newlines(lx);
        node_t *c = parse_command(lx);
        if (!c) { lx->error = 1; break; }
        if (p->nkids + 1 > cap) {
            cap *= 2;
            p->kids = xrealloc(p->kids, sizeof(node_t *) * cap);
        }
        p->kids[p->nkids++] = c;
    }
    if (!bang) return p;
    node_t *n = node_new(N_BANG);
    n->a = p;
    return n;
}

static node_t *parse_and_or(lexer_t *lx)
{
    node_t *left = parse_pipeline(lx);
    if (!left) return NULL;
    for (;;) {
        if (lx->type == T_ANDAND || lx->type == T_OROR) {
            int t = (lx->type == T_ANDAND) ? N_AND : N_OR;
            lx_next(lx);
            skip_newlines(lx);
            node_t *right = parse_pipeline(lx);
            if (!right) { lx->error = 1; return left; }
            node_t *n = node_new(t);
            n->a = left;
            n->b = right;
            left = n;
            continue;
        }
        break;
    }
    return left;
}

static int at_list_end(lexer_t *lx)
{
    if (lx->type == T_EOF || lx->type == T_RPAREN || lx->type == T_DSEMI) return 1;
    if (lx->type == T_WORD && !lx->quoted) {
        static const char *const E[] = { "then", "else", "elif", "fi", "do",
                                         "done", "esac", "}", NULL };
        for (int i = 0; E[i]; i++) if (!strcmp(lx->text, E[i])) return 1;
    }
    return 0;
}

static node_t *parse_list(lexer_t *lx, int allow_reserved_stop)
{
    node_t *head = NULL;
    for (;;) {
        skip_newlines(lx);
        if (lx->type == T_EOF) break;
        if (allow_reserved_stop && at_list_end(lx)) break;

        unsigned long before = lx->tokno;
        node_t *cmd = parse_and_or(lx);
        if (!cmd) break;
        if (lx->tokno == before) { lx->error = 1; node_free(cmd); break; }

        if (lx->type == T_AMP) { cmd->bg = 1; lx_next(lx); }
        else if (lx->type == T_SEMI) lx_next(lx);

        if (!head) head = cmd;
        else {
            node_t *n = node_new(N_LIST);
            n->a = head;
            n->b = cmd;
            head = n;
        }

        if (lx->error) break;
        if (lx->type == T_EOF) break;
        if (allow_reserved_stop && at_list_end(lx)) break;
        if (lx->type != T_NEWLINE && lx->type != T_SEMI && lx->type != T_AMP) {
            if (lx->type == T_RPAREN) break;
        }
    }
    return head;
}

static int run_string(const char *src);
static int exec_node(node_t *n);

static char *capture(const char *script, int *status)
{
    int pfd[2];
    if (pipe(pfd) < 0) return xstrdup("");

    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return xstrdup(""); }
    if (pid == 0) {
        close(pfd[0]);
        if (pfd[1] != 1) { dup2(pfd[1], 1); close(pfd[1]); }
        int rc = run_string(script);
        fflush(NULL);
        _exit(rc);
    }
    close(pfd[1]);

    sbuf b;
    sb_init(&b);
    char tmp[1024];
    ssize_t r;
    while ((r = read(pfd[0], tmp, sizeof tmp)) > 0)
        for (ssize_t i = 0; i < r; i++) sb_putc(&b, tmp[i], 0);
    close(pfd[0]);

    int st = 0;
    waitpid(pid, &st, 0);
    if (status) *status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);

    while (b.n > 0 && b.p[b.n - 1] == '\n') b.p[--b.n] = 0;
    char *out = b.p ? b.p : xstrdup("");
    b.p = NULL;
    sb_free(&b);
    return out;
}

static long arith_expr(const char **p);

static void arith_ws(const char **p) { while (**p == ' ' || **p == '\t') (*p)++; }

static long arith_primary(const char **p)
{
    arith_ws(p);
    if (**p == '(') {
        (*p)++;
        long v = arith_expr(p);
        arith_ws(p);
        if (**p == ')') (*p)++;
        return v;
    }
    if (**p == '-') { (*p)++; return -arith_primary(p); }
    if (**p == '+') { (*p)++; return arith_primary(p); }
    if (**p == '!') { (*p)++; return !arith_primary(p); }
    if (**p == '~') { (*p)++; return ~arith_primary(p); }
    if (isdigit((unsigned char)**p)) {
        char *end;
        long v = strtol(*p, &end, 0);
        *p = end;
        return v;
    }
    if (is_name_char((unsigned char)**p)) {
        char name[128];
        size_t k = 0;
        while (is_name_char((unsigned char)**p) && k < sizeof name - 1) name[k++] = *(*p)++;
        name[k] = 0;
        const char *v = var_get(name);
        return v ? strtol(v, NULL, 0) : 0;
    }
    return 0;
}

static long arith_pow(const char **p)
{
    long v = arith_primary(p);
    arith_ws(p);
    if (**p == '*' && (*p)[1] == '*') {
        *p += 2;
        long e = arith_pow(p);
        long r = 1;
        while (e-- > 0) r *= v;
        return r;
    }
    return v;
}

static long arith_mul(const char **p)
{
    long v = arith_pow(p);
    for (;;) {
        arith_ws(p);
        char c = **p;
        if (c == '*' || c == '/' || c == '%') {
            (*p)++;
            long r = arith_pow(p);
            if (c == '*') v *= r;
            else if (r == 0) v = 0;
            else if (c == '/') v /= r;
            else v %= r;
            continue;
        }
        break;
    }
    return v;
}

static long arith_add(const char **p)
{
    long v = arith_mul(p);
    for (;;) {
        arith_ws(p);
        if (**p == '+' && (*p)[1] != '+') { (*p)++; v += arith_mul(p); continue; }
        if (**p == '-' && (*p)[1] != '-') { (*p)++; v -= arith_mul(p); continue; }
        break;
    }
    return v;
}

static long arith_shift(const char **p)
{
    long v = arith_add(p);
    for (;;) {
        arith_ws(p);
        if (**p == '<' && (*p)[1] == '<') { *p += 2; v <<= arith_add(p); continue; }
        if (**p == '>' && (*p)[1] == '>') { *p += 2; v >>= arith_add(p); continue; }
        break;
    }
    return v;
}

static long arith_rel(const char **p)
{
    long v = arith_shift(p);
    for (;;) {
        arith_ws(p);
        if (**p == '<' && (*p)[1] == '=') { *p += 2; v = v <= arith_shift(p); continue; }
        if (**p == '>' && (*p)[1] == '=') { *p += 2; v = v >= arith_shift(p); continue; }
        if (**p == '<') { (*p)++; v = v < arith_shift(p); continue; }
        if (**p == '>') { (*p)++; v = v > arith_shift(p); continue; }
        break;
    }
    return v;
}

static long arith_eq(const char **p)
{
    long v = arith_rel(p);
    for (;;) {
        arith_ws(p);
        if (**p == '=' && (*p)[1] == '=') { *p += 2; v = v == arith_rel(p); continue; }
        if (**p == '!' && (*p)[1] == '=') { *p += 2; v = v != arith_rel(p); continue; }
        break;
    }
    return v;
}

static long arith_band(const char **p)
{
    long v = arith_eq(p);
    while (arith_ws(p), **p == '&' && (*p)[1] != '&') { (*p)++; v &= arith_eq(p); }
    return v;
}

static long arith_bxor(const char **p)
{
    long v = arith_band(p);
    while (arith_ws(p), **p == '^') { (*p)++; v ^= arith_band(p); }
    return v;
}

static long arith_bor(const char **p)
{
    long v = arith_bxor(p);
    while (arith_ws(p), **p == '|' && (*p)[1] != '|') { (*p)++; v |= arith_bxor(p); }
    return v;
}

static long arith_and(const char **p)
{
    long v = arith_bor(p);
    while (arith_ws(p), **p == '&' && (*p)[1] == '&') { *p += 2; long r = arith_bor(p); v = v && r; }
    return v;
}

static long arith_expr(const char **p)
{
    long v = arith_and(p);
    while (arith_ws(p), **p == '|' && (*p)[1] == '|') { *p += 2; long r = arith_and(p); v = v || r; }
    return v;
}

static long arith_eval(const char *s)
{
    char *copy = xstrdup(s);
    char *eq = NULL;
    for (char *t = copy; *t; t++) {
        if (*t == '=' && t != copy && t[-1] != '=' && t[-1] != '!' &&
            t[-1] != '<' && t[-1] != '>' && t[1] != '=') { eq = t; break; }
    }
    if (eq) {
        *eq = 0;
        char *name = copy;
        while (*name == ' ') name++;
        char *end = name + strlen(name);
        while (end > name && end[-1] == ' ') *--end = 0;
        const char *rest = eq + 1;
        long v = arith_expr(&rest);
        char nb[32];
        snprintf(nb, sizeof nb, "%ld", v);
        if (*name) var_set(name, nb);
        free(copy);
        return v;
    }
    const char *p = copy;
    long v = arith_expr(&p);
    free(copy);
    return v;
}

static char *trim_prefix(const char *val, const char *pat, int greedy)
{
    size_t len = strlen(val);
    size_t best = (size_t)-1;
    for (size_t i = 0; i <= len; i++) {
        char *sub = xstrndup(val, i);
        int m = fnmatch(pat, sub, 0) == 0;
        free(sub);
        if (m) { best = i; if (!greedy) break; }
    }
    return xstrdup(best == (size_t)-1 ? val : val + best);
}

static char *trim_suffix(const char *val, const char *pat, int greedy)
{
    size_t len = strlen(val);
    size_t best = (size_t)-1;
    if (greedy) {
        for (size_t i = 0; i <= len; i++)
            if (fnmatch(pat, val + i, 0) == 0) { best = i; break; }
    } else {
        for (size_t i = len + 1; i-- > 0; )
            if (fnmatch(pat, val + i, 0) == 0) { best = i; break; }
    }
    if (best == (size_t)-1) return xstrdup(val);
    return xstrndup(val, best);
}

static int match_at(const char *val, size_t i, const char *pat, size_t *len)
{
    size_t n = strlen(val);
    for (size_t j = n + 1; j-- > i; ) {
        char *sub = xstrndup(val + i, j - i);
        int m = fnmatch(pat, sub, 0) == 0;
        free(sub);
        if (m) { *len = j - i; return 1; }
    }
    return 0;
}

static char *pat_subst(const char *val, const char *pat, const char *repl,
                       int all, int anchor)
{
    sbuf out;
    sb_init(&out);
    size_t n = strlen(val);
    size_t mlen = 0;

    if (anchor == 1) {
        if (match_at(val, 0, pat, &mlen)) {
            sb_puts(&out, repl, 0);
            sb_puts(&out, val + mlen, 0);
        } else sb_puts(&out, val, 0);
    } else if (anchor == 2) {
        size_t hit = (size_t)-1;
        for (size_t i = 0; i <= n; i++)
            if (fnmatch(pat, val + i, 0) == 0) { hit = i; break; }
        if (hit != (size_t)-1) {
            for (size_t i = 0; i < hit; i++) sb_putc(&out, val[i], 0);
            sb_puts(&out, repl, 0);
        } else sb_puts(&out, val, 0);
    } else {
        size_t i = 0;
        int done = 0;
        while (i < n) {
            if (!done && match_at(val, i, pat, &mlen) && mlen > 0) {
                sb_puts(&out, repl, 0);
                i += mlen;
                if (!all) done = 1;
                continue;
            }
            sb_putc(&out, val[i], 0);
            i++;
        }
        if (!done && n == 0 && match_at(val, 0, pat, &mlen)) sb_puts(&out, repl, 0);
    }

    char *r = out.p ? out.p : xstrdup("");
    out.p = NULL;
    sb_free(&out);
    return r;
}

#define EXP_SPLIT   1
#define EXP_HEREDOC 2

static void expand_into(sbuf *out, const char *w, int flags, svec *fields);
static char *unescape(const char *s);

static char *expand_pattern(const char *w)
{
    sbuf b;
    sb_init(&b);
    expand_into(&b, w, 0, NULL);
    char *s = b.p ? b.p : xstrdup("");
    b.p = NULL;
    sb_free(&b);
    return s;
}

static char *expand_to_string(const char *w)
{
    char *pat = expand_pattern(w);
    char *plain = unescape(pat);
    free(pat);
    return plain;
}

static char *expand_heredoc(const char *w)
{
    sbuf b;
    sb_init(&b);
    expand_into(&b, w, EXP_HEREDOC, NULL);
    char *s = b.p ? b.p : xstrdup("");
    b.p = NULL;
    sb_free(&b);
    char *plain = unescape(s);
    free(s);
    return plain;
}

static void put_ch(sbuf *b, char c, int quoted)
{
    if (c == '\\') { sb_putc(b, '\\', (char)quoted); sb_putc(b, '\\', (char)quoted); return; }
    if (quoted && (c == '*' || c == '?' || c == '[')) sb_putc(b, '\\', 1);
    sb_putc(b, c, (char)quoted);
}

static const char *ifs_chars(void)
{
    const char *v = var_get("IFS");
    return v ? v : " \t\n";
}

static void expand_param(sbuf *out, const char *spec, int quoted, svec *fields, int *started);

static void append_split(sbuf *out, const char *s, int quoted, svec *fields, int *started)
{
    if (quoted || !fields) {
        while (*s) put_ch(out, *s++, quoted ? 1 : 0);
        return;
    }
    const char *ifs = ifs_chars();
    while (*s) {
        if (strchr(ifs, *s)) {
            if (out->n > 0 || *started) {
                sv_push(fields, xstrndup(out->p ? out->p : "", out->n));
                out->n = 0;
                if (out->p) out->p[0] = 0;
                *started = 0;
            }
            while (*s && strchr(ifs, *s)) s++;
            continue;
        }
        put_ch(out, *s, 0);
        s++;
    }
}

static void expand_dollar(sbuf *out, const char **pp, int in_dq, svec *fields, int *started)
{
    const char *p = *pp;
    p++;
    if (!*p) { sb_putc(out, '$', in_dq); *pp = p; return; }

    if (*p == '(' && p[1] == '(') {
        const char *s = p + 2;
        int depth = 1;
        const char *e = s;
        while (*e) {
            if (*e == '(') depth++;
            else if (*e == ')') {
                if (e[1] == ')' && depth == 1) break;
                depth--;
            }
            e++;
        }
        char *expr = xstrndup(s, (size_t)(e - s));
        char *ex = expand_to_string(expr);
        long v = arith_eval(ex);
        free(ex);
        free(expr);
        char nb[32];
        snprintf(nb, sizeof nb, "%ld", v);
        append_split(out, nb, in_dq, fields, started);
        *pp = *e ? e + 2 : e;
        return;
    }

    if (*p == '(') {
        const char *s = p + 1;
        int depth = 1;
        const char *e = s;
        while (*e) {
            if (*e == '(') depth++;
            else if (*e == ')') { depth--; if (!depth) break; }
            e++;
        }
        char *script = xstrndup(s, (size_t)(e - s));
        int st = 0;
        char *res = capture(script, &st);
        g_status = st;
        free(script);
        append_split(out, res, in_dq, fields, started);
        free(res);
        *pp = *e ? e + 1 : e;
        return;
    }

    if (*p == '{') {
        const char *s = p + 1;
        int depth = 1;
        const char *e = s;
        while (*e) {
            if (*e == '{') depth++;
            else if (*e == '}') { depth--; if (!depth) break; }
            e++;
        }
        char *spec = xstrndup(s, (size_t)(e - s));
        expand_param(out, spec, in_dq, fields, started);
        free(spec);
        *pp = *e ? e + 1 : e;
        return;
    }

    char name[128];
    size_t k = 0;
    if (isdigit((unsigned char)*p)) {
        name[k++] = *p++;
    } else if (is_name_char((unsigned char)*p)) {
        while (is_name_char((unsigned char)*p) && k < sizeof name - 1) name[k++] = *p++;
    } else if (strchr("?#$!*@-", *p)) {
        name[k++] = *p++;
    } else {
        sb_putc(out, '$', in_dq);
        *pp = p;
        return;
    }
    name[k] = 0;
    expand_param(out, name, in_dq, fields, started);
    *pp = p;
}

static void expand_param(sbuf *out, const char *spec, int in_dq, svec *fields, int *started)
{
    char nb[64];

    if (spec[0] == '#' && spec[1]) {
        char *sub = xstrdup(spec + 1);
        sbuf t;
        sb_init(&t);
        int st = 0;
        expand_param(&t, sub, 1, NULL, &st);
        char *plain = unescape(t.p ? t.p : "");
        snprintf(nb, sizeof nb, "%zu", strlen(plain));
        free(plain);
        sb_free(&t);
        free(sub);
        append_split(out, nb, in_dq, fields, started);
        return;
    }

    size_t nlen = 0;
    while (spec[nlen] && (is_name_char((unsigned char)spec[nlen]) ||
                          (nlen == 0 && strchr("?#$!*@-", spec[nlen])))) {
        if (nlen == 0 && strchr("?#$!*@-", spec[0])) { nlen = 1; break; }
        nlen++;
    }
    char *name = xstrndup(spec, nlen);
    const char *op = spec + nlen;

    const char *val = NULL;
    char numbuf[32];

    if (!strcmp(name, "?")) { snprintf(numbuf, sizeof numbuf, "%d", g_status); val = numbuf; }
    else if (!strcmp(name, "$")) { snprintf(numbuf, sizeof numbuf, "%d", (int)g_shell_pid); val = numbuf; }
    else if (!strcmp(name, "!")) { snprintf(numbuf, sizeof numbuf, "%d", g_last_bg); val = numbuf; }
    else if (!strcmp(name, "LINENO") && !var_get("LINENO")) { snprintf(numbuf, sizeof numbuf, "%d", g_lineno); val = numbuf; }
    else if (!strcmp(name, "#")) { snprintf(numbuf, sizeof numbuf, "%d", g_argv.n); val = numbuf; }
    else if (!strcmp(name, "-")) {
        size_t o = 0;
        if (g_opt_e) numbuf[o++] = 'e';
        if (g_opt_u) numbuf[o++] = 'u';
        if (g_opt_x) numbuf[o++] = 'x';
        numbuf[o] = 0;
        val = numbuf;
    }
    else if (!strcmp(name, "@") || !strcmp(name, "*")) {
        int star = (name[0] == '*');
        if (in_dq && !star) {
            for (int i = 0; i < g_argv.n; i++) {
                if (i > 0 && fields) {
                    sv_push(fields, xstrndup(out->p ? out->p : "", out->n));
                    out->n = 0;
                    if (out->p) out->p[0] = 0;
                }
                for (const char *q = g_argv.v[i]; *q; q++) put_ch(out, *q, 1);
                *started = 1;
            }
            free(name);
            return;
        }
        sbuf j;
        sb_init(&j);
        const char *ifs = ifs_chars();
        char sep = (star && in_dq) ? (ifs[0] ? ifs[0] : ' ') : ' ';
        for (int i = 0; i < g_argv.n; i++) {
            if (i) sb_putc(&j, sep, 0);
            sb_puts(&j, g_argv.v[i], 0);
        }
        append_split(out, j.p ? j.p : "", in_dq, fields, started);
        sb_free(&j);
        free(name);
        return;
    }
    else if (isdigit((unsigned char)name[0])) val = positional(atoi(name));
    else val = var_get(name);

    if (*op) {
        int colon = (*op == ':');
        const char *o2 = colon ? op + 1 : op;
        char c = *o2;
        const char *arg = o2 + 1;
        int unset_or_null = (!val || (colon && !*val));

        if (c == '/') {
            int all = 0, anchor = 0;
            const char *a = arg;
            if (*a == '/') { all = 1; a++; }
            else if (*a == '#') { anchor = 1; a++; }
            else if (*a == '%') { anchor = 2; a++; }
            const char *slash = NULL;
            for (const char *t = a; *t; t++) {
                if (*t == '\\' && t[1]) { t++; continue; }
                if (*t == '/') { slash = t; break; }
            }
            char *patw  = slash ? xstrndup(a, (size_t)(slash - a)) : xstrdup(a);
            char *replw = slash ? xstrdup(slash + 1) : xstrdup("");
            char *pat   = expand_pattern(patw);
            char *rep   = expand_to_string(replw);
            char *res   = pat_subst(val ? val : "", pat, rep, all, anchor);
            append_split(out, res, in_dq, fields, started);
            free(res); free(rep); free(pat); free(replw); free(patw);
            free(name);
            return;
        }

        if (colon && !strchr("-=?+", c)) {
            const char *q = o2;
            long off = strtol(q, (char **)&q, 10);
            long cnt = -1;
            if (*q == ':') { q++; cnt = strtol(q, NULL, 10); }
            const char *v = val ? val : "";
            long vlen = (long)strlen(v);
            if (off < 0) { off += vlen; if (off < 0) off = 0; }
            if (off > vlen) off = vlen;
            if (cnt < 0 || off + cnt > vlen) cnt = vlen - off;
            char *res = xstrndup(v + off, (size_t)cnt);
            append_split(out, res, in_dq, fields, started);
            free(res);
            free(name);
            return;
        }

        if (c == '-') {
            if (unset_or_null) {
                char *w = expand_to_string(arg);
                append_split(out, w, in_dq, fields, started);
                free(w);
            } else append_split(out, val, in_dq, fields, started);
            free(name);
            return;
        }
        if (c == '=') {
            if (unset_or_null) {
                char *w = expand_to_string(arg);
                var_set(name, w);
                append_split(out, w, in_dq, fields, started);
                free(w);
            } else append_split(out, val, in_dq, fields, started);
            free(name);
            return;
        }
        if (c == '+') {
            if (!unset_or_null) {
                char *w = expand_to_string(arg);
                append_split(out, w, in_dq, fields, started);
                free(w);
            }
            free(name);
            return;
        }
        if (c == '?') {
            if (unset_or_null) {
                char *w = expand_to_string(arg);
                fprintf(stderr, "%s: %s: %s\n", g_shname, name,
                        *w ? w : "parameter not set");
                free(w);
                free(name);
                if (!g_interactive) { g_exiting = 1; g_exit_code = 1; }
                g_status = 1;
                return;
            }
            append_split(out, val, in_dq, fields, started);
            free(name);
            return;
        }
        if (c == '#' || c == '%') {
            int greedy = (*arg == c);
            const char *pat = greedy ? arg + 1 : arg;
            char *p2 = expand_pattern(pat);
            char *res = (c == '%') ? trim_suffix(val ? val : "", p2, greedy)
                                   : trim_prefix(val ? val : "", p2, greedy);
            append_split(out, res, in_dq, fields, started);
            free(res);
            free(p2);
            free(name);
            return;
        }
    }

    if (!val) {
        if (g_opt_u && !isdigit((unsigned char)name[0]) && !strchr("?#$!*@-", name[0])) {
            fprintf(stderr, "%s: %s: parameter not set\n", g_shname, name);
            g_status = 1;
            if (!g_interactive) { g_exiting = 1; g_exit_code = 1; }
        }
        free(name);
        return;
    }
    append_split(out, val, in_dq, fields, started);
    free(name);
}

static void expand_into(sbuf *out, const char *w, int flags, svec *fields)
{
    int started = 0;
    const char *p = w;
    int heredoc = (flags & EXP_HEREDOC) != 0;
    int dq = heredoc;

    while (*p) {
        char c = *p;
        if (c == '\'' && !dq && !heredoc) {
            started = 1;
            p++;
            while (*p && *p != '\'') { put_ch(out, *p, 1); p++; }
            if (*p) p++;
            continue;
        }
        if (c == '"' && !heredoc) {
            dq = !dq;
            started = 1;
            p++;
            continue;
        }
        if (c == '\\') {
            if (dq) {
                char nx = p[1];
                if (nx == '\n') { p += 2; continue; }
                if (nx == '$' || nx == '`' || nx == '\\' || (nx == '"' && !heredoc)) {
                    put_ch(out, nx, 1);
                    p += 2;
                    continue;
                }
                put_ch(out, '\\', 1);
                p++;
                continue;
            }
            if (p[1]) { put_ch(out, p[1], 1); started = 1; p += 2; continue; }
            p++;
            continue;
        }
        if (c == '`') {
            p++;
            sbuf s;
            sb_init(&s);
            while (*p && *p != '`') {
                if (*p == '\\' && (p[1] == '`' || p[1] == '\\' || p[1] == '$')) p++;
                sb_putc(&s, *p, 0);
                p++;
            }
            if (*p) p++;
            int st = 0;
            char *res = capture(s.p ? s.p : "", &st);
            g_status = st;
            sb_free(&s);
            append_split(out, res, dq, fields, &started);
            free(res);
            continue;
        }
        if (c == '$') {
            expand_dollar(out, &p, dq, fields, &started);
            continue;
        }
        if (c == '~' && !dq && !heredoc && !started && out->n == 0 &&
            (p[1] == 0 || p[1] == '/')) {
            const char *home = var_get("HOME");
            if (!home || !*home) home = "/";
            for (const char *q = home; *q; q++) put_ch(out, *q, 1);
            started = 1;
            p++;
            continue;
        }
        if (!dq && fields && strchr(ifs_chars(), c)) {
            if (out->n > 0 || started) {
                sv_push(fields, xstrndup(out->p ? out->p : "", out->n));
                out->n = 0;
                if (out->p) out->p[0] = 0;
                started = 0;
            }
            p++;
            continue;
        }
        put_ch(out, c, dq ? 1 : 0);
        started = 1;
        p++;
    }
    if (fields && (out->n > 0 || started)) {
        sv_push(fields, xstrndup(out->p ? out->p : "", out->n));
        out->n = 0;
        if (out->p) out->p[0] = 0;
    }
}

static int has_glob(const char *s)
{
    int esc = 0;
    for (const char *p = s; *p; p++) {
        if (esc) { esc = 0; continue; }
        if (*p == '\\') { esc = 1; continue; }
        if (*p == '*' || *p == '?' || *p == '[') return 1;
    }
    return 0;
}

static void glob_walk(const char *base, char **parts, int idx, int nparts, svec *out)
{
    char path[1024];
    if (idx >= nparts) {
        struct stat st;
        if (stat(base, &st) == 0) sv_push(out, xstrdup(base));
        return;
    }

    const char *part = parts[idx];
    if (!has_glob(part)) {
        if (!strcmp(base, "/")) snprintf(path, sizeof path, "/%s", part);
        else if (!*base)       snprintf(path, sizeof path, "%s", part);
        else                   snprintf(path, sizeof path, "%s/%s", base, part);
        glob_walk(path, parts, idx + 1, nparts, out);
        return;
    }

    const char *dir = *base ? base : ".";
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    svec found;
    sv_init(&found);
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (de->d_name[0] == '.' && part[0] != '.') continue;
        if (fnmatch(part, de->d_name, 0) != 0) continue;
        sv_push(&found, xstrdup(de->d_name));
    }
    closedir(d);

    for (int i = 0; i < found.n; i++)
        for (int j = i + 1; j < found.n; j++)
            if (strcmp(found.v[i], found.v[j]) > 0) {
                char *t = found.v[i]; found.v[i] = found.v[j]; found.v[j] = t;
            }

    for (int i = 0; i < found.n; i++) {
        if (!strcmp(base, "/")) snprintf(path, sizeof path, "/%s", found.v[i]);
        else if (!*base)       snprintf(path, sizeof path, "%s", found.v[i]);
        else                   snprintf(path, sizeof path, "%s/%s", base, found.v[i]);
        glob_walk(path, parts, idx + 1, nparts, out);
    }
    sv_free(&found);
}

static char *unescape(const char *s)
{
    char *o = xmalloc(strlen(s) + 1);
    size_t k = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '\\' && p[1]) { o[k++] = *++p; continue; }
        o[k++] = *p;
    }
    o[k] = 0;
    return o;
}

static void glob_field(const char *field, svec *out)
{
    if (!has_glob(field)) { sv_push(out, unescape(field)); return; }

    svec parts;
    sv_init(&parts);
    const char *p = field;
    int absolute = (*p == '/');
    while (*p == '/') p++;
    while (*p) {
        const char *s = p;
        while (*p && *p != '/') {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
        sv_push(&parts, xstrndup(s, (size_t)(p - s)));
        while (*p == '/') p++;
    }

    svec hits;
    sv_init(&hits);
    glob_walk(absolute ? "/" : "", parts.v, 0, parts.n, &hits);
    sv_free(&parts);

    if (hits.n == 0) {
        sv_free(&hits);
        sv_push(out, unescape(field));
        return;
    }
    for (int i = 0; i < hits.n; i++) sv_push(out, hits.v[i]);
    free(hits.v);
}

static void expand_word_list(svec *in, svec *out)
{
    for (int i = 0; i < in->n; i++) {
        svec fields;
        sv_init(&fields);
        sbuf b;
        sb_init(&b);
        expand_into(&b, in->v[i], EXP_SPLIT, &fields);
        sb_free(&b);
        for (int k = 0; k < fields.n; k++) glob_field(fields.v[k], out);
        sv_free(&fields);
    }
}

typedef struct {
    int saved[16];
    int fds[16];
    int n;
} fdsave_t;

static int open_redir_target(redir_t *r, char **err)
{
    *err = NULL;
    if (r->type == R_HEREDOC) return -1;

    svec fields;
    sv_init(&fields);
    sbuf b;
    sb_init(&b);
    expand_into(&b, r->word, EXP_SPLIT, &fields);
    sb_free(&b);
    char *target = fields.n ? unescape(fields.v[0]) : xstrdup("");
    sv_free(&fields);

    int fd = -1;
    switch (r->type) {
        case R_IN:     fd = open(target, O_RDONLY, 0); break;
        case R_OUT:    fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644); break;
        case R_APPEND: fd = open(target, O_WRONLY | O_CREAT | O_APPEND, 0644); break;
        case R_RW:     fd = open(target, O_RDWR | O_CREAT, 0644); break;
        case R_DUPIN:
        case R_DUPOUT:
            if (!strcmp(target, "-")) { free(target); return -2; }
            fd = (int)strtol(target, NULL, 10);
            free(target);
            return fd >= 0 ? -3 - fd : -1;
    }
    if (fd < 0) *err = target;
    else free(target);
    return fd;
}

static int apply_redirs(redir_t *r, fdsave_t *save)
{
    if (save) save->n = 0;
    for (; r; r = r->next) {
        int newfd;
        if (r->type == R_HEREDOC) {
            int pfd[2];
            if (pipe(pfd) < 0) return -1;
            const char *body = r->here ? r->here : "";
            char *expanded = NULL;
            if (!r->here_raw) {
                expanded = expand_heredoc(body);
                body = expanded;
            }
            size_t blen = strlen(body);
            pid_t pid = fork();
            if (pid == 0) {
                close(pfd[0]);
                size_t off = 0;
                while (off < blen) {
                    ssize_t w = write(pfd[1], body + off, blen - off);
                    if (w <= 0) break;
                    off += (size_t)w;
                }
                close(pfd[1]);
                _exit(0);
            }
            free(expanded);
            close(pfd[1]);
            newfd = pfd[0];
        } else {
            char *err = NULL;
            int got = open_redir_target(r, &err);
            if (got == -2) {
                if (save && save->n < 16) {
                    save->fds[save->n] = r->fd;
                    save->saved[save->n] = dup(r->fd);
                    save->n++;
                }
                close(r->fd);
                continue;
            }
            if (got <= -3) {
                newfd = -3 - got;
                if (save && save->n < 16) {
                    save->fds[save->n] = r->fd;
                    save->saved[save->n] = dup(r->fd);
                    save->n++;
                }
                if (newfd != r->fd) dup2(newfd, r->fd);
                continue;
            }
            if (got < 0) {
                fprintf(stderr, "%s: %s: cannot open\n", g_shname, err ? err : "");
                free(err);
                return -1;
            }
            newfd = got;
        }
        if (save && save->n < 16) {
            save->fds[save->n] = r->fd;
            save->saved[save->n] = dup(r->fd);
            save->n++;
        }
        if (newfd != r->fd) {
            dup2(newfd, r->fd);
            close(newfd);
        }
    }
    return 0;
}

static void restore_redirs(fdsave_t *save)
{
    for (int i = save->n - 1; i >= 0; i--) {
        if (save->saved[i] >= 0) {
            dup2(save->saved[i], save->fds[i]);
            close(save->saved[i]);
        }
    }
    save->n = 0;
}

static int test_main(int argc, char **argv);

static int bi_cd(int argc, char **argv)
{
    const char *dir;
    int i = 1;
    while (i < argc && !strcmp(argv[i], "--")) i++;
    if (i < argc && argv[i][0]) dir = argv[i];
    else {
        dir = var_get("HOME");
        if (!dir || !*dir) dir = "/";
    }
    if (!strcmp(dir, "-")) {
        const char *old = var_get("OLDPWD");
        if (!old) { fprintf(stderr, "%s: cd: OLDPWD not set\n", g_shname); return 1; }
        dir = old;
        printf("%s\n", dir);
    }
    char cwd[1024];
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    if (chdir(dir) != 0) {
        fprintf(stderr, "%s: cd: %s: no such directory\n", g_shname, dir);
        return 1;
    }
    if (cwd[0]) var_set("OLDPWD", cwd);
    if (getcwd(cwd, sizeof cwd)) {
        var_set("PWD", cwd);
        var_export("PWD");
    }
    return 0;
}

static int bi_echo(int argc, char **argv)
{
    int i = 1, nl = 1, esc = 0;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        int ok = 1;
        for (const char *p = argv[i] + 1; *p; p++)
            if (*p != 'n' && *p != 'e' && *p != 'E') { ok = 0; break; }
        if (!ok) break;
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'n') nl = 0;
            else if (*p == 'e') esc = 1;
            else if (*p == 'E') esc = 0;
        }
        i++;
    }
    for (; i < argc; i++) {
        if (!esc) fputs(argv[i], stdout);
        else {
            for (const char *p = argv[i]; *p; p++) {
                if (*p != '\\' || !p[1]) { putchar(*p); continue; }
                p++;
                switch (*p) {
                    case 'n': putchar('\n'); break;
                    case 't': putchar('\t'); break;
                    case 'r': putchar('\r'); break;
                    case '\\': putchar('\\'); break;
                    case 'a': putchar('\a'); break;
                    case 'b': putchar('\b'); break;
                    case '0': putchar('\0'); break;
                    default: putchar('\\'); putchar(*p); break;
                }
            }
        }
        if (i + 1 < argc) putchar(' ');
    }
    if (nl) putchar('\n');
    fflush(stdout);
    return 0;
}

static int bi_read(int argc, char **argv)
{
    int raw = 0, i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] == 'r' && !argv[i][2]) { raw = 1; i++; }

    sbuf line;
    sb_init(&line);
    char c;
    int got = 0;
    for (;;) {
        ssize_t r = read(0, &c, 1);
        if (r <= 0) break;
        got = 1;
        if (c == '\n') break;
        if (!raw && c == '\\') {
            char nx;
            if (read(0, &nx, 1) == 1) {
                if (nx == '\n') continue;
                sb_putc(&line, nx, 0);
                continue;
            }
            break;
        }
        sb_putc(&line, c, 0);
    }
    if (!got && line.n == 0) { sb_free(&line); return 1; }

    const char *ifs = ifs_chars();
    const char *s = line.p ? line.p : "";
    int nnames = argc - i;
    if (nnames <= 0) {
        var_set("REPLY", s);
        sb_free(&line);
        return 0;
    }
    for (int k = 0; k < nnames; k++) {
        while (*s && strchr(ifs, *s)) s++;
        if (k == nnames - 1) {
            const char *end = s + strlen(s);
            while (end > s && strchr(ifs, end[-1])) end--;
            char *v = xstrndup(s, (size_t)(end - s));
            var_set(argv[i + k], v);
            free(v);
            s = end;
            break;
        }
        const char *start = s;
        while (*s && !strchr(ifs, *s)) s++;
        char *v = xstrndup(start, (size_t)(s - start));
        var_set(argv[i + k], v);
        free(v);
    }
    sb_free(&line);
    return 0;
}

static int path_find(const char *cmd, char *out, size_t outsz)
{
    if (strchr(cmd, '/')) {
        if (access(cmd, X_OK) == 0) { snprintf(out, outsz, "%s", cmd); return 1; }
        return 0;
    }
    const char *path = var_get("PATH");
    if (!path || !*path) path = "/bin:/apps:/usr/bin";
    const char *p = path;
    while (*p) {
        const char *e = strchr(p, ':');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == 0) { snprintf(out, outsz, "./%s", cmd); }
        else snprintf(out, outsz, "%.*s/%s", (int)len, p, cmd);
        if (access(out, X_OK) == 0) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static int is_builtin(const char *name);

static int bi_command(int argc, char **argv, int *handled);

static int bi_type(int argc, char **argv)
{
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (func_find(argv[i])) { printf("%s is a function\n", argv[i]); continue; }
        if (is_builtin(argv[i])) { printf("%s is a shell builtin\n", argv[i]); continue; }
        char full[1024];
        if (path_find(argv[i], full, sizeof full)) printf("%s is %s\n", argv[i], full);
        else { fprintf(stderr, "%s: not found\n", argv[i]); rc = 1; }
    }
    return rc;
}

static void set_positional(char **argv, int n)
{
    sv_free(&g_argv);
    sv_init(&g_argv);
    for (int i = 0; i < n; i++) sv_push(&g_argv, xstrdup(argv[i]));
}

static int bi_set(int argc, char **argv)
{
    int i = 1;
    int saw_dashdash = 0;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--")) { saw_dashdash = 1; i++; break; }
        if (a[0] == '-' && a[1]) {
            for (const char *p = a + 1; *p; p++) {
                if (*p == 'e') g_opt_e = 1;
                else if (*p == 'u') g_opt_u = 1;
                else if (*p == 'x') g_opt_x = 1;
                else if (*p == 'n') g_opt_n = 1;
            }
            continue;
        }
        if (a[0] == '+' && a[1]) {
            for (const char *p = a + 1; *p; p++) {
                if (*p == 'e') g_opt_e = 0;
                else if (*p == 'u') g_opt_u = 0;
                else if (*p == 'x') g_opt_x = 0;
            }
            continue;
        }
        break;
    }
    if (i < argc || saw_dashdash) set_positional(argv + i, argc - i);
    else if (argc == 1) {
        for (int k = 0; k < g_nvars; k++)
            printf("%s=%s\n", g_vars[k].name, g_vars[k].val ? g_vars[k].val : "");
    }
    return 0;
}

typedef struct localsave {
    char *name;
    char *val;
    int   existed;
    struct localsave *next;
} localsave_t;

static localsave_t *g_locals;

static int bi_local(int argc, char **argv)
{
    if (g_func_depth == 0) {
        fprintf(stderr, "%s: local: not in a function\n", g_shname);
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        char *name = eq ? xstrndup(argv[i], (size_t)(eq - argv[i])) : xstrdup(argv[i]);
        localsave_t *ls = xmalloc(sizeof(localsave_t));
        ls->name = xstrdup(name);
        int vi = var_index(name);
        ls->existed = vi >= 0;
        ls->val = (vi >= 0 && g_vars[vi].val) ? xstrdup(g_vars[vi].val) : NULL;
        ls->next = g_locals;
        g_locals = ls;
        var_set(name, eq ? eq + 1 : "");
        free(name);
    }
    return 0;
}

static int run_builtin(int argc, char **argv, int *is_bi);

static int bi_dot(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "%s: .: needs a file\n", g_shname); return 1; }
    char full[1024];
    if (!strchr(argv[1], '/')) {
        if (!path_find(argv[1], full, sizeof full)) snprintf(full, sizeof full, "%s", argv[1]);
    } else snprintf(full, sizeof full, "%s", argv[1]);

    int fd = open(full, O_RDONLY, 0);
    if (fd < 0) {
        fprintf(stderr, "%s: %s: cannot open\n", g_shname, argv[1]);
        return 1;
    }
    sbuf b;
    sb_init(&b);
    char tmp[4096];
    ssize_t r;
    while ((r = read(fd, tmp, sizeof tmp)) > 0)
        for (ssize_t i = 0; i < r; i++) sb_putc(&b, tmp[i], 0);
    close(fd);

    svec saved;
    sv_init(&saved);
    int had_args = argc > 2;
    if (had_args) {
        for (int i = 0; i < g_argv.n; i++) sv_push(&saved, xstrdup(g_argv.v[i]));
        set_positional(argv + 2, argc - 2);
    }

    int rc = run_string(b.p ? b.p : "");
    sb_free(&b);

    if (had_args) {
        sv_free(&g_argv);
        g_argv = saved;
    }
    if (g_return) g_return = 0;
    return rc;
}

static int is_builtin(const char *name)
{
    static const char *const B[] = {
        ":", ".", "source", "break", "cd", "continue", "echo", "eval", "exec",
        "exit", "export", "false", "hash", "local", "pwd", "read", "return",
        "set", "shift", "test", "[", "times", "trap", "true", "type", "umask",
        "unset", "command", "getopts", NULL
    };
    for (int i = 0; B[i]; i++) if (!strcmp(name, B[i])) return 1;
    return 0;
}

static int exec_external(char **argv, redir_t *redirs, svec *assigns, int fork_it);

static int bi_command(int argc, char **argv, int *handled)
{
    int i = 1;
    int show = 0, show_v = 0;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (!strcmp(argv[i], "-v")) { show = 1; show_v = 1; i++; continue; }
        if (!strcmp(argv[i], "-V")) { show = 1; i++; continue; }
        if (!strcmp(argv[i], "-p")) { i++; continue; }
        break;
    }
    if (show) {
        int rc = 0;
        for (; i < argc; i++) {
            if (func_find(argv[i]) || is_builtin(argv[i])) {
                if (show_v) printf("%s\n", argv[i]);
                else printf("%s is a shell builtin\n", argv[i]);
                continue;
            }
            char full[1024];
            if (path_find(argv[i], full, sizeof full)) {
                if (show_v) printf("%s\n", full);
                else printf("%s is %s\n", argv[i], full);
            } else rc = 1;
        }
        *handled = 1;
        return rc;
    }
    if (i >= argc) { *handled = 1; return 0; }
    *handled = 0;
    return i;
}

static int bi_getopts(int argc, char **argv)
{
    if (argc < 3) return 1;
    const char *spec = argv[1];
    const char *name = argv[2];

    const char *oi = var_get("OPTIND");
    int optind_v = oi ? atoi(oi) : 1;
    const char *oo = var_get("__OPTPOS");
    int optpos = oo ? atoi(oo) : 1;
    char nb[32];

    if (optind_v > g_argv.n) return 1;
    const char *arg = g_argv.v[optind_v - 1];
    if (!arg || arg[0] != '-' || !arg[1]) return 1;
    if (!strcmp(arg, "--")) {
        snprintf(nb, sizeof nb, "%d", optind_v + 1);
        var_set("OPTIND", nb);
        return 1;
    }

    char c = arg[optpos];
    if (!c) {
        snprintf(nb, sizeof nb, "%d", optind_v + 1);
        var_set("OPTIND", nb);
        var_set("__OPTPOS", "1");
        return bi_getopts(argc, argv);
    }

    const char *f = strchr(spec, c);
    char vb[2] = { c, 0 };
    if (!f) {
        var_set(name, "?");
        var_set("OPTARG", vb);
        optpos++;
        if (!arg[optpos]) { optind_v++; optpos = 1; }
        snprintf(nb, sizeof nb, "%d", optind_v);
        var_set("OPTIND", nb);
        snprintf(nb, sizeof nb, "%d", optpos);
        var_set("__OPTPOS", nb);
        return 0;
    }
    var_set(name, vb);
    optpos++;
    if (f[1] == ':') {
        if (arg[optpos]) {
            var_set("OPTARG", arg + optpos);
            optind_v++;
        } else {
            optind_v++;
            if (optind_v <= g_argv.n) { var_set("OPTARG", g_argv.v[optind_v - 1]); optind_v++; }
            else { var_set(name, "?"); var_set("OPTARG", vb); }
        }
        optpos = 1;
    } else if (!arg[optpos]) {
        optind_v++;
        optpos = 1;
    }
    snprintf(nb, sizeof nb, "%d", optind_v);
    var_set("OPTIND", nb);
    snprintf(nb, sizeof nb, "%d", optpos);
    var_set("__OPTPOS", nb);
    return 0;
}

static int run_builtin(int argc, char **argv, int *is_bi)
{
    *is_bi = 1;
    const char *c = argv[0];

    if (!strcmp(c, ":") || !strcmp(c, "true")) return 0;
    if (!strcmp(c, "false")) return 1;
    if (!strcmp(c, "hash") || !strcmp(c, "times")) return 0;
    if (!strcmp(c, "echo")) return bi_echo(argc, argv);
    if (!strcmp(c, "cd")) return bi_cd(argc, argv);
    if (!strcmp(c, "pwd")) {
        char buf[1024];
        if (getcwd(buf, sizeof buf)) { printf("%s\n", buf); return 0; }
        return 1;
    }
    if (!strcmp(c, "read")) return bi_read(argc, argv);
    if (!strcmp(c, "test") || !strcmp(c, "[")) return test_main(argc, argv);
    if (!strcmp(c, "set")) return bi_set(argc, argv);
    if (!strcmp(c, "local")) return bi_local(argc, argv);
    if (!strcmp(c, "type")) return bi_type(argc, argv);
    if (!strcmp(c, "getopts")) return bi_getopts(argc, argv);
    if (!strcmp(c, ".") || !strcmp(c, "source")) return bi_dot(argc, argv);

    if (!strcmp(c, "umask")) {
        if (argc > 1) {
            char *end = NULL;
            long v = strtol(argv[1], &end, 8);
            if (end == argv[1] || (end && *end) || v < 0 || v > 0777) {
                fprintf(stderr, "umask: %s: invalid mask\n", argv[1]);
                return 1;
            }
            umask((mode_t)v);
            return 0;
        }
        mode_t cur = umask(0);
        umask(cur);
        printf("%04o\n", (unsigned)cur);
        return 0;
    }

    if (!strcmp(c, "trap")) {
        if (argc >= 3 && !strcmp(argv[2], "EXIT")) {
            free(g_exit_trap);
            g_exit_trap = xstrdup(argv[1]);
        }
        return 0;
    }

    if (!strcmp(c, "exit")) {
        g_exiting = 1;
        g_exit_code = (argc > 1) ? (int)strtol(argv[1], NULL, 10) : g_status;
        return g_exit_code;
    }

    if (!strcmp(c, "return")) {
        g_return = 1;
        return (argc > 1) ? (int)strtol(argv[1], NULL, 10) : g_status;
    }

    if (!strcmp(c, "break")) {
        int lv = (argc > 1) ? (int)strtol(argv[1], NULL, 10) : 1;
        if (lv < 1) lv = 1;
        g_break = lv;
        return 0;
    }

    if (!strcmp(c, "continue")) {
        int lv = (argc > 1) ? (int)strtol(argv[1], NULL, 10) : 1;
        if (lv < 1) lv = 1;
        g_continue = lv;
        return 0;
    }

    if (!strcmp(c, "shift")) {
        int nsh = (argc > 1) ? (int)strtol(argv[1], NULL, 10) : 1;
        if (nsh < 0 || nsh > g_argv.n) return 1;
        for (int i = 0; i < nsh; i++) free(g_argv.v[i]);
        memmove(g_argv.v, g_argv.v + nsh, (size_t)(g_argv.n - nsh + 1) * sizeof(char *));
        g_argv.n -= nsh;
        return 0;
    }

    if (!strcmp(c, "export")) {
        if (argc == 1) {
            for (int i = 0; i < g_nvars; i++)
                if (g_vars[i].exported)
                    printf("export %s=%s\n", g_vars[i].name, g_vars[i].val ? g_vars[i].val : "");
            return 0;
        }
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (eq) {
                char *name = xstrndup(argv[i], (size_t)(eq - argv[i]));
                var_set(name, eq + 1);
                var_export(name);
                free(name);
            } else var_export(argv[i]);
        }
        return 0;
    }

    if (!strcmp(c, "unset")) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-f")) continue;
            if (!strcmp(argv[i], "-v")) continue;
            var_unset(argv[i]);
        }
        return 0;
    }

    if (!strcmp(c, "eval")) {
        sbuf b;
        sb_init(&b);
        for (int i = 1; i < argc; i++) {
            if (i > 1) sb_putc(&b, ' ', 0);
            sb_puts(&b, argv[i], 0);
        }
        int rc = run_string(b.p ? b.p : "");
        sb_free(&b);
        return rc;
    }

    if (!strcmp(c, "exec")) {
        if (argc == 1) return 0;
        char full[1024];
        if (!path_find(argv[1], full, sizeof full)) {
            fprintf(stderr, "%s: %s: not found\n", g_shname, argv[1]);
            exit(127);
        }
        execve(full, argv + 1, environ);
        exit(127);
    }

    *is_bi = 0;
    return 0;
}

static char g_cd_drop[64];

static void cd_on_exit_setup(void)
{
    snprintf(g_cd_drop, sizeof g_cd_drop, "/tmp/.cd-on-exit.%d", (int)getpid());
    unlink(g_cd_drop);
    var_set("CD_ON_EXIT", g_cd_drop);
    var_export("CD_ON_EXIT");
}

static void cd_on_exit_take(void)
{
    if (!g_cd_drop[0]) return;
    int fd = open(g_cd_drop, O_RDONLY, 0);
    if (fd < 0) return;
    char dir[1024];
    ssize_t n = read(fd, dir, sizeof dir - 1);
    close(fd);
    unlink(g_cd_drop);
    if (n <= 0) return;
    dir[n] = 0;
    dir[strcspn(dir, "\n")] = 0;
    if (!dir[0]) return;
    if (chdir(dir) == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof cwd)) { var_set("PWD", cwd); var_export("PWD"); }
    }
}

static int exec_external(char **argv, redir_t *redirs, svec *assigns, int fork_it)
{
    char full[1024];
    if (!path_find(argv[0], full, sizeof full)) {
        fprintf(stderr, "%s: %s: not found\n", g_shname, argv[0]);
        return 127;
    }

    if (!fork_it) {
        if (redirs && apply_redirs(redirs, NULL) < 0) _exit(1);
        if (assigns)
            for (int i = 0; i < assigns->n; i++) {
                char *eq = strchr(assigns->v[i], '=');
                if (!eq) continue;
                char *name = xstrndup(assigns->v[i], (size_t)(eq - assigns->v[i]));
                setenv(name, eq + 1, 1);
                free(name);
            }
        execve(full, argv, environ);
        fprintf(stderr, "%s: %s: cannot run\n", g_shname, argv[0]);
        _exit(126);
    }

    pid_t pid = fork();
    if (pid < 0) { fprintf(stderr, "%s: cannot fork\n", g_shname); return 1; }
    if (pid == 0) {
        if (redirs && apply_redirs(redirs, NULL) < 0) _exit(1);
        if (assigns)
            for (int i = 0; i < assigns->n; i++) {
                char *eq = strchr(assigns->v[i], '=');
                if (!eq) continue;
                char *name = xstrndup(assigns->v[i], (size_t)(eq - assigns->v[i]));
                setenv(name, eq + 1, 1);
                free(name);
            }
        execve(full, argv, environ);
        fprintf(stderr, "%s: %s: cannot run\n", g_shname, argv[0]);
        _exit(126);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    cd_on_exit_take();
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

static int exec_function(func_t *f, svec *args)
{
    svec saved;
    sv_init(&saved);
    for (int i = 0; i < g_argv.n; i++) sv_push(&saved, xstrdup(g_argv.v[i]));
    set_positional(args->v + 1, args->n - 1);

    localsave_t *mark = g_locals;
    g_func_depth++;
    int rc = exec_node(f->body);
    g_func_depth--;

    while (g_locals != mark) {
        localsave_t *ls = g_locals;
        g_locals = ls->next;
        if (ls->existed) var_set(ls->name, ls->val ? ls->val : "");
        else var_unset(ls->name);
        free(ls->name);
        free(ls->val);
        free(ls);
    }

    sv_free(&g_argv);
    g_argv = saved;
    if (g_return) { g_return = 0; }
    return rc;
}

static void trace_cmd(svec *w)
{
    if (!g_opt_x) return;
    fputs("+", stderr);
    for (int i = 0; i < w->n; i++) fprintf(stderr, " %s", w->v[i]);
    fputc('\n', stderr);
}

static int exec_simple(node_t *n, int in_child)
{
    svec words;
    sv_init(&words);
    expand_word_list(&n->words, &words);

    if (words.n == 0) {
        int rc = 0;
        g_status = 0;
        for (int i = 0; i < n->assigns.n; i++) {
            char *eq = strchr(n->assigns.v[i], '=');
            if (!eq) continue;
            char *name = xstrndup(n->assigns.v[i], (size_t)(eq - n->assigns.v[i]));
            char *val = expand_to_string(eq + 1);
            var_set(name, val);
            free(val);
            free(name);
        }
        if (n->redirs) {
            fdsave_t save;
            if (apply_redirs(n->redirs, &save) == 0) restore_redirs(&save);
            else rc = 1;
        }
        if (!rc) rc = g_status;
        sv_free(&words);
        return rc;
    }

    trace_cmd(&words);

    svec assigns;
    sv_init(&assigns);
    for (int i = 0; i < n->assigns.n; i++) {
        char *eq = strchr(n->assigns.v[i], '=');
        if (!eq) continue;
        char *name = xstrndup(n->assigns.v[i], (size_t)(eq - n->assigns.v[i]));
        char *val = expand_to_string(eq + 1);
        size_t len = strlen(name) + strlen(val) + 2;
        char *pair = xmalloc(len);
        snprintf(pair, len, "%s=%s", name, val);
        sv_push(&assigns, pair);
        free(val);
        free(name);
    }

    char **av = words.v;
    int ac = words.n;

    if (!strcmp(av[0], "command")) {
        fdsave_t csave;
        if (apply_redirs(n->redirs, &csave) < 0) {
            sv_free(&words);
            sv_free(&assigns);
            return 1;
        }
        int handled = 0;
        int skip = bi_command(ac, av, &handled);
        if (handled) {
            int rc = skip;
            fflush(NULL);
            restore_redirs(&csave);
            sv_free(&words);
            sv_free(&assigns);
            return rc;
        }
        restore_redirs(&csave);
        av += skip;
        ac -= skip;
        if (ac <= 0) { sv_free(&words); sv_free(&assigns); return 0; }
        func_t *dummy = NULL;
        (void)dummy;
        fdsave_t save;
        int rc;
        if (apply_redirs(n->redirs, &save) < 0) rc = 1;
        else {
            rc = exec_external(av, NULL, &assigns, !in_child);
            restore_redirs(&save);
        }
        sv_free(&words);
        sv_free(&assigns);
        return rc;
    }

    func_t *fn = func_find(av[0]);
    if (fn) {
        fdsave_t save;
        int rc;
        if (apply_redirs(n->redirs, &save) < 0) rc = 1;
        else {
            svec argsv;
            sv_init(&argsv);
            for (int i = 0; i < ac; i++) sv_push(&argsv, xstrdup(av[i]));
            rc = exec_function(fn, &argsv);
            sv_free(&argsv);
            restore_redirs(&save);
        }
        sv_free(&words);
        sv_free(&assigns);
        return rc;
    }

    if (is_builtin(av[0])) {
        fdsave_t save;
        int rc;
        if (apply_redirs(n->redirs, &save) < 0) rc = 1;
        else {
            int flush_needed = 1;
            (void)flush_needed;
            int nsave = g_nvars;
            (void)nsave;
            svec oldvals;
            sv_init(&oldvals);
            for (int i = 0; i < assigns.n; i++) {
                char *eq = strchr(assigns.v[i], '=');
                char *name = xstrndup(assigns.v[i], (size_t)(eq - assigns.v[i]));
                const char *ov = var_get(name);
                sv_push(&oldvals, ov ? xstrdup(ov) : NULL);
                var_set(name, eq + 1);
                free(name);
            }
            int is_bi = 0;
            rc = run_builtin(ac, av, &is_bi);
            fflush(NULL);
            sv_free(&oldvals);
            restore_redirs(&save);
        }
        sv_free(&words);
        sv_free(&assigns);
        return rc;
    }

    int rc = exec_external(av, n->redirs, &assigns, !in_child);
    sv_free(&words);
    sv_free(&assigns);
    return rc;
}

static int exec_pipeline(node_t *n)
{
    int nk = n->nkids;
    int prev_read = -1;
    pid_t pids[32];
    if (nk > 32) nk = 32;

    for (int i = 0; i < nk; i++) {
        int pfd[2] = { -1, -1 };
        if (i + 1 < nk && pipe(pfd) < 0) { fprintf(stderr, "%s: cannot pipe\n", g_shname); break; }

        pid_t pid = fork();
        if (pid < 0) { fprintf(stderr, "%s: cannot fork\n", g_shname); break; }
        if (pid == 0) {
            if (prev_read >= 0) { dup2(prev_read, 0); close(prev_read); }
            if (pfd[1] >= 0) { close(pfd[0]); dup2(pfd[1], 1); close(pfd[1]); }
            int rc = exec_node(n->kids[i]);
            fflush(NULL);
            _exit(rc);
        }
        pids[i] = pid;
        if (prev_read >= 0) close(prev_read);
        if (pfd[1] >= 0) close(pfd[1]);
        prev_read = pfd[0];
    }
    if (prev_read >= 0) close(prev_read);

    int rc = 0;
    for (int i = 0; i < nk; i++) {
        int st = 0;
        waitpid(pids[i], &st, 0);
        if (i == nk - 1) rc = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    }
    return rc;
}

static int exec_in_subshell(node_t *body, redir_t *redirs)
{
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        if (redirs && apply_redirs(redirs, NULL) < 0) _exit(1);
        int rc = exec_node(body);
        fflush(NULL);
        _exit(g_exiting ? g_exit_code : rc);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

static int exec_node(node_t *n)
{
    if (!n) return 0;
    if (g_exiting || g_return || g_break || g_continue) return g_status;

    switch (n->t) {
        case N_LIST: {
            int rc = exec_node(n->a);
            if (g_exiting || g_return || g_break || g_continue) return rc;
            return exec_node(n->b);
        }

        case N_AND: {
            g_cond_depth++;
            int rc = exec_node(n->a);
            g_cond_depth--;
            g_status = rc;
            if (rc != 0) return rc;
            return exec_node(n->b);
        }

        case N_OR: {
            g_cond_depth++;
            int rc = exec_node(n->a);
            g_cond_depth--;
            g_status = rc;
            if (rc == 0) return rc;
            return exec_node(n->b);
        }

        case N_BANG: {
            g_cond_depth++;
            int rc = exec_node(n->a);
            g_cond_depth--;
            return rc ? 0 : 1;
        }

        case N_PIPE: {
            if (n->bg) {
                pid_t pid = fork();
                if (pid == 0) { _exit(exec_pipeline(n)); }
                g_last_bg = (int)pid;
                return 0;
            }
            int rc = exec_pipeline(n);
            g_status = rc;
            if (g_opt_e && rc != 0 && g_cond_depth == 0) { g_exiting = 1; g_exit_code = rc; }
            return rc;
        }

        case N_SIMPLE: {
            if (n->line) g_lineno = n->line;
            if (n->bg) {
                pid_t pid = fork();
                if (pid == 0) {
                    int rc = exec_simple(n, 1);
                    fflush(NULL);
                    _exit(rc);
                }
                g_last_bg = (int)pid;
                return 0;
            }
            int rc = exec_simple(n, 0);
            g_status = rc;
            if (g_opt_e && rc != 0 && g_cond_depth == 0 && !g_exiting) {
                g_exiting = 1;
                g_exit_code = rc;
            }
            return rc;
        }

        case N_SUBSHELL: {
            int rc = exec_in_subshell(n->a, n->redirs);
            g_status = rc;
            if (g_opt_e && rc != 0 && g_cond_depth == 0) { g_exiting = 1; g_exit_code = rc; }
            return rc;
        }

        case N_BRACE: {
            fdsave_t save;
            if (apply_redirs(n->redirs, &save) < 0) return 1;
            int rc = exec_node(n->a);
            restore_redirs(&save);
            return rc;
        }

        case N_IF: {
            fdsave_t save;
            if (apply_redirs(n->redirs, &save) < 0) return 1;
            g_cond_depth++;
            int c = exec_node(n->a);
            g_cond_depth--;
            int rc;
            if (c == 0) rc = exec_node(n->b);
            else if (n->c) rc = exec_node(n->c);
            else rc = 0;
            restore_redirs(&save);
            g_status = rc;
            return rc;
        }

        case N_WHILE:
        case N_UNTIL: {
            fdsave_t save;
            if (apply_redirs(n->redirs, &save) < 0) return 1;
            int rc = 0;
            g_loop_depth++;
            for (;;) {
                g_cond_depth++;
                int c = exec_node(n->a);
                g_cond_depth--;
                if (g_exiting || g_return) break;
                int go = (n->t == N_WHILE) ? (c == 0) : (c != 0);
                if (!go) break;
                rc = exec_node(n->b);
                if (g_exiting || g_return) break;
                if (g_break) { g_break--; break; }
                if (g_continue) { g_continue--; if (g_continue) break; }
            }
            g_loop_depth--;
            restore_redirs(&save);
            return rc;
        }

        case N_FOR: {
            fdsave_t save;
            if (apply_redirs(n->redirs, &save) < 0) return 1;
            svec items;
            sv_init(&items);
            if (n->has_items) expand_word_list(&n->items, &items);
            else for (int i = 0; i < g_argv.n; i++) sv_push(&items, xstrdup(g_argv.v[i]));

            int rc = 0;
            g_loop_depth++;
            for (int i = 0; i < items.n; i++) {
                var_set(n->name, items.v[i]);
                rc = exec_node(n->a);
                if (g_exiting || g_return) break;
                if (g_break) { g_break--; break; }
                if (g_continue) { g_continue--; if (g_continue) break; continue; }
            }
            g_loop_depth--;
            sv_free(&items);
            restore_redirs(&save);
            return rc;
        }

        case N_CASE: {
            fdsave_t save;
            if (apply_redirs(n->redirs, &save) < 0) return 1;
            svec sel;
            sv_init(&sel);
            svec one;
            sv_init(&one);
            sv_push(&one, xstrdup(n->name));
            expand_word_list(&one, &sel);
            sv_free(&one);
            const char *subject = sel.n ? sel.v[0] : "";

            int rc = 0;
            for (casearm_t *arm = n->arms; arm; arm = arm->next) {
                int hit = 0;
                for (int i = 0; i < arm->pats.n && !hit; i++) {
                    char *pat = expand_pattern(arm->pats.v[i]);
                    if (fnmatch(pat, subject, 0) == 0) hit = 1;
                    free(pat);
                }
                if (hit) {
                    rc = arm->body ? exec_node(arm->body) : 0;
                    break;
                }
            }
            sv_free(&sel);
            restore_redirs(&save);
            g_status = rc;
            return rc;
        }

        case N_FUNC:
            func_define(n->name, n->a);
            n->a = NULL;
            return 0;
    }
    return 0;
}

static int run_string(const char *src)
{
    lexer_t lx;
    lx_init(&lx, src);
    lx_next(&lx);

    int rc = g_status;
    while (lx.type != T_EOF && !lx.error) {
        node_t *n = parse_list(&lx, 0);
        if (!n) break;
        if (!g_opt_n) rc = exec_node(n);
        node_free(n);
        if (g_exiting || g_return || g_break || g_continue) break;
    }
    if (lx.error) {
        fprintf(stderr, "%s: line %zu: syntax error near '%s'\n",
                g_shname, lx.tok_line, lx.text ? lx.text : "");
        rc = 2;
        g_status = 2;
    }
    free(lx.text);
    return g_exiting ? g_exit_code : rc;
}

static int t_str_ge(const char *a, const char *b) { return strcmp(a, b) >= 0; }

static int file_test(char op, const char *path)
{
    struct stat st;
    switch (op) {
        case 'e': return stat(path, &st) == 0;
        case 'f': return stat(path, &st) == 0 && S_ISREG(st.st_mode);
        case 'd': return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        case 'c': return stat(path, &st) == 0 && S_ISCHR(st.st_mode);
        case 'b': return stat(path, &st) == 0 && S_ISBLK(st.st_mode);
        case 'L':
        case 'h': return stat(path, &st) == 0 && S_ISLNK(st.st_mode);
        case 'p': return stat(path, &st) == 0 && S_ISFIFO(st.st_mode);
        case 'S': return stat(path, &st) == 0 && S_ISSOCK(st.st_mode);
        case 's': return stat(path, &st) == 0 && st.st_size > 0;
        case 'r': return access(path, R_OK) == 0;
        case 'w': return access(path, W_OK) == 0;
        case 'x': return access(path, X_OK) == 0;
    }
    return 0;
}

static int test_expr(char **a, int n, int *used);

static int test_primary(char **a, int n, int *used)
{
    if (n <= 0) { *used = 0; return 0; }

    if (!strcmp(a[0], "!")) {
        int u = 0;
        int v = test_primary(a + 1, n - 1, &u);
        *used = u + 1;
        return !v;
    }

    if (!strcmp(a[0], "(")) {
        int u = 0;
        int v = test_expr(a + 1, n - 1, &u);
        *used = u + 1;
        if (u + 1 < n && !strcmp(a[u + 1], ")")) (*used)++;
        return v;
    }

    if (n >= 3) {
        const char *op = a[1];
        if (!strcmp(op, "=") || !strcmp(op, "==")) { *used = 3; return strcmp(a[0], a[2]) == 0; }
        if (!strcmp(op, "!="))                     { *used = 3; return strcmp(a[0], a[2]) != 0; }
        if (!strcmp(op, "<"))                      { *used = 3; return strcmp(a[0], a[2]) <  0; }
        if (!strcmp(op, ">"))                      { *used = 3; return strcmp(a[0], a[2]) >  0; }
        if (!strcmp(op, "-eq") || !strcmp(op, "-ne") || !strcmp(op, "-lt") ||
            !strcmp(op, "-le") || !strcmp(op, "-gt") || !strcmp(op, "-ge")) {
            long x = strtol(a[0], NULL, 10), y = strtol(a[2], NULL, 10);
            *used = 3;
            if (!strcmp(op, "-eq")) return x == y;
            if (!strcmp(op, "-ne")) return x != y;
            if (!strcmp(op, "-lt")) return x <  y;
            if (!strcmp(op, "-le")) return x <= y;
            if (!strcmp(op, "-gt")) return x >  y;
            return x >= y;
        }
        if (!strcmp(op, "-nt") || !strcmp(op, "-ot") || !strcmp(op, "-ef")) {
            struct stat s1, s2;
            int r1 = stat(a[0], &s1), r2 = stat(a[2], &s2);
            *used = 3;
            if (r1 || r2) return 0;
            if (!strcmp(op, "-nt")) return s1.st_mtime > s2.st_mtime;
            if (!strcmp(op, "-ot")) return s1.st_mtime < s2.st_mtime;
            return s1.st_ino == s2.st_ino;
        }
    }

    if (n >= 2 && a[0][0] == '-' && a[0][1] && !a[0][2]) {
        char op = a[0][1];
        *used = 2;
        if (op == 'z') return a[1][0] == 0;
        if (op == 'n') return a[1][0] != 0;
        if (op == 't') return isatty((int)strtol(a[1], NULL, 10));
        if (strchr("efdcbLhpSsrwx", op)) return file_test(op, a[1]);
        if (op == 'g' || op == 'u' || op == 'k' || op == 'O' || op == 'G') return 0;
    }

    *used = 1;
    return a[0][0] != 0;
}

static int test_and(char **a, int n, int *used)
{
    int u = 0;
    int v = test_primary(a, n, &u);
    int total = u;
    while (total < n && !strcmp(a[total], "-a")) {
        int u2 = 0;
        int r = test_primary(a + total + 1, n - total - 1, &u2);
        v = v && r;
        total += 1 + u2;
    }
    *used = total;
    return v;
}

static int test_expr(char **a, int n, int *used)
{
    int u = 0;
    int v = test_and(a, n, &u);
    int total = u;
    while (total < n && !strcmp(a[total], "-o")) {
        int u2 = 0;
        int r = test_and(a + total + 1, n - total - 1, &u2);
        v = v || r;
        total += 1 + u2;
    }
    *used = total;
    return v;
}

static int test_main(int argc, char **argv)
{
    int n = argc - 1;
    char **a = argv + 1;
    if (!strcmp(argv[0], "[")) {
        if (n < 1 || strcmp(a[n - 1], "]")) {
            fprintf(stderr, "%s: [: missing ]\n", g_shname);
            return 2;
        }
        n--;
    }
    if (n == 0) return 1;
    int used = 0;
    int v = test_expr(a, n, &used);
    (void)t_str_ge;
    return v ? 0 : 1;
}

static char *slurp_fd(int fd)
{
    sbuf b;
    sb_init(&b);
    char tmp[4096];
    ssize_t r;
    while ((r = read(fd, tmp, sizeof tmp)) > 0)
        for (ssize_t i = 0; i < r; i++) sb_putc(&b, tmp[i], 0);
    char *s = b.p ? b.p : xstrdup("");
    b.p = NULL;
    sb_free(&b);
    return s;
}

static void import_environ(void)
{
    for (char **e = environ; e && *e; e++) {
        char *eq = strchr(*e, '=');
        if (!eq) continue;
        char *name = xstrndup(*e, (size_t)(eq - *e));
        int i = var_index(name);
        if (i < 0) {
            var_set(name, eq + 1);
            i = var_index(name);
        }
        if (i >= 0) g_vars[i].exported = 1;
        free(name);
    }
    if (!var_get("PATH")) { var_set("PATH", "/bin:/apps:/usr/bin"); var_export("PATH"); }
    if (!var_get("IFS")) var_set("IFS", " \t\n");
    char cwd[1024];
    if (getcwd(cwd, sizeof cwd)) { var_set("PWD", cwd); var_export("PWD"); }
    var_set("OPTIND", "1");
}

static void run_exit_trap(void)
{
    if (!g_exit_trap) return;
    char *t = g_exit_trap;
    g_exit_trap = NULL;
    int saved = g_exiting;
    int savedcode = g_exit_code;
    g_exiting = 0;
    run_string(t);
    g_exiting = saved;
    g_exit_code = savedcode;
    free(t);
}

static int input_incomplete(const char *src)
{
    lexer_t lx;
    lx_init(&lx, src);
    lx_next(&lx);
    while (lx.type != T_EOF && !lx.error) {
        node_t *n = parse_list(&lx, 0);
        if (!n) break;
        node_free(n);
    }
    int inc = (lx.error && lx.type == T_EOF) || lx.unterminated;
    free(lx.text);
    return inc;
}

static void interactive_loop(void)
{
    readline_set_history_file("/root/.sh_history");
    sbuf pending;
    sb_init(&pending);

    for (;;) {
        char prompt[600];
        if (pending.n) snprintf(prompt, sizeof prompt, "> ");
        else {
            const char *ps1 = var_get("PS1");
            if (ps1) snprintf(prompt, sizeof prompt, "%s", ps1);
            else {
                char cwd[512];
                if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "?");
                snprintf(prompt, sizeof prompt, "%s $ ", cwd);
            }
        }

        char *line = readline(prompt);
        if (!line) { putchar('\n'); break; }
        if (!*line && !pending.n) { free(line); continue; }

        sb_puts(&pending, line, 0);
        sb_putc(&pending, '\n', 0);
        free(line);

        if (input_incomplete(pending.p ? pending.p : "")) continue;

        readline_add_history(pending.p ? pending.p : "");
        run_string(pending.p ? pending.p : "");
        pending.n = 0;
        if (pending.p) pending.p[0] = 0;

        if (g_exiting) break;
        g_break = g_continue = g_return = 0;
    }
    sb_free(&pending);
}

static const char *USAGE =
    "usage: sh [-c command | script [args...]] [-eux]\n"
    "\n"
    "A POSIX shell: pipelines, redirections including here-documents,\n"
    "if/while/until/for/case, functions, parameter expansion, command\n"
    "substitution, arithmetic and pathname expansion.\n"
    "\n"
    "  -c CMD   run CMD and exit\n"
    "  -s       read commands from standard input\n"
    "  -e       exit as soon as a command fails\n"
    "  -u       treat an unset parameter as an error\n"
    "  -n       parse the input but do not run it\n"
    "  -x       print each command before running it\n";

int main(int argc, char **argv)
{
    g_shname = argv[0] && argv[0][0] ? argv[0] : "sh";
    const char *base = strrchr(g_shname, '/');
    if (base) g_shname = base + 1;

    g_shell_pid = getpid();

    sv_init(&g_argv);
    import_environ();

    int i = 1;
    const char *cmd = NULL;
    int force_stdin = 0;

    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || !a[1]) break;
        if (!strcmp(a, "--")) { i++; break; }
        if (!strcmp(a, "--help")) { fputs(USAGE, stdout); return 0; }
        if (!strcmp(a, "-c")) {
            if (i + 1 >= argc) { fputs(USAGE, stderr); return 2; }
            cmd = argv[++i];
            i++;
            break;
        }
        int stop = 0;
        for (const char *p = a + 1; *p; p++) {
            if (*p == 'e') g_opt_e = 1;
            else if (*p == 'u') g_opt_u = 1;
            else if (*p == 'x') g_opt_x = 1;
            else if (*p == 's') force_stdin = 1;
            else if (*p == 'n') g_opt_n = 1;
            else if (*p == 'i') g_interactive = 1;
            else { stop = 1; break; }
        }
        if (stop) break;
    }

    int rc = 0;

    if (cmd) {
        if (i < argc) {
            g_shname = argv[i];
            set_positional(argv + i + 1, argc - i - 1);
        }
        rc = run_string(cmd);
        run_exit_trap();
        return g_exiting ? g_exit_code : rc;
    }

    if (i < argc && !force_stdin) {
        const char *path = argv[i];
        int fd = open(path, O_RDONLY, 0);
        if (fd < 0) {
            fprintf(stderr, "%s: %s: cannot open\n", g_shname, path);
            return 127;
        }
        g_shname = path;
        set_positional(argv + i + 1, argc - i - 1);
        char *src = slurp_fd(fd);
        close(fd);
        rc = run_string(src);
        free(src);
        run_exit_trap();
        return g_exiting ? g_exit_code : rc;
    }

    if (isatty(0) || g_interactive) {
        g_interactive = 1;
        cd_on_exit_setup();
        interactive_loop();
        run_exit_trap();
        return g_exiting ? g_exit_code : g_status;
    }

    char *src = slurp_fd(0);
    rc = run_string(src);
    free(src);
    run_exit_trap();
    return g_exiting ? g_exit_code : rc;
}

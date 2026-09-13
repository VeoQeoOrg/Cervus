#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: sed [-nrEsi] [-e script] [-f file] [script] [file...]\n"
    "Stream editor.\n\n"
    "  -n            suppress automatic printing\n"
    "  -e script     add the script to the commands\n"
    "  -f file       add the contents of file to the commands\n"
    "  -E, -r        use extended regular expressions\n"
    "  -s            treat files as separate streams\n"
    "  -i[SUFFIX]    edit files in place (makes a backup if SUFFIX given)\n"
    "\n"
    "Commands: s y p P d D q Q a i c n N g G h H x b t : { } = l r w\n";

typedef struct { char *b; size_t len, cap; } str_t;

static void s_init(str_t *s) { s->b = NULL; s->len = 0; s->cap = 0; }
static void s_reserve(str_t *s, size_t n)
{
    if (s->len + n + 1 <= s->cap) return;
    size_t nc = s->cap ? s->cap : 64;
    while (nc < s->len + n + 1) nc *= 2;
    char *nb = realloc(s->b, nc);
    if (!nb) { fputs("sed: out of memory\n", stderr); exit(4); }
    s->b = nb; s->cap = nc;
}
static void s_clear(str_t *s) { s->len = 0; if (s->b) s->b[0] = '\0'; }
static void s_putc(str_t *s, char c) { s_reserve(s, 1); s->b[s->len++] = c; s->b[s->len] = '\0'; }
static void s_putn(str_t *s, const char *p, size_t n) { s_reserve(s, n); memcpy(s->b + s->len, p, n); s->len += n; s->b[s->len] = '\0'; }
static void s_puts(str_t *s, const char *p) { s_putn(s, p, strlen(p)); }
static void s_set(str_t *s, const char *p, size_t n) { s_clear(s); s_putn(s, p, n); }
static const char *s_cstr(str_t *s) { if (!s->b) { s->b = malloc(1); s->b[0] = '\0'; s->cap = 1; } return s->b; }

enum { A_NONE, A_LINE, A_LAST, A_RE, A_STEP, A_ZERO };

typedef struct {
    int kind;
    long n, step;
    regex_t re;
    int re_valid;
} addr_t;

enum {
    C_SUBST='s', C_PRINT='p', C_PRINTF='P', C_DELETE='d', C_DELETEF='D',
    C_QUIT='q', C_QUITNP='Q', C_APPEND='a', C_INSERT='i', C_CHANGE='c',
    C_NEXT='n', C_NEXTA='N', C_GET='g', C_GETA='G', C_HOLD='h', C_HOLDA='H',
    C_EXCH='x', C_BRANCH='b', C_TEST='t', C_LABEL=':', C_BOPEN='{', C_BCLOSE='}',
    C_EQ='=', C_LIST='l', C_READ='r', C_WRITE='w', C_TRANS='y'
};

typedef struct cmd {
    addr_t a1, a2;
    int    naddr;
    int    negate;
    int    active;
    char   cmd;

    regex_t re; int re_valid; int use_last_re;
    str_t   repl;
    int     s_global, s_print, s_icase, s_nth; int s_eval;
    char   *s_wfile;

    char  *text;
    char  *label;
    char  *fname;
    FILE  *wfp;
    char  *ymap;

    int    block_end;
    int    pending_emit;
} cmd_t;

static cmd_t *g_cmds;
static int    g_ncmds, g_ncap;
static int    g_quiet;
static int    g_ere;
static int    g_separate;
static regex_t g_last_re; static int g_have_last_re;

static cmd_t *new_cmd(void)
{
    if (g_ncmds >= g_ncap) {
        g_ncap = g_ncap ? g_ncap * 2 : 32;
        g_cmds = realloc(g_cmds, (size_t)g_ncap * sizeof(cmd_t));
        if (!g_cmds) { fputs("sed: out of memory\n", stderr); exit(4); }
    }
    cmd_t *c = &g_cmds[g_ncmds++];
    memset(c, 0, sizeof(*c));
    return c;
}

static void die_script(const char *why)
{
    fprintf(stderr, "sed: -e expression: %s\n", why);
    exit(1);
}

static int reflags(void) { return g_ere ? REG_EXTENDED : 0; }

static const char *skip_ws(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

static const char *parse_addr(const char *s, addr_t *a)
{
    a->kind = A_NONE; a->re_valid = 0;
    if (*s == '$') { a->kind = A_LAST; return s + 1; }
    if (*s >= '0' && *s <= '9') {
        a->n = strtol(s, (char **)&s, 10);
        if (*s == '~') { s++; a->step = strtol(s, (char **)&s, 10); a->kind = A_STEP; }
        else a->kind = (a->n == 0) ? A_ZERO : A_LINE;
        return s;
    }
    if (*s == '/' || *s == '\\') {
        char delim = '/';
        if (*s == '\\') { s++; delim = *s; }
        s++;
        str_t pat; s_init(&pat);
        while (*s && *s != delim) {
            if (*s == '\\' && s[1] == delim) { s_putc(&pat, delim); s += 2; continue; }
            if (*s == '\\' && s[1] == 'n') { s_putc(&pat, '\n'); s += 2; continue; }
            s_putc(&pat, *s++);
        }
        if (*s != delim) die_script("unterminated address regex");
        s++;
        int fl = reflags();
        if (*s == 'I') { fl |= REG_ICASE; s++; }
        if (pat.len == 0) { a->kind = A_RE; a->re_valid = 0; free(pat.b); return s; }
        if (regcomp(&a->re, s_cstr(&pat), fl) != 0) die_script("invalid address regex");
        a->kind = A_RE; a->re_valid = 1;
        free(pat.b);
        return s;
    }
    return s;
}

static char *collect_text(const char **sp)
{
    const char *s = *sp;
    if (*s == '\\') { s++; if (*s == '\n') s++; }
    else s = skip_ws(s);
    str_t t; s_init(&t);
    while (*s) {
        if (*s == '\\' && s[1]) { s_putc(&t, s[1]); s += 2; continue; }
        if (*s == '\n') { s++; break; }
        s_putc(&t, *s++);
    }
    *sp = s;
    return t.b ? t.b : strdup("");
}

static char *collect_label(const char **sp)
{
    const char *s = skip_ws(*sp);
    const char *start = s;
    while (*s && *s != ';' && *s != '\n' && *s != ' ' && *s != '\t' && *s != '}') s++;
    char *l = malloc((size_t)(s - start) + 1);
    memcpy(l, start, (size_t)(s - start));
    l[s - start] = '\0';
    *sp = s;
    return l;
}

static char *collect_fname(const char **sp)
{
    const char *s = skip_ws(*sp);
    const char *start = s;
    while (*s && *s != '\n') s++;
    char *l = malloc((size_t)(s - start) + 1);
    memcpy(l, start, (size_t)(s - start));
    l[s - start] = '\0';
    *sp = s;
    return l;
}

static void parse_subst(const char **sp, cmd_t *c)
{
    const char *s = *sp;
    char delim = *s++;
    if (!delim || delim == '\\' || delim == '\n') die_script("bad s delimiter");

    str_t pat; s_init(&pat);
    while (*s && *s != delim) {
        if (*s == '\\' && s[1] == delim) { s_putc(&pat, delim); s += 2; continue; }
        if (*s == '\\' && s[1] == '\n') { s_putc(&pat, '\n'); s += 2; continue; }
        if (*s == '\\' && s[1] == 'n') { s_putc(&pat, '\n'); s += 2; continue; }
        if (*s == '\\') { s_putc(&pat, '\\'); s_putc(&pat, s[1]); s += 2; continue; }
        s_putc(&pat, *s++);
    }
    if (*s != delim) die_script("unterminated s command");
    s++;

    s_init(&c->repl);
    while (*s && *s != delim) {
        if (*s == '\\' && s[1] == delim) { s_putc(&c->repl, delim); s += 2; continue; }
        if (*s == '\\' && s[1]) { s_putc(&c->repl, '\\'); s_putc(&c->repl, s[1]); s += 2; continue; }
        if (*s == '\n') { s_putc(&c->repl, '\n'); s++; continue; }
        s_putc(&c->repl, *s++);
    }
    if (*s != delim) die_script("unterminated s replacement");
    s++;

    for (;;) {
        if (*s == 'g') { c->s_global = 1; s++; }
        else if (*s == 'p') { c->s_print = 1; s++; }
        else if (*s == 'i' || *s == 'I') { c->s_icase = 1; s++; }
        else if (*s == 'm' || *s == 'M') { s++; }
        else if (*s == 'e') { c->s_eval = 1; s++; }
        else if (*s >= '0' && *s <= '9') { c->s_nth = (int)strtol(s, (char **)&s, 10); }
        else if (*s == 'w') { s++; c->s_wfile = collect_fname(&s); break; }
        else break;
    }

    if (pat.len == 0) c->use_last_re = 1;
    else {
        if (regcomp(&c->re, s_cstr(&pat), reflags() | (c->s_icase ? REG_ICASE : 0)) != 0)
            die_script("invalid s regex");
        c->re_valid = 1;
    }
    free(pat.b);
    *sp = s;
}

static void parse_trans(const char **sp, cmd_t *c)
{
    const char *s = *sp;
    char delim = *s++;
    char from[256], to[256];
    int fi = 0, ti = 0;
    while (*s && *s != delim && fi < 255) {
        if (*s == '\\' && s[1]) { s++; from[fi++] = (*s == 'n') ? '\n' : (*s == 't') ? '\t' : *s; s++; continue; }
        from[fi++] = *s++;
    }
    if (*s != delim) die_script("unterminated y command");
    s++;
    while (*s && *s != delim && ti < 255) {
        if (*s == '\\' && s[1]) { s++; to[ti++] = (*s == 'n') ? '\n' : (*s == 't') ? '\t' : *s; s++; continue; }
        to[ti++] = *s++;
    }
    if (*s != delim) die_script("unterminated y command");
    s++;
    if (fi != ti) die_script("y strings differ in length");
    c->ymap = malloc(256);
    for (int i = 0; i < 256; i++) c->ymap[i] = (char)i;
    for (int i = 0; i < fi; i++) c->ymap[(unsigned char)from[i]] = to[i];
    *sp = s;
}

static void parse_script(const char *s)
{
    int brace_stack[64], bsp = 0;
    while (*s) {
        s = skip_ws(s);
        while (*s == ';' || *s == '\n') { s++; s = skip_ws(s); }
        if (!*s) break;
        if (*s == '#') { while (*s && *s != '\n') s++; continue; }

        cmd_t *c = new_cmd();
        s = parse_addr(s, &c->a1);
        if (c->a1.kind != A_NONE) {
            c->naddr = 1;
            s = skip_ws(s);
            if (*s == ',') {
                s++; s = skip_ws(s);
                s = parse_addr(s, &c->a2);
                c->naddr = 2;
            }
        }
        s = skip_ws(s);
        while (*s == '!') { c->negate = !c->negate; s++; s = skip_ws(s); }

        if (!*s) die_script("missing command");
        c->cmd = *s++;

        switch (c->cmd) {
            case C_SUBST: parse_subst(&s, c); break;
            case C_TRANS: parse_trans(&s, c); break;
            case C_APPEND: case C_INSERT: case C_CHANGE:
                c->text = collect_text(&s); break;
            case C_BRANCH: case C_TEST:
                c->label = collect_label(&s); break;
            case C_LABEL:
                c->label = collect_label(&s); break;
            case C_READ: case C_WRITE:
                c->fname = collect_fname(&s); break;
            case C_BOPEN:
                if (bsp < 64) brace_stack[bsp++] = g_ncmds - 1;
                break;
            case C_BCLOSE:
                if (bsp <= 0) die_script("unexpected }");
                g_cmds[brace_stack[--bsp]].block_end = g_ncmds - 1;
                break;
            case C_PRINT: case C_PRINTF: case C_DELETE: case C_DELETEF:
            case C_QUIT: case C_QUITNP: case C_NEXT: case C_NEXTA:
            case C_GET: case C_GETA: case C_HOLD: case C_HOLDA:
            case C_EXCH: case C_EQ: case C_LIST:
                break;
            default:
                die_script("unknown command");
        }
        s = skip_ws(s);
        if (*s == ';' || *s == '\n') s++;
    }
    if (bsp != 0) die_script("unmatched {");
}

static int addr_hit(addr_t *a, long lineno, int is_last, const char *line)
{
    switch (a->kind) {
        case A_LINE: return lineno == a->n;
        case A_LAST: return is_last;
        case A_STEP: return a->step > 0 ? (lineno >= a->n && (lineno - a->n) % a->step == 0) : (lineno == a->n);
        case A_RE: {
            regex_t *re = a->re_valid ? &a->re : (g_have_last_re ? &g_last_re : NULL);
            if (!re) return 0;
            int r = regexec(re, line, 0, NULL, 0) == 0;
            if (a->re_valid) { g_last_re = a->re; g_have_last_re = 1; }
            return r;
        }
        default: return 0;
    }
}

static int selected(cmd_t *c, long lineno, int is_last, const char *line)
{
    int hit;
    if (c->naddr == 0) hit = 1;
    else if (c->naddr == 1) hit = addr_hit(&c->a1, lineno, is_last, line);
    else {
        if (!c->active) {
            if (c->a1.kind == A_ZERO) { c->active = 1; }
            else if (addr_hit(&c->a1, lineno, is_last, line)) {
                c->active = 1;
                if (c->a2.kind == A_LINE && c->a2.n <= lineno) c->active = 0;
            } else hit = 0;
        }
        if (c->active) {
            hit = 1;
            int end;
            if (c->a2.kind == A_LINE) end = lineno >= c->a2.n;
            else if (c->a2.kind == A_STEP) end = c->a2.step > 0 && lineno % c->a2.step == 0;
            else if (c->a2.kind == A_LAST) end = is_last;
            else end = addr_hit(&c->a2, lineno, is_last, line);
            if (c->a1.kind == A_ZERO && c->a2.kind == A_RE)
                end = addr_hit(&c->a2, lineno, is_last, line);
            if (end) c->active = 0;
        } else hit = 0;
    }
    return c->negate ? !hit : hit;
}

static void apply_repl(cmd_t *c, str_t *out, const char *line, regmatch_t *m)
{
    enum { CN, CU, CL } perm = CN, once = CN;
    for (const char *r = c->repl.b ? c->repl.b : ""; *r; r++) {
        char ch = 0; int emit = 0; int grp = -1;
        if (*r == '&') { grp = 0; }
        else if (*r == '\\' && r[1] >= '0' && r[1] <= '9') { grp = r[1] - '0'; r++; }
        else if (*r == '\\' && r[1]) {
            r++;
            switch (*r) {
                case 'n': ch = '\n'; emit = 1; break;
                case 't': ch = '\t'; emit = 1; break;
                case 'r': ch = '\r'; emit = 1; break;
                case 'L': perm = CL; break;
                case 'U': perm = CU; break;
                case 'E': perm = CN; break;
                case 'l': once = CL; break;
                case 'u': once = CU; break;
                default: ch = *r; emit = 1; break;
            }
        } else { ch = *r; emit = 1; }

        if (grp >= 0) {
            if (m[grp].rm_so >= 0) {
                for (int i = m[grp].rm_so; i < m[grp].rm_eo; i++) {
                    char x = line[i];
                    enum { N,U,L } mode = once != CN ? (once==CU?U:L) : (perm==CU?U:perm==CL?L:N);
                    if (mode == U && x >= 'a' && x <= 'z') x -= 32;
                    else if (mode == L && x >= 'A' && x <= 'Z') x += 32;
                    s_putc(out, x);
                    once = CN;
                }
            }
        } else if (emit) {
            char x = ch;
            enum { N,U,L } mode = once != CN ? (once==CU?U:L) : (perm==CU?U:perm==CL?L:N);
            if (mode == U && x >= 'a' && x <= 'z') x -= 32;
            else if (mode == L && x >= 'A' && x <= 'Z') x += 32;
            s_putc(out, x);
            once = CN;
        }
    }
}

static int do_subst(cmd_t *c, str_t *ps)
{
    regex_t *re = c->re_valid ? &c->re : (g_have_last_re ? &g_last_re : NULL);
    if (!re) return 0;
    if (c->re_valid) { g_last_re = c->re; g_have_last_re = 1; }

    const char *line = s_cstr(ps);
    str_t out; s_init(&out);
    regmatch_t m[10];
    int count = 0, did = 0;
    size_t pos = 0;
    size_t len = ps->len;
    int nth = c->s_nth ? c->s_nth : 1;
    int eflags = 0;

    while (pos <= len) {
        if (regexec(re, line + pos, 10, m, eflags) != 0) break;
        count++;
        int this_one = (count >= nth) && (c->s_global || count == nth);
        size_t mstart = pos + m[0].rm_so, mend = pos + m[0].rm_eo;
        s_putn(&out, line + pos, mstart - pos);
        if (this_one) {
            regmatch_t abs[10];
            for (int i = 0; i < 10; i++) {
                if (m[i].rm_so < 0) { abs[i].rm_so = -1; abs[i].rm_eo = -1; }
                else { abs[i].rm_so = pos + m[i].rm_so; abs[i].rm_eo = pos + m[i].rm_eo; }
            }
            apply_repl(c, &out, line, abs);
            did = 1;
        } else {
            s_putn(&out, line + mstart, mend - mstart);
        }
        if (mend == mstart) {
            if (mstart < len) s_putc(&out, line[mstart]);
            pos = mstart + 1;
        } else {
            pos = mend;
        }
        eflags = REG_NOTBOL;
        if (!c->s_global && count >= nth) break;
    }
    if (pos < len) s_putn(&out, line + pos, len - pos);
    if (did) { s_set(ps, out.b ? out.b : "", out.len); }
    free(out.b);
    return did;
}

static void list_line(const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)p[i];
        switch (ch) {
            case '\\': fputs("\\\\", stdout); break;
            case '\a': fputs("\\a", stdout); break;
            case '\b': fputs("\\b", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\v': fputs("\\v", stdout); break;
            case '\f': fputs("\\f", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            default:
                if (ch < 32 || ch >= 127) printf("\\%03o", ch);
                else putchar(ch);
        }
    }
    puts("$");
}

static int find_label(const char *name)
{
    for (int i = 0; i < g_ncmds; i++)
        if (g_cmds[i].cmd == C_LABEL && g_cmds[i].label && !strcmp(g_cmds[i].label, name))
            return i;
    return -1;
}

static FILE *g_out;

static long read_line(FILE *f, str_t *dst, int *had_nl)
{
    s_clear(dst);
    int ch;
    int any = 0;
    *had_nl = 0;
    while ((ch = fgetc(f)) != EOF) {
        any = 1;
        if (ch == '\n') { *had_nl = 1; break; }
        s_putc(dst, (char)ch);
    }
    if (!any && ch == EOF) return -1;
    return (long)dst->len;
}

static str_t g_ps, g_hold;
static long  g_lineno;
static int   g_tflag;
static FILE *g_cur;
static str_t g_peek; static int g_peek_valid, g_peek_nl;

static int next_raw(str_t *dst, int *had_nl)
{
    if (g_peek_valid) {
        s_set(dst, g_peek.b ? g_peek.b : "", g_peek.len);
        *had_nl = g_peek_nl;
        g_peek_valid = 0;
        return 1;
    }
    return read_line(g_cur, dst, had_nl) >= 0;
}

static int is_last_line(void)
{
    if (g_separate) {
        int nl; 
        if (g_peek_valid) return 0;
        if (read_line(g_cur, &g_peek, &nl) < 0) return 1;
        g_peek_nl = nl; g_peek_valid = 1;
        return 0;
    }
    int nl;
    if (g_peek_valid) return 0;
    if (read_line(g_cur, &g_peek, &nl) < 0) return 1;
    g_peek_nl = nl; g_peek_valid = 1;
    return 0;
}

static void flush_ps(int had_nl)
{
    fwrite(g_ps.b ? g_ps.b : "", 1, g_ps.len, g_out);
    if (had_nl) fputc('\n', g_out);
}

static int g_exit_code;

static int run_stream(void)
{
    int had_nl = 0;
    while (next_raw(&g_ps, &had_nl)) {
        g_lineno++;
        int is_last = is_last_line();
        g_tflag = 0;
        int pc = 0;
        int deleted = 0, quit = 0, restart;

        do {
            restart = 0;
            for (; pc < g_ncmds; pc++) {
                cmd_t *c = &g_cmds[pc];
                if (c->cmd == C_LABEL) continue;
                int sel = selected(c, g_lineno, is_last, s_cstr(&g_ps));
                if (c->cmd == C_BOPEN) { if (!sel) pc = c->block_end; continue; }
                if (c->cmd == C_BCLOSE) continue;
                if (!sel) continue;

                switch (c->cmd) {
                    case C_SUBST:
                        if (do_subst(c, &g_ps)) {
                            g_tflag = 1;
                            if (c->s_print) { flush_ps(1); }
                            if (c->s_wfile) {
                                FILE *w = fopen(c->s_wfile, "a");
                                if (w) { fwrite(g_ps.b, 1, g_ps.len, w); fputc('\n', w); fclose(w); }
                            }
                        }
                        break;
                    case C_TRANS:
                        for (size_t i = 0; i < g_ps.len; i++)
                            g_ps.b[i] = c->ymap[(unsigned char)g_ps.b[i]];
                        break;
                    case C_PRINT: flush_ps(1); break;
                    case C_PRINTF: {
                        char *nl = memchr(g_ps.b, '\n', g_ps.len);
                        size_t n = nl ? (size_t)(nl - g_ps.b) : g_ps.len;
                        fwrite(g_ps.b, 1, n, g_out); fputc('\n', g_out);
                        break;
                    }
                    case C_DELETE: deleted = 1; pc = g_ncmds; break;
                    case C_DELETEF: {
                        char *nl = memchr(g_ps.b, '\n', g_ps.len);
                        if (!nl) { deleted = 1; pc = g_ncmds; }
                        else {
                            size_t rest = g_ps.len - (size_t)(nl + 1 - g_ps.b);
                            memmove(g_ps.b, nl + 1, rest);
                            g_ps.len = rest; g_ps.b[rest] = '\0';
                            pc = -1; restart = 1;
                        }
                        break;
                    }
                    case C_QUIT: quit = 1; pc = g_ncmds; break;
                    case C_QUITNP: quit = 1; deleted = 1; pc = g_ncmds; break;
                    case C_APPEND: c->pending_emit = 1; break;
                    case C_INSERT: fputs(c->text, g_out); fputc('\n', g_out); break;
                    case C_CHANGE:
                        deleted = 1;
                        if (c->naddr < 2 || !c->active) { fputs(c->text, g_out); fputc('\n', g_out); }
                        pc = g_ncmds;
                        break;
                    case C_NEXT:
                        if (!g_quiet) flush_ps(had_nl);
                        if (!next_raw(&g_ps, &had_nl)) { quit = 1; deleted = 1; pc = g_ncmds; break; }
                        g_lineno++; is_last = is_last_line();
                        break;
                    case C_NEXTA: {
                        if (is_last) {
                            if (!g_quiet) { /* GNU prints pattern then ends */ }
                        }
                        int nnl;
                        str_t tmp; s_init(&tmp);
                        if (!next_raw(&tmp, &nnl)) { free(tmp.b); pc = g_ncmds; break; }
                        s_putc(&g_ps, '\n'); s_putn(&g_ps, tmp.b ? tmp.b : "", tmp.len);
                        free(tmp.b);
                        g_lineno++; had_nl = nnl; is_last = is_last_line();
                        break;
                    }
                    case C_GET: s_set(&g_ps, g_hold.b ? g_hold.b : "", g_hold.len); break;
                    case C_GETA: s_putc(&g_ps, '\n'); s_putn(&g_ps, g_hold.b ? g_hold.b : "", g_hold.len); break;
                    case C_HOLD: s_set(&g_hold, g_ps.b ? g_ps.b : "", g_ps.len); break;
                    case C_HOLDA: s_putc(&g_hold, '\n'); s_putn(&g_hold, g_ps.b ? g_ps.b : "", g_ps.len); break;
                    case C_EXCH: {
                        str_t t = g_ps; g_ps = g_hold; g_hold = t;
                        break;
                    }
                    case C_BRANCH:
                        if (!c->label || !*c->label) { pc = g_ncmds; }
                        else { int t = find_label(c->label); if (t < 0) { fprintf(stderr, "sed: no label %s\n", c->label); exit(1);} pc = t; }
                        break;
                    case C_TEST:
                        if (g_tflag) {
                            g_tflag = 0;
                            if (!c->label || !*c->label) pc = g_ncmds;
                            else { int t = find_label(c->label); if (t < 0) { fprintf(stderr, "sed: no label %s\n", c->label); exit(1);} pc = t; }
                        }
                        break;
                    case C_EQ: fprintf(g_out, "%ld\n", g_lineno); break;
                    case C_LIST: list_line(g_ps.b ? g_ps.b : "", g_ps.len); break;
                    case C_READ: {
                        FILE *rf = fopen(c->fname, "r");
                        if (rf) { int x; while ((x = fgetc(rf)) != EOF) fputc(x, g_out); fclose(rf); }
                        break;
                    }
                    case C_WRITE: {
                        if (!c->wfp) c->wfp = fopen(c->fname, "w");
                        if (c->wfp) { fwrite(g_ps.b, 1, g_ps.len, c->wfp); fputc('\n', c->wfp); fflush(c->wfp); }
                        break;
                    }
                }
                if (pc >= g_ncmds) break;
            }
        } while (restart);

        if (!deleted && !g_quiet) flush_ps(had_nl);

        for (int i = 0; i < g_ncmds; i++) {
            cmd_t *c = &g_cmds[i];
            if (c->cmd == C_APPEND && c->pending_emit) {
                fputs(c->text, g_out); fputc('\n', g_out);
                c->pending_emit = 0;
            }
        }

        if (quit) return g_exit_code;
    }
    return g_exit_code;
}

static str_t g_script;

static void add_script(const char *s)
{
    if (g_script.len) s_putc(&g_script, '\n');
    s_puts(&g_script, s);
}

static void add_script_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "sed: cannot read %s\n", path); exit(1); }
    if (g_script.len) s_putc(&g_script, '\n');
    int ch;
    while ((ch = fgetc(f)) != EOF) s_putc(&g_script, (char)ch);
    fclose(f);
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "sed")) return 0;

    s_init(&g_script);
    int have_script = 0;
    int inplace = 0;
    char *suffix = NULL;

    int i = 1;
    for (; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (!strcmp(a, "--")) { i++; break; }
        if (a[1] == '-') {
            if (!strcmp(a, "--quiet") || !strcmp(a, "--silent")) g_quiet = 1;
            else if (!strcmp(a, "--regexp-extended")) g_ere = 1;
            else if (!strcmp(a, "--separate")) g_separate = 1;
            else if (!strncmp(a, "--expression=", 13)) { add_script(a + 13); have_script = 1; }
            else if (!strncmp(a, "--file=", 7)) { add_script_file(a + 7); have_script = 1; }
            else if (!strncmp(a, "--in-place", 10)) { inplace = 1; g_separate = 1; if (a[10] == '=') suffix = a + 11; }
            else if (!strcmp(a, "--posix")) {}
            else { fputs(USAGE, stderr); return 1; }
            continue;
        }
        for (int j = 1; a[j]; j++) {
            char o = a[j];
            if (o == 'n') g_quiet = 1;
            else if (o == 'r' || o == 'E') g_ere = 1;
            else if (o == 's') g_separate = 1;
            else if (o == 'i') { inplace = 1; g_separate = 1; if (a[j+1]) suffix = &a[j+1]; break; }
            else if (o == 'e') {
                const char *arg = a[j+1] ? &a[j+1] : argv[++i];
                if (!arg) { fputs("sed: -e needs an argument\n", stderr); return 1; }
                add_script(arg); have_script = 1; break;
            }
            else if (o == 'f') {
                const char *arg = a[j+1] ? &a[j+1] : argv[++i];
                if (!arg) { fputs("sed: -f needs an argument\n", stderr); return 1; }
                add_script_file(arg); have_script = 1; break;
            }
            else { fputs(USAGE, stderr); return 1; }
        }
    }

    if (!have_script) {
        if (i >= argc) { fputs(USAGE, stderr); return 1; }
        add_script(argv[i++]);
    }

    parse_script(s_cstr(&g_script));

    s_init(&g_ps); s_init(&g_hold); s_init(&g_peek);

    int nfiles = argc - i;

    if (nfiles == 0) {
        g_cur = stdin; g_out = stdout;
        run_stream();
        return g_exit_code;
    }

    for (int fi = i; fi < argc; fi++) {
        int is_stdin = !strcmp(argv[fi], "-");
        FILE *f = is_stdin ? stdin : fopen(argv[fi], "r");
        if (!f) { fprintf(stderr, "sed: cannot open '%s'\n", argv[fi]); g_exit_code = 2; continue; }
        g_cur = f;
        g_peek_valid = 0;

        char tmpname[512];
        FILE *outf = stdout;
        if (inplace && !is_stdin) {
            snprintf(tmpname, sizeof tmpname, "%s.sed_tmp", argv[fi]);
            outf = fopen(tmpname, "w");
            if (!outf) { fprintf(stderr, "sed: cannot write '%s'\n", tmpname); fclose(f); g_exit_code = 2; continue; }
        }
        g_out = outf;

        if (g_separate) {
            g_lineno = 0;
            for (int k = 0; k < g_ncmds; k++) g_cmds[k].active = 0;
        }
        run_stream();

        if (!is_stdin) fclose(f);
        if (inplace && !is_stdin) {
            fclose(outf);
            if (suffix && *suffix) {
                char bak[512];
                snprintf(bak, sizeof bak, "%s%s", argv[fi], suffix);
                rename(argv[fi], bak);
            }
            rename(tmpname, argv[fi]);
        }
    }
    return g_exit_code;
}

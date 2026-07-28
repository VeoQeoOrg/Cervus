#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#define MAXFLD  256
#define MAXVAR  128
#define STRLEN  1024

typedef struct { double num; char str[STRLEN]; int is_str; } val_t;

static char  g_fs = ' ';
static char  g_ofs = ' ';
static char  g_line[8192];
static char  g_fields[MAXFLD][STRLEN];
static int   g_nf = 0;
static long  g_nr = 0;

typedef struct { char name[32]; val_t v; } var_t;
static var_t g_vars[MAXVAR];
static int   g_nvars = 0;

static double to_num(const val_t *v) { return v->is_str ? atof(v->str) : v->num; }
static const char *to_str(val_t *v, char *tmp)
{
    if (v->is_str) return v->str;
    double d = v->num;
    if (d == (long)d) snprintf(tmp, STRLEN, "%ld", (long)d);
    else              snprintf(tmp, STRLEN, "%g", d);
    return tmp;
}
static void set_num(val_t *v, double n) { v->is_str = 0; v->num = n; v->str[0] = 0; }
static void set_str(val_t *v, const char *s) { v->is_str = 1; strncpy(v->str, s, STRLEN - 1); v->str[STRLEN - 1] = 0; }

static val_t *var_ref(const char *name)
{
    for (int i = 0; i < g_nvars; i++)
        if (!strcmp(g_vars[i].name, name)) return &g_vars[i].v;
    if (g_nvars < MAXVAR) {
        strncpy(g_vars[g_nvars].name, name, 31);
        g_vars[g_nvars].name[31] = 0;
        set_num(&g_vars[g_nvars].v, 0);
        return &g_vars[g_nvars++].v;
    }
    static val_t dummy;
    return &dummy;
}

static void split_fields(void)
{
    g_nf = 0;
    const char *p = g_line;
    if (g_fs == ' ') {
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            int k = 0;
            while (*p && *p != ' ' && *p != '\t' && k < STRLEN - 1) g_fields[g_nf][k++] = *p++;
            g_fields[g_nf][k] = 0;
            if (++g_nf >= MAXFLD) break;
        }
    } else {
        int k = 0;
        while (*p) {
            if (*p == g_fs) {
                g_fields[g_nf][k] = 0; k = 0;
                if (++g_nf >= MAXFLD) return;
                p++;
            } else if (k < STRLEN - 1) {
                g_fields[g_nf][k++] = *p++;
            } else p++;
        }
        g_fields[g_nf][k] = 0;
        g_nf++;
    }
}

static const char *field(int i)
{
    if (i == 0) return g_line;
    if (i >= 1 && i <= g_nf) return g_fields[i - 1];
    return "";
}

/* ---- expression evaluator over a token stream ---- */

static const char *E;   /* current expr cursor */

static void skip_ws(void) { while (*E == ' ' || *E == '\t') E++; }

static void eval_assign(val_t *out);

static void eval_primary(val_t *out)
{
    skip_ws();
    if (*E == '$') {
        E++;
        val_t idx; eval_primary(&idx);
        set_str(out, field((int)to_num(&idx)));
        return;
    }
    if (*E == '(') {
        E++;
        eval_assign(out);
        skip_ws();
        if (*E == ')') E++;
        return;
    }
    if (*E == '"') {
        E++;
        char buf[STRLEN]; int k = 0;
        while (*E && *E != '"' && k < STRLEN - 1) {
            if (*E == '\\' && E[1]) {
                E++;
                char c = *E++;
                if (c == 'n') buf[k++] = '\n';
                else if (c == 't') buf[k++] = '\t';
                else buf[k++] = c;
            } else buf[k++] = *E++;
        }
        if (*E == '"') E++;
        buf[k] = 0;
        set_str(out, buf);
        return;
    }
    if (isdigit((unsigned char)*E) || (*E == '.' && isdigit((unsigned char)E[1]))) {
        set_num(out, strtod(E, (char **)&E));
        return;
    }
    if (isalpha((unsigned char)*E) || *E == '_') {
        char name[32]; int k = 0;
        while ((isalnum((unsigned char)*E) || *E == '_') && k < 31) name[k++] = *E++;
        name[k] = 0;
        if (!strcmp(name, "NR")) { set_num(out, (double)g_nr); return; }
        if (!strcmp(name, "NF")) { set_num(out, (double)g_nf); return; }
        skip_ws();
        if (*E == '(') {
            E++;
            val_t a[3]; int na = 0;
            skip_ws();
            if (*E != ')') {
                eval_assign(&a[na++]);
                skip_ws();
                while (*E == ',' && na < 3) { E++; eval_assign(&a[na++]); skip_ws(); }
            }
            if (*E == ')') E++;
            char tmp[STRLEN];
            if (!strcmp(name, "length")) {
                const char *s = (na > 0) ? to_str(&a[0], tmp) : g_line;
                set_num(out, (double)strlen(s));
            } else if (!strcmp(name, "substr") && na >= 2) {
                const char *s = to_str(&a[0], tmp);
                int slen = (int)strlen(s);
                int m = (int)to_num(&a[1]);
                if (m < 1) m = 1;
                int len = (na >= 3) ? (int)to_num(&a[2]) : slen - m + 1;
                char r[STRLEN]; int j = 0;
                for (int i = m - 1; i < slen && j < len && j < STRLEN - 1; i++) r[j++] = s[i];
                r[j] = 0;
                set_str(out, r);
            } else if (!strcmp(name, "index") && na >= 2) {
                char t2[STRLEN];
                const char *hay = to_str(&a[0], tmp);
                const char *needle = to_str(&a[1], t2);
                const char *fnd = strstr(hay, needle);
                set_num(out, fnd ? (double)(fnd - hay + 1) : 0);
            } else {
                set_num(out, 0);
            }
            return;
        }
        if (!strcmp(name, "length")) { set_num(out, (double)strlen(g_line)); return; }
        *out = *var_ref(name);
        return;
    }
    set_num(out, 0);
}

static void eval_unary(val_t *out)
{
    skip_ws();
    if (*E == '!') { E++; val_t v; eval_unary(&v); set_num(out, to_num(&v) == 0 && (!v.is_str || !v.str[0])); return; }
    if (*E == '-') { E++; val_t v; eval_unary(&v); set_num(out, -to_num(&v)); return; }
    if (*E == '+') { E++; eval_unary(out); return; }
    eval_primary(out);
}

static void eval_mul(val_t *out)
{
    eval_unary(out);
    for (;;) {
        skip_ws();
        char op = *E;
        if (op != '*' && op != '/' && op != '%') break;
        E++;
        val_t r; eval_unary(&r);
        double a = to_num(out), b = to_num(&r);
        if (op == '*') set_num(out, a * b);
        else if (op == '/') set_num(out, b ? a / b : 0);
        else set_num(out, b ? (double)((long)a % (long)b) : 0);
    }
}

static void eval_add(val_t *out)
{
    eval_mul(out);
    for (;;) {
        skip_ws();
        char op = *E;
        if (op != '+' && op != '-') break;
        E++;
        val_t r; eval_mul(&r);
        if (op == '+') set_num(out, to_num(out) + to_num(&r));
        else set_num(out, to_num(out) - to_num(&r));
    }
}

static int starts_operand(char c)
{
    return c == '$' || c == '(' || c == '"' || c == '_' ||
           isalnum((unsigned char)c) || c == '.';
}

static void eval_concat(val_t *out)
{
    eval_add(out);
    for (;;) {
        skip_ws();
        if (!starts_operand(*E)) break;
        if (*E == '<' || *E == '>' || *E == '=' || *E == '!') break;
        char tmp[STRLEN];
        char left[STRLEN];
        strncpy(left, to_str(out, tmp), STRLEN - 1); left[STRLEN - 1] = 0;
        val_t r; eval_add(&r);
        char rbuf[STRLEN];
        char joined[STRLEN];
        snprintf(joined, STRLEN, "%s%s", left, to_str(&r, rbuf));
        set_str(out, joined);
    }
}

static void eval_cmp(val_t *out)
{
    eval_concat(out);
    skip_ws();
    char op0 = *E, op1 = E[1];
    int is_cmp = (op0 == '<' || op0 == '>') ||
                 ((op0 == '=' || op0 == '!') && op1 == '=');
    if (!is_cmp) return;
    char op[3] = { op0, 0, 0 };
    E++;
    if (E[0] == '=') { op[1] = '='; E++; }
    val_t r; eval_concat(&r);
    int res;
    if (out->is_str || r.is_str) {
        char t1[STRLEN], t2[STRLEN];
        int c = strcmp(to_str(out, t1), to_str(&r, t2));
        res = (!strcmp(op, "<")) ? c < 0 : (!strcmp(op, "<=")) ? c <= 0 :
              (!strcmp(op, ">")) ? c > 0 : (!strcmp(op, ">=")) ? c >= 0 :
              (!strcmp(op, "==")) ? c == 0 : c != 0;
    } else {
        double a = to_num(out), b = to_num(&r);
        res = (!strcmp(op, "<")) ? a < b : (!strcmp(op, "<=")) ? a <= b :
              (!strcmp(op, ">")) ? a > b : (!strcmp(op, ">=")) ? a >= b :
              (!strcmp(op, "==")) ? a == b : a != b;
    }
    set_num(out, res);
}

static void eval_and(val_t *out)
{
    eval_cmp(out);
    while (1) {
        skip_ws();
        if (E[0] == '&' && E[1] == '&') { E += 2; val_t r; eval_cmp(&r);
            set_num(out, (to_num(out) != 0) && (to_num(&r) != 0)); }
        else break;
    }
}

static void eval_or(val_t *out)
{
    eval_and(out);
    while (1) {
        skip_ws();
        if (E[0] == '|' && E[1] == '|') { E += 2; val_t r; eval_and(&r);
            set_num(out, (to_num(out) != 0) || (to_num(&r) != 0)); }
        else break;
    }
}

static void eval_assign(val_t *out)
{
    skip_ws();
    const char *save = E;
    if (isalpha((unsigned char)*E) || *E == '_') {
        char name[32]; int k = 0;
        const char *p = E;
        while ((isalnum((unsigned char)*p) || *p == '_') && k < 31) name[k++] = *p++;
        name[k] = 0;
        const char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        if ((q[0] == '=' && q[1] != '=') ||
            ((q[0] == '+' || q[0] == '-' || q[0] == '*' || q[0] == '/') && q[1] == '=')) {
            char op = q[0];
            E = q + ((op == '=') ? 1 : 2);
            val_t r; eval_assign(&r);
            val_t *ref = var_ref(name);
            if (op == '=') *ref = r;
            else {
                double a = to_num(ref), b = to_num(&r);
                if (op == '+') set_num(ref, a + b);
                else if (op == '-') set_num(ref, a - b);
                else if (op == '*') set_num(ref, a * b);
                else set_num(ref, b ? a / b : 0);
            }
            *out = *ref;
            return;
        }
        E = save;
    }
    eval_or(out);
}

static double eval_str(const char *expr, val_t *out)
{
    E = expr;
    eval_assign(out);
    return to_num(out);
}

/* ---- statement execution ---- */

static void do_printf(const char *args);

static void exec_stmts(const char *stmts)
{
    const char *p = stmts;
    char stmt[1024];
    while (*p) {
        int depth = 0, k = 0;
        while (*p && (*p != ';' || depth > 0) && k < 1023) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            stmt[k++] = *p++;
        }
        stmt[k] = 0;
        if (*p == ';') p++;

        const char *s = stmt;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) continue;

        if (!strncmp(s, "printf", 6) &&
            (s[6] == '\0' || s[6] == ' ' || s[6] == '\t' || s[6] == '"')) {
            do_printf(s + 6);
            continue;
        }
        if (!strncmp(s, "print", 5) && !isalnum((unsigned char)s[5]) && s[5] != '_') {
            s += 5;
            while (*s == ' ' || *s == '\t') s++;
            if (!*s) { printf("%s\n", g_line); continue; }
            char out[STRLEN * 4]; int oi = 0;
            const char *a = s;
            while (*a) {
                int depth = 0, k = 0; char item[STRLEN];
                while (*a && (*a != ',' || depth > 0)) {
                    if (*a == '(') depth++; else if (*a == ')') depth--;
                    if (k < STRLEN - 1) item[k++] = *a;
                    a++;
                }
                item[k] = 0;
                if (*a == ',') a++;
                val_t v; eval_str(item, &v);
                char tmp[STRLEN];
                oi += snprintf(out + oi, sizeof(out) - oi, "%s", to_str(&v, tmp));
                if (*a && oi < (int)sizeof(out) - 1) out[oi++] = g_ofs;
            }
            out[oi] = 0;
            printf("%s\n", out);
        } else {
            val_t v; eval_str(s, &v);
        }
    }
}

static void do_printf(const char *args)
{
    while (*args == ' ' || *args == '\t') args++;
    val_t fmtv;
    const char *save = E;
    E = args;
    eval_assign(&fmtv);
    const char *rest = E;
    E = save;
    char ftmp[STRLEN];
    const char *fmt = to_str(&fmtv, ftmp);

    val_t argv[32]; int argn = 0;
    const char *a = rest;
    while (*a == ' ' || *a == '\t') a++;
    if (*a == ',') {
        a++;
        while (*a && argn < 32) {
            int depth = 0, k = 0; char item[STRLEN];
            while (*a && (*a != ',' || depth > 0)) {
                if (*a == '(') depth++; else if (*a == ')') depth--;
                if (k < STRLEN - 1) item[k++] = *a;
                a++;
            }
            item[k] = 0;
            if (*a == ',') a++;
            eval_str(item, &argv[argn++]);
        }
    }

    int ai = 0;
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') { putchar(*f); continue; }
        char spec[32]; int si = 0; spec[si++] = '%';
        f++;
        while (*f && !strchr("diouxXeEfgGcs%", *f) && si < 30) spec[si++] = *f++;
        if (!*f) break;
        char conv = *f; spec[si++] = conv; spec[si] = 0;
        if (conv == '%') { putchar('%'); continue; }
        val_t *v = (ai < argn) ? &argv[ai++] : NULL;
        char tmp[STRLEN];
        if (conv == 's') printf(spec, v ? to_str(v, tmp) : "");
        else if (conv == 'c') { const char *s = v ? to_str(v, tmp) : ""; printf(spec, s[0]); }
        else if (strchr("diouxX", conv)) printf(spec, (long)(v ? to_num(v) : 0));
        else printf(spec, v ? to_num(v) : 0.0);
    }
}

/* ---- rules ---- */

typedef struct {
    int   when;          /* 0 normal, 1 BEGIN, 2 END */
    int   has_pattern;
    char  pattern[512];
    int   pat_regex;     /* pattern is /substr/ */
    char  regex[256];
    char  action[2048];
    int   has_action;
} rule_t;

static rule_t g_rules[64];
static int    g_nrules = 0;

static int match_pattern(rule_t *r)
{
    if (!r->has_pattern) return 1;
    if (r->pat_regex) return strstr(g_line, r->regex) != NULL;
    val_t v;
    eval_str(r->pattern, &v);
    return (to_num(&v) != 0) || (v.is_str && v.str[0]);
}

static void run_line(void)
{
    split_fields();
    for (int i = 0; i < g_nrules; i++) {
        rule_t *r = &g_rules[i];
        if (r->when != 0) continue;
        if (match_pattern(r)) {
            if (r->has_action) exec_stmts(r->action);
            else printf("%s\n", g_line);
        }
    }
}

static void parse_program(const char *prog)
{
    const char *p = prog;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ';') p++;
        if (!*p) break;
        rule_t *r = &g_rules[g_nrules];
        memset(r, 0, sizeof(*r));

        if (!strncmp(p, "BEGIN", 5)) { r->when = 1; p += 5; }
        else if (!strncmp(p, "END", 3)) { r->when = 2; p += 3; }
        while (*p == ' ' || *p == '\t') p++;

        if (r->when == 0 && *p != '{') {
            if (*p == '/') {
                p++;
                int k = 0;
                while (*p && *p != '/' && k < 255) r->regex[k++] = *p++;
                r->regex[k] = 0;
                if (*p == '/') p++;
                r->pat_regex = 1;
                r->has_pattern = 1;
            } else {
                int depth = 0, k = 0;
                while (*p && (*p != '{' || depth > 0) && k < 511) {
                    if (*p == '(') depth++; else if (*p == ')') depth--;
                    r->pattern[k++] = *p++;
                }
                r->pattern[k] = 0;
                r->has_pattern = 1;
            }
        }
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '{') {
            p++;
            int depth = 1, k = 0;
            while (*p && depth > 0 && k < 2047) {
                if (*p == '{') depth++;
                else if (*p == '}') { depth--; if (depth == 0) break; }
                r->action[k++] = *p++;
            }
            r->action[k] = 0;
            if (*p == '}') p++;
            r->has_action = 1;
        }
        if (++g_nrules >= 64) break;
    }
}

int main(int argc, char **argv)
{
    const char *prog = NULL;
    int argi = 1;
    for (; argi < argc; argi++) {
        if (!strcmp(argv[argi], "-F") && argi + 1 < argc) {
            g_fs = argv[++argi][0];
        } else if (!strncmp(argv[argi], "-F", 2)) {
            g_fs = argv[argi][2];
        } else break;
    }
    if (argi >= argc) { fprintf(stderr, "usage: awk [-F sep] 'program' [file]\n"); return 1; }
    prog = argv[argi++];
    if (g_fs != ' ') g_ofs = ' ';

    parse_program(prog);

    for (int i = 0; i < g_nrules; i++)
        if (g_rules[i].when == 1 && g_rules[i].has_action) exec_stmts(g_rules[i].action);

    FILE *in = stdin;
    if (argi < argc) {
        in = fopen(argv[argi], "r");
        if (!in) { fprintf(stderr, "awk: cannot open %s\n", argv[argi]); return 1; }
    }

    int need_lines = 0;
    for (int i = 0; i < g_nrules; i++) if (g_rules[i].when == 0) need_lines = 1;

    if (need_lines) {
        while (fgets(g_line, sizeof(g_line), in)) {
            size_t L = strlen(g_line);
            if (L && g_line[L - 1] == '\n') g_line[L - 1] = 0;
            g_nr++;
            run_line();
        }
    }
    if (in != stdin) fclose(in);

    for (int i = 0; i < g_nrules; i++)
        if (g_rules[i].when == 2 && g_rules[i].has_action) exec_stmts(g_rules[i].action);

    return 0;
}

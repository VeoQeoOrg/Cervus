#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: expr EXPRESSION\n"
    "Evaluate EXPRESSION and write the result to standard output.\n"
    "\n"
    "  ARG1 | ARG2       ARG1 if it is neither null nor 0, otherwise ARG2\n"
    "  ARG1 & ARG2       ARG1 if neither argument is null or 0, otherwise 0\n"
    "  ARG1 OP ARG2      compare with = != < <= > >=\n"
    "  ARG1 + ARG2       arithmetic: + - * / %\n"
    "  STRING : REGEX    anchored match; length matched, or \\( \\) capture\n"
    "  match STRING REGEX\n"
    "  substr STRING POS LENGTH\n"
    "  index STRING CHARS\n"
    "  length STRING\n";

static char **g_tok;
static int    g_n;
static int    g_i;

static char *parse_or(void);

static void die(const char *m)
{
    fprintf(stderr, "expr: %s\n", m);
    exit(2);
}

static char *dup_str(const char *s)
{
    char *p = strdup(s ? s : "");
    if (!p) die("out of memory");
    return p;
}

static char *dup_num(long v)
{
    char b[32];
    snprintf(b, sizeof b, "%ld", v);
    return dup_str(b);
}

static const char *peek(void) { return (g_i < g_n) ? g_tok[g_i] : NULL; }
static const char *next(void) { return (g_i < g_n) ? g_tok[g_i++] : NULL; }

static int tok_is(const char *s)
{
    const char *t = peek();
    return t && !strcmp(t, s);
}

static int as_num(const char *s, long *out)
{
    if (!s) return 0;
    const char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-' || *p == '+') p++;
    if (!*p) return 0;
    char *end;
    long v = strtol(s, &end, 10);
    while (*end == ' ' || *end == '\t') end++;
    if (*end) return 0;
    if (out) *out = v;
    return 1;
}

static long need_num(const char *s)
{
    long v;
    if (!as_num(s, &v)) die("non-integer argument");
    return v;
}

static int truthy(const char *s)
{
    long v;
    if (!s || !*s) return 0;
    if (as_num(s, &v)) return v != 0;
    return 1;
}

static char *do_match(const char *s, const char *pat)
{
    char *anchored = malloc(strlen(pat) + 2);
    if (!anchored) die("out of memory");
    if (pat[0] == '^') strcpy(anchored, pat);
    else { anchored[0] = '^'; strcpy(anchored + 1, pat); }

    regex_t re;
    if (regcomp(&re, anchored, 0) != 0) { free(anchored); die("invalid regular expression"); }
    free(anchored);

    regmatch_t m[2];
    int rc = regexec(&re, s, 2, m, 0);
    int grouped = re.re_nsub > 0;
    regfree(&re);

    if (rc != 0) return grouped ? dup_str("") : dup_num(0);
    if (!grouped) return dup_num((long)(m[0].rm_eo - m[0].rm_so));
    if (m[1].rm_so < 0) return dup_str("");

    size_t len = (size_t)(m[1].rm_eo - m[1].rm_so);
    char *out = malloc(len + 1);
    if (!out) die("out of memory");
    memcpy(out, s + m[1].rm_so, len);
    out[len] = '\0';
    return out;
}

static char *parse_prim(void)
{
    if (tok_is("(")) {
        next();
        char *v = parse_or();
        if (!tok_is(")")) die("missing )");
        next();
        return v;
    }
    if (tok_is("length")) {
        next();
        const char *s = next();
        if (!s) die("missing argument");
        return dup_num((long)strlen(s));
    }
    if (tok_is("match")) {
        next();
        const char *s = next();
        const char *p = next();
        if (!s || !p) die("missing argument");
        return do_match(s, p);
    }
    if (tok_is("index")) {
        next();
        const char *s = next();
        const char *set = next();
        if (!s || !set) die("missing argument");
        for (size_t i = 0; s[i]; i++)
            if (strchr(set, s[i])) return dup_num((long)i + 1);
        return dup_num(0);
    }
    if (tok_is("substr")) {
        next();
        const char *s = next();
        const char *ps = next();
        const char *ls = next();
        if (!s || !ps || !ls) die("missing argument");
        long pos, len;
        if (!as_num(ps, &pos) || !as_num(ls, &len)) return dup_str("");
        long slen = (long)strlen(s);
        if (pos < 1 || len < 1 || pos > slen) return dup_str("");
        if (pos - 1 + len > slen) len = slen - (pos - 1);
        char *out = malloc((size_t)len + 1);
        if (!out) die("out of memory");
        memcpy(out, s + pos - 1, (size_t)len);
        out[len] = '\0';
        return out;
    }
    if (tok_is("+")) {
        next();
        const char *s = next();
        if (!s) die("missing argument");
        return dup_str(s);
    }
    const char *t = next();
    if (!t) die("missing argument");
    return dup_str(t);
}

static char *parse_colon(void)
{
    char *v = parse_prim();
    while (tok_is(":")) {
        next();
        char *p = parse_prim();
        char *r = do_match(v, p);
        free(v); free(p);
        v = r;
    }
    return v;
}

static char *parse_mul(void)
{
    char *v = parse_colon();
    while (tok_is("*") || tok_is("/") || tok_is("%")) {
        const char *op = next();
        char *rs = parse_colon();
        long a = need_num(v), b = need_num(rs);
        free(v); free(rs);
        if (op[0] != '*' && b == 0) die("division by zero");
        v = dup_num(op[0] == '*' ? a * b : op[0] == '/' ? a / b : a % b);
    }
    return v;
}

static char *parse_add(void)
{
    char *v = parse_mul();
    while (tok_is("+") || tok_is("-")) {
        const char *op = next();
        char *rs = parse_mul();
        long a = need_num(v), b = need_num(rs);
        free(v); free(rs);
        v = dup_num(op[0] == '+' ? a + b : a - b);
    }
    return v;
}

static char *parse_cmp(void)
{
    char *v = parse_add();
    while (tok_is("=") || tok_is("!=") || tok_is("<") ||
           tok_is("<=") || tok_is(">") || tok_is(">=")) {
        const char *op = next();
        char *rs = parse_add();
        long a, b;
        int c;
        if (as_num(v, &a) && as_num(rs, &b)) c = (a < b) ? -1 : (a > b) ? 1 : 0;
        else                                 c = strcmp(v, rs);
        free(v); free(rs);
        int res = !strcmp(op, "=")  ? c == 0 :
                  !strcmp(op, "!=") ? c != 0 :
                  !strcmp(op, "<")  ? c <  0 :
                  !strcmp(op, "<=") ? c <= 0 :
                  !strcmp(op, ">")  ? c >  0 : c >= 0;
        v = dup_num(res);
    }
    return v;
}

static char *parse_and(void)
{
    char *v = parse_cmp();
    while (tok_is("&")) {
        next();
        char *rs = parse_cmp();
        if (!truthy(v) || !truthy(rs)) { free(v); v = dup_num(0); }
        free(rs);
    }
    return v;
}

static char *parse_or(void)
{
    char *v = parse_and();
    while (tok_is("|")) {
        next();
        char *rs = parse_and();
        if (truthy(v)) free(rs);
        else { free(v); v = rs; }
    }
    return v;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "expr")) return 0;
    argc = cervus_end_of_options(argc, argv);

    if (argc < 2) { fputs(USAGE, stderr); return 2; }
    g_tok = argv + 1;
    g_n   = argc - 1;
    g_i   = 0;

    char *v = parse_or();
    if (g_i != g_n) die("syntax error");

    printf("%s\n", v);
    int rc = truthy(v) ? 0 : 1;
    free(v);
    return rc;
}

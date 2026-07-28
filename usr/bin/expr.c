#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char **g_tok;
static int    g_n;
static int    g_i;

static const char *peek(void) { return (g_i < g_n) ? g_tok[g_i] : NULL; }
static const char *next(void) { return (g_i < g_n) ? g_tok[g_i++] : NULL; }

static long parse_or(void);

static int is_num(const char *s, long *out)
{
    if (!s || !*s) return 0;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end) return 0;
    if (out) *out = v;
    return 1;
}

static long parse_prim(void)
{
    const char *t = peek();
    if (t && !strcmp(t, "(")) {
        next();
        long v = parse_or();
        if (peek() && !strcmp(peek(), ")")) next();
        return v;
    }
    if (t && !strcmp(t, "length")) {
        next();
        const char *s = next();
        return s ? (long)strlen(s) : 0;
    }
    next();
    long v = 0;
    is_num(t, &v);
    return v;
}

static long parse_mul(void)
{
    long v = parse_prim();
    const char *op;
    while ((op = peek()) && (!strcmp(op, "*") || !strcmp(op, "/") || !strcmp(op, "%"))) {
        next();
        long r = parse_prim();
        if (!strcmp(op, "*")) v *= r;
        else if (r == 0)      { fprintf(stderr, "expr: division by zero\n"); exit(2); }
        else if (!strcmp(op, "/")) v /= r;
        else                       v %= r;
    }
    return v;
}

static long parse_add(void)
{
    long v = parse_mul();
    const char *op;
    while ((op = peek()) && (!strcmp(op, "+") || !strcmp(op, "-"))) {
        next();
        long r = parse_mul();
        if (!strcmp(op, "+")) v += r; else v -= r;
    }
    return v;
}

static long parse_cmp(void)
{
    long v = parse_add();
    const char *op;
    while ((op = peek()) && (!strcmp(op, "=") || !strcmp(op, "!=") || !strcmp(op, "<") ||
                             !strcmp(op, "<=") || !strcmp(op, ">") || !strcmp(op, ">="))) {
        next();
        long r = parse_add();
        int res = 0;
        if      (!strcmp(op, "="))  res = (v == r);
        else if (!strcmp(op, "!=")) res = (v != r);
        else if (!strcmp(op, "<"))  res = (v <  r);
        else if (!strcmp(op, "<=")) res = (v <= r);
        else if (!strcmp(op, ">"))  res = (v >  r);
        else                        res = (v >= r);
        v = res;
    }
    return v;
}

static long parse_and(void)
{
    long v = parse_cmp();
    while (peek() && !strcmp(peek(), "&")) {
        next();
        long r = parse_cmp();
        v = (v && r) ? v : 0;
    }
    return v;
}

static long parse_or(void)
{
    long v = parse_and();
    while (peek() && !strcmp(peek(), "|")) {
        next();
        long r = parse_and();
        if (!v) v = r;
    }
    return v;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: expr EXPRESSION\n"); return 2; }
    g_tok = argv + 1;
    g_n   = argc - 1;
    g_i   = 0;
    long v = parse_or();
    printf("%ld\n", v);
    return v != 0 ? 0 : 1;
}

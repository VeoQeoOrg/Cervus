#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <regex.h>
#include <cervus_util.h>

static void fatal(const char *m) { fprintf(stderr, "awk: %s\n", m); exit(2); }
static void *xmalloc(size_t n) { void *p = malloc(n); if (!p) fatal("out of memory"); return p; }
static void *xrealloc(void *q, size_t n) { void *p = realloc(q, n); if (!p) fatal("out of memory"); return p; }
static char *xstrdup(const char *s) { char *p = xmalloc(strlen(s) + 1); strcpy(p, s); return p; }

/* ---------- values ---------- */
enum { V_NUM = 1, V_STR = 2, V_STRNUM = 4 };
typedef struct { double num; char *str; int flags; } Val;

static Val v_num(double n) { Val v; v.num = n; v.str = NULL; v.flags = V_NUM; return v; }
static Val v_str(char *s) { Val v; v.num = 0; v.str = s; v.flags = V_STR; return v; }
static Val v_strnum(char *s) { Val v; v.num = 0; v.str = s; v.flags = V_STR | V_STRNUM; return v; }
static Val v_uninit(void) { Val v; v.num = 0; v.str = NULL; v.flags = 0; return v; }

static int looks_num(const char *s, double *out)
{
    if (!s) return 0;
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (!*p) return 0;
    char *end;
    double d = strtod(p, &end);
    if (end == p) return 0;
    while (*end == ' ' || *end == '\t' || *end == '\n') end++;
    if (*end) return 0;
    if (out) *out = d;
    return 1;
}

static double to_num(const Val *v)
{
    if (v->flags & V_NUM) return v->num;
    if (v->str) { double d; return looks_num(v->str, &d) ? d : atof(v->str); }
    return 0;
}

static char g_convfmt[64] = "%.6g";
static char g_ofmt[64] = "%.6g";

static char *num_to_str(double d, const char *fmt)
{
    char buf[128];
    if (d == (long long)d && d >= -1e18 && d <= 1e18)
        snprintf(buf, sizeof buf, "%lld", (long long)d);
    else
        snprintf(buf, sizeof buf, fmt, d);
    return xstrdup(buf);
}

static char *to_str(const Val *v)
{
    if (v->flags & V_STR) return xstrdup(v->str ? v->str : "");
    if (v->flags & V_NUM) return num_to_str(v->num, g_convfmt);
    return xstrdup("");
}
static char *to_ostr(const Val *v)
{
    if (v->flags & V_STR) return xstrdup(v->str ? v->str : "");
    if (v->flags & V_NUM) return num_to_str(v->num, g_ofmt);
    return xstrdup("");
}

static int to_bool(const Val *v)
{
    if (v->flags & V_NUM) return v->num != 0;
    if (v->flags & V_STRNUM) { double d; if (looks_num(v->str, &d)) return d != 0; }
    if (v->str) return v->str[0] != 0;
    return 0;
}

static void v_free(Val *v) { if (v->str) { free(v->str); v->str = NULL; } }

/* ---------- lexer ---------- */
enum {
    T_EOF, T_NUM, T_STR, T_ERE, T_FUNC_NAME, T_NAME, T_BUILTIN, T_FUNC,
    T_BEGIN, T_END, T_IF, T_ELSE, T_WHILE, T_FOR, T_DO, T_BREAK, T_CONTINUE,
    T_NEXT, T_NEXTFILE, T_EXIT, T_RETURN, T_DELETE, T_IN, T_GETLINE, T_PRINT, T_PRINTF,
    T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN, T_LBRACK, T_RBRACK,
    T_SEMI, T_NEWLINE, T_COMMA,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT, T_CARET,
    T_ASSIGN, T_ADDA, T_SUBA, T_MULA, T_DIVA, T_MODA, T_POWA,
    T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_MATCH, T_NOTMATCH, T_NOT, T_AND, T_OR,
    T_INCR, T_DECR, T_QUES, T_COLON, T_DOLLAR, T_APPEND, T_PIPE
};

typedef struct { int type; char *s; double num; } Tok;

static const char *L;         /* cursor */
static Tok cur, ahead; static int have_ahead;
static int g_prev_type = -1;  /* for regex/division disambiguation */

static int allow_regex(void)
{
    switch (g_prev_type) {
        case T_NUM: case T_STR: case T_NAME: case T_RPAREN: case T_RBRACK:
        case T_DOLLAR: case T_INCR: case T_DECR: case T_BUILTIN:
            return 0;
        default: return 1;
    }
}

static void lex_raw(Tok *t)
{
    const char *p = L;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\\' && p[1] == '\n') { p += 2; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; }
        break;
    }
    t->s = NULL; t->num = 0;
    if (!*p) { t->type = T_EOF; L = p; return; }

    if (*p == '\n') { t->type = T_NEWLINE; L = p + 1; return; }

    if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
        char *end; t->num = strtod(p, &end); t->type = T_NUM; L = end; return;
    }

    if (*p == '"') {
        p++;
        char buf[8192]; int k = 0;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) {
                p++;
                char c = *p++;
                switch (c) {
                    case 'n': buf[k++]='\n'; break; case 't': buf[k++]='\t'; break;
                    case 'r': buf[k++]='\r'; break; case '\\': buf[k++]='\\'; break;
                    case '"': buf[k++]='"'; break; case '/': buf[k++]='/'; break;
                    case 'a': buf[k++]='\a'; break; case 'b': buf[k++]='\b'; break;
                    case 'f': buf[k++]='\f'; break; case 'v': buf[k++]='\v'; break;
                    default: buf[k++]='\\'; buf[k++]=c; break;
                }
            } else buf[k++] = *p++;
            if (k >= 8190) break;
        }
        if (*p == '"') p++;
        buf[k] = 0;
        t->type = T_STR; t->s = xstrdup(buf); L = p; return;
    }

    if (*p == '/' && allow_regex()) {
        p++;
        char buf[4096]; int k = 0; int inbr = 0;
        while (*p && (*p != '/' || inbr)) {
            if (*p == '\\' && p[1]) { buf[k++] = *p++; buf[k++] = *p++; continue; }
            if (*p == '[') inbr = 1;
            else if (*p == ']') inbr = 0;
            buf[k++] = *p++;
            if (k >= 4094) break;
        }
        if (*p == '/') p++;
        buf[k] = 0;
        t->type = T_ERE; t->s = xstrdup(buf); L = p; return;
    }

    if (isalpha((unsigned char)*p) || *p == '_') {
        char buf[128]; int k = 0;
        while ((isalnum((unsigned char)*p) || *p == '_') && k < 127) buf[k++] = *p++;
        buf[k] = 0;
        L = p;
        struct { const char *w; int t; } kw[] = {
            {"BEGIN",T_BEGIN},{"END",T_END},{"if",T_IF},{"else",T_ELSE},
            {"while",T_WHILE},{"for",T_FOR},{"do",T_DO},{"break",T_BREAK},
            {"continue",T_CONTINUE},{"next",T_NEXT},{"nextfile",T_NEXTFILE},
            {"exit",T_EXIT},{"return",T_RETURN},{"delete",T_DELETE},{"in",T_IN},
            {"getline",T_GETLINE},{"print",T_PRINT},{"printf",T_PRINTF},
            {"function",T_FUNC},{"func",T_FUNC},{NULL,0}
        };
        for (int i = 0; kw[i].w; i++) if (!strcmp(buf, kw[i].w)) { t->type = kw[i].t; return; }
        const char *builtins[] = {"length","substr","index","split","sub","gsub",
            "match","sprintf","sin","cos","atan2","exp","log","sqrt","int","rand",
            "srand","tolower","toupper","system","close","gensub","fflush",NULL};
        for (int i = 0; builtins[i]; i++) if (!strcmp(buf, builtins[i])) { t->type = T_BUILTIN; t->s = xstrdup(buf); return; }
        /* function call if immediately followed by '(' with no space */
        if (*L == '(') { t->type = T_FUNC_NAME; t->s = xstrdup(buf); return; }
        t->type = T_NAME; t->s = xstrdup(buf); return;
    }

    L = p + 1;
    switch (*p) {
        case '{': t->type = T_LBRACE; return;
        case '}': t->type = T_RBRACE; return;
        case '(': t->type = T_LPAREN; return;
        case ')': t->type = T_RPAREN; return;
        case '[': t->type = T_LBRACK; return;
        case ']': t->type = T_RBRACK; return;
        case ';': t->type = T_SEMI; return;
        case ',': t->type = T_COMMA; return;
        case '?': t->type = T_QUES; return;
        case ':': t->type = T_COLON; return;
        case '$': t->type = T_DOLLAR; return;
        case '~': t->type = T_MATCH; return;
        case '^': if (*L=='='){L++;t->type=T_POWA;return;} t->type = T_CARET; return;
        case '%': if (*L=='='){L++;t->type=T_MODA;return;} t->type = T_PERCENT; return;
        case '*': if (*L=='*'){L++; if(*L=='='){L++;t->type=T_POWA;return;} t->type=T_CARET;return;}
                  if (*L=='='){L++;t->type=T_MULA;return;} t->type = T_STAR; return;
        case '/': if (*L=='='){L++;t->type=T_DIVA;return;} t->type = T_SLASH; return;
        case '+': if (*L=='+'){L++;t->type=T_INCR;return;} if(*L=='='){L++;t->type=T_ADDA;return;} t->type=T_PLUS; return;
        case '-': if (*L=='-'){L++;t->type=T_DECR;return;} if(*L=='='){L++;t->type=T_SUBA;return;} t->type=T_MINUS; return;
        case '=': if (*L=='='){L++;t->type=T_EQ;return;} t->type=T_ASSIGN; return;
        case '!': if (*L=='='){L++;t->type=T_NE;return;} if(*L=='~'){L++;t->type=T_NOTMATCH;return;} t->type=T_NOT; return;
        case '<': if (*L=='='){L++;t->type=T_LE;return;} t->type=T_LT; return;
        case '>': if (*L=='>'){L++;t->type=T_APPEND;return;} if(*L=='='){L++;t->type=T_GE;return;} t->type=T_GT; return;
        case '&': if (*L=='&'){L++;t->type=T_AND;return;} t->type=T_EOF; return;
        case '|': if (*L=='|'){L++;t->type=T_OR;return;} t->type=T_PIPE; return;
    }
    t->type = T_EOF;
}

static void next_tok(void)
{
    if (have_ahead) { cur = ahead; have_ahead = 0; }
    else { lex_raw(&cur); }
    g_prev_type = cur.type;
}
static Tok *peek(void)
{
    if (!have_ahead) { int save = g_prev_type; lex_raw(&ahead); g_prev_type = save; have_ahead = 1; }
    return &ahead;
}

/* ---------- AST ---------- */
enum {
    N_NUM, N_STR, N_ERE, N_VAR, N_FIELD, N_ASSIGN, N_BINOP, N_UNARY, N_POST,
    N_PRE, N_TERN, N_AND, N_OR, N_NOT, N_MATCH, N_IN, N_CALL, N_BUILTIN,
    N_GROUP, N_INDEX, N_CONCAT, N_GETLINE,
    S_PRINT, S_PRINTF, S_IF, S_WHILE, S_DO, S_FOR, S_FORIN, S_BLOCK,
    S_EXPR, S_NEXT, S_NEXTFILE, S_EXIT, S_RETURN, S_BREAK, S_CONTINUE, S_DELETE
};

typedef struct Node Node;
struct Node {
    int type, op;
    double num;
    char *str;
    Node *a, *b, *c, *d;
    Node **list; int nlist;
    regex_t *re; int re_ready;
};

static Node *node(int type) { Node *n = xmalloc(sizeof(Node)); memset(n, 0, sizeof(*n)); n->type = type; return n; }
static void nlist_add(Node *n, Node *e) { n->list = xrealloc(n->list, (size_t)(n->nlist + 1) * sizeof(Node*)); n->list[n->nlist++] = e; }

/* ---------- parser ---------- */
static Node *parse_expr(void);
static Node *parse_ternary(void);
static Node *parse_stmt(void);
static Node *parse_stmt_list(int stop);

static void expect(int t, const char *m) { if (cur.type != t) fatal(m); next_tok(); }
static void skip_nl(void) { while (cur.type == T_NEWLINE || cur.type == T_SEMI) next_tok(); }
static void skip_opt_nl(void) { while (cur.type == T_NEWLINE) next_tok(); }

static regex_t *compile_ere(const char *pat)
{
    regex_t *re = xmalloc(sizeof(regex_t));
    if (regcomp(re, pat, REG_EXTENDED) != 0) { free(re); fatal("bad regex"); }
    return re;
}

static Node *parse_primary(void)
{
    Node *n;
    switch (cur.type) {
        case T_NUM: n = node(N_NUM); n->num = cur.num; next_tok(); return n;
        case T_STR: n = node(N_STR); n->str = cur.s; next_tok(); return n;
        case T_ERE: n = node(N_ERE); n->str = cur.s; n->re = compile_ere(cur.s); next_tok(); return n;
        case T_NOT: next_tok(); n = node(N_NOT); n->a = parse_primary(); return n;
        case T_MINUS: next_tok(); n = node(N_UNARY); n->op = '-'; n->a = parse_primary(); return n;
        case T_PLUS: next_tok(); n = node(N_UNARY); n->op = '+'; n->a = parse_primary(); return n;
        case T_INCR: next_tok(); n = node(N_PRE); n->op = 1; n->a = parse_primary(); return n;
        case T_DECR: next_tok(); n = node(N_PRE); n->op = -1; n->a = parse_primary(); return n;
        case T_DOLLAR: {
            next_tok();
            n = node(N_FIELD); n->a = parse_primary();
            goto postfix;
        }
        case T_LPAREN: {
            next_tok();
            Node *e = parse_expr();
            if (cur.type == T_COMMA) {
                Node *g = node(N_GROUP); nlist_add(g, e);
                while (cur.type == T_COMMA) { next_tok(); skip_opt_nl(); nlist_add(g, parse_expr()); }
                expect(T_RPAREN, "expected )");
                return g;
            }
            expect(T_RPAREN, "expected )");
            n = e;
            goto postfix;
        }
        case T_GETLINE: {
            next_tok();
            n = node(N_GETLINE);
            if (cur.type == T_NAME || cur.type == T_DOLLAR) n->a = parse_primary();
            if (cur.type == T_LT) { next_tok(); n->b = parse_primary(); n->op = '<'; }
            return n;
        }
        case T_BUILTIN: {
            n = node(N_BUILTIN); n->str = cur.s; next_tok();
            if (cur.type == T_LPAREN) {
                next_tok(); skip_opt_nl();
                if (cur.type != T_RPAREN) {
                    nlist_add(n, parse_expr());
                    while (cur.type == T_COMMA) { next_tok(); skip_opt_nl(); nlist_add(n, parse_expr()); }
                }
                skip_opt_nl();
                expect(T_RPAREN, "expected )");
            }
            goto postfix;
        }
        case T_FUNC_NAME: {
            n = node(N_CALL); n->str = cur.s; next_tok();
            expect(T_LPAREN, "expected (");
            skip_opt_nl();
            if (cur.type != T_RPAREN) {
                nlist_add(n, parse_expr());
                while (cur.type == T_COMMA) { next_tok(); skip_opt_nl(); nlist_add(n, parse_expr()); }
            }
            skip_opt_nl();
            expect(T_RPAREN, "expected )");
            goto postfix;
        }
        case T_NAME: {
            n = node(N_VAR); n->str = cur.s; next_tok();
            if (cur.type == T_LBRACK) {
                Node *idx = node(N_INDEX); idx->str = n->str; idx->a = n;
                next_tok();
                nlist_add(idx, parse_expr());
                while (cur.type == T_COMMA) { next_tok(); nlist_add(idx, parse_expr()); }
                expect(T_RBRACK, "expected ]");
                n = idx;
            }
            goto postfix;
        }
        default:
            fatal("syntax error in expression");
    }
postfix:
    if (cur.type == T_INCR) { next_tok(); Node *p = node(N_POST); p->op = 1; p->a = n; return p; }
    if (cur.type == T_DECR) { next_tok(); Node *p = node(N_POST); p->op = -1; p->a = n; return p; }
    return n;
}

static int starts_concat(int t)
{
    switch (t) {
        case T_NUM: case T_STR: case T_ERE: case T_NAME: case T_DOLLAR:
        case T_LPAREN: case T_NOT: case T_BUILTIN: case T_FUNC_NAME:
        case T_INCR: case T_DECR: case T_MINUS: case T_PLUS:
            return 1;
        default: return 0;
    }
}

static Node *parse_pow(void)
{
    Node *a = parse_primary();
    if (cur.type == T_CARET) { next_tok(); Node *n = node(N_BINOP); n->op = '^'; n->a = a; n->b = parse_pow(); return n; }
    return a;
}
static Node *parse_mul(void)
{
    Node *a = parse_pow();
    while (cur.type == T_STAR || cur.type == T_SLASH || cur.type == T_PERCENT) {
        int op = cur.type == T_STAR ? '*' : cur.type == T_SLASH ? '/' : '%';
        next_tok(); Node *n = node(N_BINOP); n->op = op; n->a = a; n->b = parse_pow(); a = n;
    }
    return a;
}
static Node *parse_addsub(void)
{
    Node *a = parse_mul();
    while (cur.type == T_PLUS || cur.type == T_MINUS) {
        int op = cur.type == T_PLUS ? '+' : '-';
        next_tok(); Node *n = node(N_BINOP); n->op = op; n->a = a; n->b = parse_mul(); a = n;
    }
    return a;
}
static Node *parse_concat_lvl(void)
{
    Node *a = parse_addsub();
    while (starts_concat(cur.type)) {
        Node *n = node(N_CONCAT); n->a = a; n->b = parse_addsub(); a = n;
    }
    return a;
}
static Node *parse_rel(void)
{
    Node *a = parse_concat_lvl();
    int t = cur.type;
    if (t==T_LT||t==T_LE||t==T_GT||t==T_GE||t==T_EQ||t==T_NE) {
        next_tok(); Node *n = node(N_BINOP);
        n->op = t==T_LT?'<':t==T_LE?'l':t==T_GT?'>':t==T_GE?'g':t==T_EQ?'e':'n';
        n->a = a; n->b = parse_concat_lvl(); return n;
    }
    return a;
}
static Node *parse_match(void)
{
    Node *a = parse_rel();
    while (cur.type == T_MATCH || cur.type == T_NOTMATCH) {
        int neg = cur.type == T_NOTMATCH; next_tok();
        Node *n = node(N_MATCH); n->op = neg; n->a = a; n->b = parse_rel(); a = n;
    }
    return a;
}
static Node *parse_in(void)
{
    Node *a = parse_match();
    while (cur.type == T_IN) {
        next_tok();
        Node *n = node(N_IN); n->a = a;
        if (cur.type != T_NAME) fatal("expected array after in");
        n->str = cur.s; next_tok();
        a = n;
    }
    return a;
}
static Node *parse_and(void)
{
    Node *a = parse_in();
    while (cur.type == T_AND) { next_tok(); skip_opt_nl(); Node *n = node(N_AND); n->a = a; n->b = parse_in(); a = n; }
    return a;
}
static Node *parse_or(void)
{
    Node *a = parse_and();
    while (cur.type == T_OR) { next_tok(); skip_opt_nl(); Node *n = node(N_OR); n->a = a; n->b = parse_and(); a = n; }
    return a;
}
static Node *parse_ternary(void)
{
    Node *a = parse_or();
    if (cur.type == T_QUES) {
        next_tok(); skip_opt_nl();
        Node *n = node(N_TERN); n->a = a; n->b = parse_ternary();
        skip_opt_nl(); expect(T_COLON, "expected :"); skip_opt_nl();
        n->c = parse_ternary(); return n;
    }
    return a;
}
static int is_lvalue(Node *n) { return n && (n->type == N_VAR || n->type == N_FIELD || n->type == N_INDEX); }
static Node *parse_expr(void)
{
    Node *a = parse_ternary();
    int t = cur.type;
    if ((t==T_ASSIGN||t==T_ADDA||t==T_SUBA||t==T_MULA||t==T_DIVA||t==T_MODA||t==T_POWA) && is_lvalue(a)) {
        next_tok(); skip_opt_nl();
        Node *n = node(N_ASSIGN);
        n->op = t==T_ASSIGN?'=':t==T_ADDA?'+':t==T_SUBA?'-':t==T_MULA?'*':t==T_DIVA?'/':t==T_MODA?'%':'^';
        n->a = a; n->b = parse_expr(); return n;
    }
    return a;
}

static Node *parse_simple_stmt(void)
{
    switch (cur.type) {
        case T_PRINT: case T_PRINTF: {
            int pf = cur.type == T_PRINTF; next_tok();
            Node *n = node(pf ? S_PRINTF : S_PRINT);
            if (cur.type != T_SEMI && cur.type != T_NEWLINE && cur.type != T_RBRACE &&
                cur.type != T_GT && cur.type != T_APPEND && cur.type != T_PIPE && cur.type != T_EOF) {
                nlist_add(n, parse_ternary());
                while (cur.type == T_COMMA) { next_tok(); skip_opt_nl(); nlist_add(n, parse_ternary()); }
            }
            if (cur.type == T_GT || cur.type == T_APPEND || cur.type == T_PIPE) {
                n->op = cur.type == T_GT ? '>' : cur.type == T_APPEND ? 'a' : '|';
                next_tok();
                n->a = parse_ternary();
            }
            return n;
        }
        case T_DELETE: {
            next_tok();
            Node *n = node(S_DELETE);
            n->str = cur.s;
            if (cur.type != T_NAME) fatal("expected array in delete");
            next_tok();
            if (cur.type == T_LBRACK) {
                next_tok();
                nlist_add(n, parse_expr());
                while (cur.type == T_COMMA) { next_tok(); nlist_add(n, parse_expr()); }
                expect(T_RBRACK, "expected ]");
            }
            return n;
        }
        case T_NEXT: next_tok(); return node(S_NEXT);
        case T_NEXTFILE: next_tok(); return node(S_NEXTFILE);
        case T_BREAK: next_tok(); return node(S_BREAK);
        case T_CONTINUE: next_tok(); return node(S_CONTINUE);
        case T_EXIT: { next_tok(); Node *n = node(S_EXIT); if (cur.type!=T_SEMI&&cur.type!=T_NEWLINE&&cur.type!=T_RBRACE&&cur.type!=T_EOF) n->a = parse_expr(); return n; }
        case T_RETURN: { next_tok(); Node *n = node(S_RETURN); if (cur.type!=T_SEMI&&cur.type!=T_NEWLINE&&cur.type!=T_RBRACE&&cur.type!=T_EOF) n->a = parse_expr(); return n; }
        default: { Node *n = node(S_EXPR); n->a = parse_expr(); return n; }
    }
}

static Node *parse_stmt(void)
{
    skip_opt_nl();
    switch (cur.type) {
        case T_LBRACE: {
            next_tok();
            Node *b = parse_stmt_list(T_RBRACE);
            expect(T_RBRACE, "expected }");
            return b;
        }
        case T_IF: {
            next_tok(); expect(T_LPAREN, "expected ("); Node *n = node(S_IF);
            n->a = parse_expr(); expect(T_RPAREN, "expected )"); skip_opt_nl();
            n->b = parse_stmt();
            skip_nl();
            if (cur.type == T_ELSE) { next_tok(); skip_opt_nl(); n->c = parse_stmt(); }
            return n;
        }
        case T_WHILE: {
            next_tok(); expect(T_LPAREN, "expected ("); Node *n = node(S_WHILE);
            n->a = parse_expr(); expect(T_RPAREN, "expected )"); skip_opt_nl();
            n->b = parse_stmt(); return n;
        }
        case T_DO: {
            next_tok(); skip_opt_nl(); Node *n = node(S_DO);
            n->b = parse_stmt(); skip_nl();
            expect(T_WHILE, "expected while"); expect(T_LPAREN, "expected (");
            n->a = parse_expr(); expect(T_RPAREN, "expected )"); return n;
        }
        case T_FOR: {
            next_tok(); expect(T_LPAREN, "expected (");
            if (cur.type == T_NAME && peek()->type == T_IN) {
                Node *n = node(S_FORIN); n->str = cur.s; next_tok(); next_tok();
                if (cur.type != T_NAME) fatal("expected array in for-in");
                n->d = node(N_VAR); n->d->str = cur.s; next_tok();
                expect(T_RPAREN, "expected )"); skip_opt_nl();
                n->b = parse_stmt(); return n;
            }
            Node *n = node(S_FOR);
            if (cur.type != T_SEMI) n->a = parse_simple_stmt();
            expect(T_SEMI, "expected ;");
            if (cur.type != T_SEMI) n->c = parse_expr();
            expect(T_SEMI, "expected ;");
            if (cur.type != T_RPAREN) n->d = parse_simple_stmt();
            expect(T_RPAREN, "expected )"); skip_opt_nl();
            n->b = parse_stmt(); return n;
        }
        case T_SEMI: next_tok(); return node(S_BLOCK);
        default: return parse_simple_stmt();
    }
}

static Node *parse_stmt_list(int stop)
{
    Node *b = node(S_BLOCK);
    skip_nl();
    while (cur.type != stop && cur.type != T_EOF) {
        nlist_add(b, parse_stmt());
        skip_nl();
    }
    return b;
}

/* ---------- program ---------- */
typedef struct { Node *pat, *pat2; int when; Node *action; int range_active; } Rule;
static Rule g_rules[256]; static int g_nrules;

typedef struct { char *name; char **params; int nparams; Node *body; } Func;
static Func g_funcs[128]; static int g_nfuncs;

static void parse_program_text(const char *src)
{
    L = src; have_ahead = 0; g_prev_type = -1; next_tok();
    skip_nl();
    while (cur.type != T_EOF) {
        if (cur.type == T_FUNC) {
            next_tok();
            Func *f = &g_funcs[g_nfuncs++];
            if (cur.type != T_NAME && cur.type != T_FUNC_NAME) fatal("expected function name");
            f->name = cur.s; next_tok();
            expect(T_LPAREN, "expected (");
            f->params = NULL; f->nparams = 0;
            if (cur.type != T_RPAREN) {
                for (;;) {
                    if (cur.type != T_NAME) fatal("bad parameter");
                    f->params = xrealloc(f->params, (size_t)(f->nparams+1)*sizeof(char*));
                    f->params[f->nparams++] = cur.s; next_tok();
                    if (cur.type == T_COMMA) { next_tok(); skip_opt_nl(); continue; }
                    break;
                }
            }
            expect(T_RPAREN, "expected )"); skip_opt_nl();
            expect(T_LBRACE, "expected {");
            f->body = parse_stmt_list(T_RBRACE);
            expect(T_RBRACE, "expected }");
            skip_nl();
            continue;
        }
        Rule *r = &g_rules[g_nrules];
        memset(r, 0, sizeof(*r));
        if (cur.type == T_BEGIN) { r->when = 1; next_tok(); }
        else if (cur.type == T_END) { r->when = 2; next_tok(); }
        else if (cur.type != T_LBRACE) {
            r->pat = parse_expr();
            if (cur.type == T_COMMA) { next_tok(); skip_opt_nl(); r->pat2 = parse_expr(); }
        }
        skip_opt_nl();
        if (cur.type == T_LBRACE) {
            next_tok();
            r->action = parse_stmt_list(T_RBRACE);
            expect(T_RBRACE, "expected }");
        } else {
            r->action = NULL;
        }
        g_nrules++;
        skip_nl();
    }
}

/* ---------- runtime storage ---------- */
typedef struct ArrEnt { char *key; Val val; struct ArrEnt *next; } ArrEnt;
#define ABK 64
typedef struct { ArrEnt *b[ABK]; } Arr;

typedef struct Cell { int is_arr; Val v; Arr *arr; } Cell;

typedef struct SymEnt { char *name; Cell cell; struct SymEnt *next; } SymEnt;
#define SBK 128
static SymEnt *g_sym[SBK];

static unsigned hashs(const char *s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }

static Cell *global_cell(const char *name)
{
    unsigned h = hashs(name) % SBK;
    for (SymEnt *e = g_sym[h]; e; e = e->next) if (!strcmp(e->name, name)) return &e->cell;
    SymEnt *e = xmalloc(sizeof(SymEnt)); e->name = xstrdup(name); memset(&e->cell, 0, sizeof(Cell));
    e->cell.v = v_uninit(); e->next = g_sym[h]; g_sym[h] = e; return &e->cell;
}

static Arr *arr_new(void) { Arr *a = xmalloc(sizeof(Arr)); memset(a, 0, sizeof(*a)); return a; }
static Val *arr_get(Arr *a, const char *key, int create)
{
    unsigned h = hashs(key) % ABK;
    for (ArrEnt *e = a->b[h]; e; e = e->next) if (!strcmp(e->key, key)) return &e->val;
    if (!create) return NULL;
    ArrEnt *e = xmalloc(sizeof(ArrEnt)); e->key = xstrdup(key); e->val = v_uninit();
    e->next = a->b[h]; a->b[h] = e; return &e->val;
}
static void arr_del(Arr *a, const char *key)
{
    unsigned h = hashs(key) % ABK;
    ArrEnt **pp = &a->b[h];
    while (*pp) { if (!strcmp((*pp)->key, key)) { ArrEnt *d = *pp; *pp = d->next; v_free(&d->val); free(d->key); free(d); return; } pp = &(*pp)->next; }
}
static void arr_clear(Arr *a)
{
    for (int i = 0; i < ABK; i++) { ArrEnt *e = a->b[i]; while (e) { ArrEnt *n = e->next; v_free(&e->val); free(e->key); free(e); e = n; } a->b[i] = NULL; }
}

/* local scope for functions */
typedef struct Scope { char **names; Cell *cells; int n; struct Scope *prev; } Scope;
static Scope *g_scope;

static Cell *find_cell(const char *name)
{
    for (Scope *s = g_scope; s; s = s->prev) {
        for (int i = 0; i < s->n; i++) if (!strcmp(s->names[i], name)) return &s->cells[i];
        break; /* only innermost function scope (awk has no nested lexical) */
    }
    return global_cell(name);
}

/* ---------- special vars & fields ---------- */
static char *g_record;               /* $0 */
static char **g_field; static int g_nf; static int g_field_cap;
static int g_fields_valid, g_record_valid;
static long g_nr, g_fnr;
static char *g_filename = "";

static char *g_fs, *g_ofs, *g_ors, *g_rs, *g_subsep;

static void set_special_defaults(void)
{
    g_fs = xstrdup(" "); g_ofs = xstrdup(" "); g_ors = xstrdup("\n");
    g_rs = xstrdup("\n"); g_subsep = xstrdup("\034");
}

static char *getvar_str(const char *name)
{
    Cell *c = find_cell(name); return to_str(&c->v);
}

static void sync_from_special_cells(void)
{
    Cell *c;
    c = global_cell("FS"); if (c->v.flags) { free(g_fs); g_fs = to_str(&c->v); }
    c = global_cell("OFS"); if (c->v.flags) { free(g_ofs); g_ofs = to_str(&c->v); }
    c = global_cell("ORS"); if (c->v.flags) { free(g_ors); g_ors = to_str(&c->v); }
    c = global_cell("SUBSEP"); if (c->v.flags) { free(g_subsep); g_subsep = to_str(&c->v); }
    c = global_cell("CONVFMT"); if (c->v.flags) { char *s = to_str(&c->v); snprintf(g_convfmt, sizeof g_convfmt, "%s", s); free(s); }
    c = global_cell("OFMT"); if (c->v.flags) { char *s = to_str(&c->v); snprintf(g_ofmt, sizeof g_ofmt, "%s", s); free(s); }
}

static void ensure_field_cap(int n)
{
    if (n + 1 > g_field_cap) {
        int nc = g_field_cap ? g_field_cap : 16;
        while (nc < n + 1) nc *= 2;
        g_field = xrealloc(g_field, (size_t)nc * sizeof(char*));
        for (int i = g_field_cap; i < nc; i++) g_field[i] = NULL;
        g_field_cap = nc;
    }
}

static void clear_fields(void)
{
    for (int i = 0; i <= g_nf; i++) if (g_field && g_field[i]) { free(g_field[i]); g_field[i] = NULL; }
    g_nf = 0;
}

static void split_record(void)
{
    clear_fields();
    ensure_field_cap(1);
    const char *p = g_record ? g_record : "";
    sync_from_special_cells();
    int nf = 0;

    if (strcmp(g_fs, " ") == 0) {
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            if (!*p) break;
            const char *st = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            ensure_field_cap(nf + 1);
            g_field[++nf] = xmalloc((size_t)(p - st) + 1);
            memcpy(g_field[nf], st, (size_t)(p - st)); g_field[nf][p - st] = 0;
        }
    } else if (g_fs[0] && g_fs[1] == 0 && g_fs[0] != '\\') {
        char sep = g_fs[0];
        const char *st = p;
        for (;;) {
            if (*p == sep || !*p) {
                ensure_field_cap(nf + 1);
                g_field[++nf] = xmalloc((size_t)(p - st) + 1);
                memcpy(g_field[nf], st, (size_t)(p - st)); g_field[nf][p - st] = 0;
                if (!*p) break;
                p++; st = p;
            } else p++;
        }
    } else if (g_fs[0] == 0) {
        while (*p) {
            ensure_field_cap(nf + 1);
            g_field[++nf] = xmalloc(2); g_field[nf][0] = *p++; g_field[nf][1] = 0;
        }
    } else {
        regex_t re;
        if (regcomp(&re, g_fs, REG_EXTENDED) != 0) fatal("bad FS regex");
        const char *st = p;
        regmatch_t m;
        while (*p && regexec(&re, p, 1, &m, p==st?0:REG_NOTBOL) == 0) {
            if (m.rm_eo == 0) { p++; continue; }
            ensure_field_cap(nf + 1);
            int len = (int)(p + m.rm_so - st);
            g_field[++nf] = xmalloc((size_t)len + 1);
            memcpy(g_field[nf], st, (size_t)len); g_field[nf][len] = 0;
            p += m.rm_eo; st = p;
        }
        ensure_field_cap(nf + 1);
        g_field[++nf] = xstrdup(st);
        regfree(&re);
    }
    g_nf = nf;
    g_fields_valid = 1;
    Cell *c = global_cell("NF"); c->v = v_num(nf); c->is_arr = 0;
}

static void rebuild_record(void)
{
    sync_from_special_cells();
    size_t tot = 1;
    for (int i = 1; i <= g_nf; i++) tot += (g_field[i] ? strlen(g_field[i]) : 0) + strlen(g_ofs);
    char *r = xmalloc(tot + 1); r[0] = 0;
    for (int i = 1; i <= g_nf; i++) {
        if (i > 1) strcat(r, g_ofs);
        if (g_field[i]) strcat(r, g_field[i]);
    }
    free(g_record); g_record = r; g_record_valid = 1;
}

static const char *get_field(int i)
{
    if (i == 0) { if (!g_record_valid) rebuild_record(); return g_record ? g_record : ""; }
    if (!g_fields_valid) split_record();
    if (i >= 1 && i <= g_nf && g_field[i]) return g_field[i];
    return "";
}

static void set_field(int i, const char *val)
{
    if (i == 0) {
        free(g_record); g_record = xstrdup(val); g_record_valid = 1;
        g_fields_valid = 0;
        return;
    }
    if (!g_fields_valid) split_record();
    ensure_field_cap(i);
    if (i > g_nf) { for (int k = g_nf + 1; k < i; k++) { g_field[k] = xstrdup(""); } g_nf = i; Cell *c = global_cell("NF"); c->v = v_num(g_nf); }
    free(g_field[i]); g_field[i] = xstrdup(val);
    g_record_valid = 0;
}

static void set_record(char *rec)
{
    free(g_record); g_record = rec; g_record_valid = 1; g_fields_valid = 0;
}

/* ---------- evaluation ---------- */
enum { FL_NONE, FL_BREAK, FL_CONTINUE, FL_NEXT, FL_NEXTFILE, FL_EXIT, FL_RETURN };
static int g_flow;
static Val g_retval;
static int g_exit_code;

static Val eval(Node *n);
static void exec(Node *n);

static regex_t *dyn_regex(Node *n, int *own)
{
    if (n->type == N_ERE) { *own = 0; return n->re; }
    Val v = eval(n); char *s = to_str(&v);
    regex_t *re = xmalloc(sizeof(regex_t));
    if (regcomp(re, s, REG_EXTENDED) != 0) { free(re); free(s); v_free(&v); fatal("bad dynamic regex"); }
    free(s); v_free(&v); *own = 1; return re;
}

static Cell *lvalue_cell(Node *n)
{
    if (n->type == N_VAR) return find_cell(n->str);
    return NULL;
}

static char *build_index(Node *n)
{
    /* n is N_INDEX */
    char *out = xstrdup("");
    for (int i = 0; i < n->nlist; i++) {
        Val v = eval(n->list[i]); char *s = to_str(&v); v_free(&v);
        if (i) { char *t = xmalloc(strlen(out)+strlen(g_subsep)+1); strcpy(t,out); strcat(t,g_subsep); free(out); out=t; }
        char *t = xmalloc(strlen(out)+strlen(s)+1); strcpy(t,out); strcat(t,s); free(out); out=t; free(s);
    }
    return out;
}

static Val *index_ref(Node *n, int create)
{
    Cell *c = find_cell(n->str);
    if (!c->is_arr) { if (c->v.flags && !c->arr) v_free(&c->v); c->is_arr = 1; if (!c->arr) c->arr = arr_new(); }
    char *key = build_index(n);
    Val *r = arr_get(c->arr, key, create);
    free(key);
    return r;
}

static void assign_to(Node *lv, Val val)
{
    if (lv->type == N_VAR) {
        Cell *c = find_cell(lv->str);
        v_free(&c->v); c->is_arr = 0; c->v = val;
        if (!strcmp(lv->str,"FS")||!strcmp(lv->str,"OFS")||!strcmp(lv->str,"ORS")||
            !strcmp(lv->str,"SUBSEP")||!strcmp(lv->str,"CONVFMT")||!strcmp(lv->str,"OFMT"))
            sync_from_special_cells();
        if (!strcmp(lv->str,"NF")) { int nf=(int)to_num(&c->v); if(!g_fields_valid) split_record(); if(nf<g_nf){for(int k=nf+1;k<=g_nf;k++){free(g_field[k]);g_field[k]=NULL;}} else if(nf>g_nf){ensure_field_cap(nf);for(int k=g_nf+1;k<=nf;k++)g_field[k]=xstrdup("");} g_nf=nf; g_record_valid=0; }
    } else if (lv->type == N_FIELD) {
        Val iv = eval(lv->a); int i = (int)to_num(&iv); v_free(&iv);
        char *s = to_str(&val); set_field(i, s); free(s); v_free(&val);
    } else if (lv->type == N_INDEX) {
        Val *r = index_ref(lv, 1); v_free(r); *r = val;
    }
}

static Val lval_get(Node *lv)
{
    if (lv->type == N_VAR) { Cell *c = find_cell(lv->str); return c->v; }
    if (lv->type == N_FIELD) { Val iv = eval(lv->a); int i=(int)to_num(&iv); v_free(&iv); return v_strnum(xstrdup(get_field(i))); }
    if (lv->type == N_INDEX) { Val *r = index_ref(lv, 1); return *r; }
    return v_uninit();
}

static Val dup_val(Val v)
{
    if (v.flags & V_STR) { Val r = v; r.str = xstrdup(v.str ? v.str : ""); return r; }
    return v;
}

static int val_cmp(Val a, Val b)
{
    int anum = (a.flags & V_NUM) || ((a.flags & V_STRNUM) && looks_num(a.str,NULL)) || (a.flags==0);
    int bnum = (b.flags & V_NUM) || ((b.flags & V_STRNUM) && looks_num(b.str,NULL)) || (b.flags==0);
    if (anum && bnum) { double x=to_num(&a), y=to_num(&b); return x<y?-1:x>y?1:0; }
    char *x = to_str(&a), *y = to_str(&b); int c = strcmp(x,y); free(x); free(y);
    return c<0?-1:c>0?1:0;
}

static Val call_builtin(Node *n);
static Val call_func(Node *n);

static char *g_rstart_name = "RSTART", *g_rlength_name = "RLENGTH";

static Val eval(Node *n)
{
    switch (n->type) {
        case N_NUM: return v_num(n->num);
        case N_STR: return v_str(xstrdup(n->str));
        case N_ERE: {
            /* bare regex → match against $0 */
            const char *rec = get_field(0);
            return v_num(regexec(n->re, rec, 0, NULL, 0) == 0);
        }
        case N_VAR: {
            if (!strcmp(n->str,"NF")) { if(!g_fields_valid) split_record(); return v_num(g_nf); }
            if (!strcmp(n->str,"NR")) return v_num((double)g_nr);
            if (!strcmp(n->str,"FNR")) return v_num((double)g_fnr);
            if (!strcmp(n->str,"FILENAME")) return v_str(xstrdup(g_filename));
            Cell *c = find_cell(n->str);
            return dup_val(c->v);
        }
        case N_FIELD: { Val iv = eval(n->a); int i=(int)to_num(&iv); v_free(&iv); return v_strnum(xstrdup(get_field(i))); }
        case N_INDEX: { Val *r = index_ref(n, 1); return dup_val(*r); }
        case N_GROUP: { return eval(n->list[n->nlist-1]); }
        case N_CONCAT: {
            Val a = eval(n->a), b = eval(n->b);
            char *x = to_str(&a), *y = to_str(&b);
            char *r = xmalloc(strlen(x)+strlen(y)+1); strcpy(r,x); strcat(r,y);
            free(x); free(y); v_free(&a); v_free(&b);
            return v_str(r);
        }
        case N_UNARY: { Val a = eval(n->a); double d = to_num(&a); v_free(&a); return v_num(n->op=='-'?-d:d); }
        case N_NOT: { Val a = eval(n->a); int b = !to_bool(&a); v_free(&a); return v_num(b); }
        case N_BINOP: {
            if (strchr("elgn<>e", 0)) {}
            int op = n->op;
            if (op=='<'||op=='l'||op=='>'||op=='g'||op=='e'||op=='n') {
                Val a = eval(n->a), b = eval(n->b); int c = val_cmp(a,b); v_free(&a); v_free(&b);
                int r = op=='<'?c<0:op=='l'?c<=0:op=='>'?c>0:op=='g'?c>=0:op=='e'?c==0:c!=0;
                return v_num(r);
            }
            Val a = eval(n->a), b = eval(n->b);
            double x = to_num(&a), y = to_num(&b); v_free(&a); v_free(&b);
            switch (op) {
                case '+': return v_num(x+y); case '-': return v_num(x-y);
                case '*': return v_num(x*y); case '/': if(y==0) fatal("division by zero"); return v_num(x/y);
                case '%': if(y==0) fatal("division by zero"); return v_num(fmod(x,y));
                case '^': return v_num(pow(x,y));
            }
            return v_num(0);
        }
        case N_AND: { Val a = eval(n->a); if(!to_bool(&a)){v_free(&a);return v_num(0);} v_free(&a); Val b=eval(n->b); int r=to_bool(&b); v_free(&b); return v_num(r); }
        case N_OR: { Val a = eval(n->a); if(to_bool(&a)){v_free(&a);return v_num(1);} v_free(&a); Val b=eval(n->b); int r=to_bool(&b); v_free(&b); return v_num(r); }
        case N_TERN: { Val a = eval(n->a); int t = to_bool(&a); v_free(&a); return eval(t?n->b:n->c); }
        case N_MATCH: {
            Val a = eval(n->a); char *s = to_str(&a); v_free(&a);
            int own; regex_t *re = dyn_regex(n->b, &own);
            int m = regexec(re, s, 0, NULL, 0) == 0;
            free(s); if (own) { regfree(re); free(re); }
            return v_num(n->op ? !m : m);
        }
        case N_IN: {
            char *key;
            if (n->a->type == N_GROUP) {
                key = xstrdup("");
                for (int i=0;i<n->a->nlist;i++){ Val v=eval(n->a->list[i]); char*s=to_str(&v); v_free(&v);
                    if(i){char*t=xmalloc(strlen(key)+strlen(g_subsep)+1);strcpy(t,key);strcat(t,g_subsep);free(key);key=t;}
                    char*t=xmalloc(strlen(key)+strlen(s)+1);strcpy(t,key);strcat(t,s);free(key);key=t;free(s);}
            } else { Val v = eval(n->a); key = to_str(&v); v_free(&v); }
            Cell *c = find_cell(n->str);
            int r = 0;
            if (c->is_arr && c->arr) r = arr_get(c->arr, key, 0) != NULL;
            free(key); return v_num(r);
        }
        case N_ASSIGN: {
            Val rv = eval(n->b);
            if (n->op == '=') { Val d = dup_val(rv); v_free(&rv); assign_to(n->a, d); return lval_get(n->a).flags ? dup_val(lval_get(n->a)) : d; }
            Val cur2 = lval_get(n->a);
            double x = to_num(&cur2), y = to_num(&rv); v_free(&rv);
            double res = n->op=='+'?x+y:n->op=='-'?x-y:n->op=='*'?x*y:n->op=='/'?(y?x/y:(fatal("div0"),0)):n->op=='%'?fmod(x,y):pow(x,y);
            assign_to(n->a, v_num(res)); return v_num(res);
        }
        case N_PRE: { Val cv = lval_get(n->a); double d = to_num(&cv) + n->op; assign_to(n->a, v_num(d)); return v_num(d); }
        case N_POST: { Val cv = lval_get(n->a); double d = to_num(&cv); assign_to(n->a, v_num(d + n->op)); return v_num(d); }
        case N_BUILTIN: return call_builtin(n);
        case N_CALL: return call_func(n);
        case N_GETLINE: {
            /* getline [var] [< file] ; simple: from current file not supported deeply */
            return v_num(0);
        }
    }
    return v_uninit();
}

/* ---------- builtins ---------- */
static char *do_subst(int global, const char *re_src, regex_t *re, const char *repl, const char *src, int *count);

static Val call_builtin(Node *n)
{
    const char *b = n->str;
    #define A(i) (i < n->nlist ? eval(n->list[i]) : v_uninit())
    if (!strcmp(b,"length")) {
        if (n->nlist == 0) return v_num((double)strlen(get_field(0)));
        if (n->list[0]->type == N_VAR) {
            Cell *c = find_cell(n->list[0]->str);
            if (c->is_arr) { int cnt=0; for(int i=0;i<ABK;i++) for(ArrEnt*e=c->arr->b[i];e;e=e->next) cnt++; return v_num(cnt); }
        }
        Val a = A(0); char *s = to_str(&a); double r = (double)strlen(s); free(s); v_free(&a); return v_num(r);
    }
    if (!strcmp(b,"substr")) {
        Val a=A(0); char*s=to_str(&a); v_free(&a); int slen=(int)strlen(s);
        Val mv=A(1); double md=to_num(&mv); v_free(&mv);
        int m=(int)md; int len;
        if (n->nlist>=3){Val lv=A(2); len=(int)to_num(&lv); v_free(&lv);} else len=slen-(m<1?1:m)+1;
        int start = m; int end = m + len;
        if (start < 1) start = 1;
        if (end > slen+1) end = slen+1;
        if (end < start) end = start;
        int outlen = end - start;
        char *r = xmalloc((size_t)(outlen<0?0:outlen)+1);
        int j=0; for(int i=start-1;i<end-1 && i<slen && i>=0;i++) r[j++]=s[i]; r[j]=0;
        free(s); return v_str(r);
    }
    if (!strcmp(b,"index")) {
        Val a=A(0),c=A(1); char*s=to_str(&a),*t=to_str(&c); v_free(&a);v_free(&c);
        char*f=strstr(s,t); double r=f?(double)(f-s+1):0; free(s);free(t); return v_num(r);
    }
    if (!strcmp(b,"toupper")||!strcmp(b,"tolower")) {
        Val a=A(0); char*s=to_str(&a); v_free(&a); int up=b[2]=='u';
        for(char*p=s;*p;p++)*p=up?toupper((unsigned char)*p):tolower((unsigned char)*p);
        return v_str(s);
    }
    if (!strcmp(b,"sprintf")) {
        /* reuse printf formatter */
        extern char *awk_sprintf(Node *n);
        return v_str(awk_sprintf(n));
    }
    if (!strcmp(b,"split")) {
        Val a=A(0); char*s=to_str(&a); v_free(&a);
        Node *arrn = n->list[1];
        Cell *c = find_cell(arrn->str);
        if (c->is_arr && c->arr) arr_clear(c->arr); else { v_free(&c->v); c->is_arr=1; c->arr=arr_new(); }
        char *sep; int own_re=0; regex_t sre; int use_re=0; char sepc=0; int by_ws=0;
        if (n->nlist>=3) {
            if (n->list[2]->type==N_ERE) { sre=*n->list[2]->re; use_re=1; sep=NULL; }
            else { Val sv=A(2); sep=to_str(&sv); v_free(&sv);
                   if(!strcmp(sep," ")) by_ws=1;
                   else if(sep[0]&&!sep[1]) sepc=sep[0];
                   else if(sep[0]==0){/*empty*/}
                   else { if(regcomp(&sre,sep,REG_EXTENDED)==0){use_re=1;own_re=1;} } }
        } else { by_ws=1; sep=NULL; }
        int nf=0; const char*p=s;
        char kbuf[32];
        if (by_ws) {
            while(*p){ while(*p==' '||*p=='\t'||*p=='\n')p++; if(!*p)break; const char*st=p;
                while(*p&&*p!=' '&&*p!='\t'&&*p!='\n')p++;
                snprintf(kbuf,sizeof kbuf,"%d",++nf); char*fld=xmalloc((size_t)(p-st)+1);memcpy(fld,st,(size_t)(p-st));fld[p-st]=0;
                Val*r=arr_get(c->arr,kbuf,1); v_free(r); *r=v_strnum(fld); }
        } else if (use_re) {
            const char*st=p; regmatch_t m;
            while(*p&&regexec(&sre,p,1,&m,p==st?0:REG_NOTBOL)==0){ if(m.rm_eo==0){p++;continue;}
                int len=(int)(p+m.rm_so-st); snprintf(kbuf,sizeof kbuf,"%d",++nf);
                char*fld=xmalloc((size_t)len+1);memcpy(fld,st,(size_t)len);fld[len]=0;
                Val*r=arr_get(c->arr,kbuf,1);v_free(r);*r=v_strnum(fld); p+=m.rm_eo; st=p; }
            snprintf(kbuf,sizeof kbuf,"%d",++nf); Val*r=arr_get(c->arr,kbuf,1);v_free(r);*r=v_strnum(xstrdup(st));
        } else if (sepc) {
            const char*st=p; for(;;){ if(*p==sepc||!*p){ snprintf(kbuf,sizeof kbuf,"%d",++nf);
                char*fld=xmalloc((size_t)(p-st)+1);memcpy(fld,st,(size_t)(p-st));fld[p-st]=0;
                Val*r=arr_get(c->arr,kbuf,1);v_free(r);*r=v_strnum(fld); if(!*p)break; p++; st=p; } else p++; }
        } else { /* empty sep: each char */
            while(*p){ snprintf(kbuf,sizeof kbuf,"%d",++nf); char*fld=xmalloc(2);fld[0]=*p++;fld[1]=0;
                Val*r=arr_get(c->arr,kbuf,1);v_free(r);*r=v_strnum(fld); }
        }
        if (own_re) regfree(&sre);
        free(sep); free(s);
        return v_num(nf);
    }
    if (!strcmp(b,"sub")||!strcmp(b,"gsub")) {
        int global = b[0]=='g';
        int own; regex_t *re = dyn_regex(n->list[0], &own);
        Val rv=A(1); char*repl=to_str(&rv); v_free(&rv);
        Node *target = n->nlist>=3 ? n->list[2] : NULL;
        char *src; 
        if (target) { Val tv=lval_get(target); src=to_str(&tv); }
        else src = xstrdup(get_field(0));
        int cnt=0; char *res = do_subst(global, NULL, re, repl, src, &cnt);
        if (cnt>0) { if(target) assign_to(target, v_str(res)); else set_field(0, res), free(res); }
        else free(res);
        free(src); free(repl); if(own){regfree(re);free(re);}
        return v_num(cnt);
    }
    if (!strcmp(b,"match")) {
        Val a=A(0); char*s=to_str(&a); v_free(&a);
        int own; regex_t*re=dyn_regex(n->list[1],&own);
        regmatch_t m; int r=regexec(re,s,1,&m,0);
        Cell *rs=global_cell("RSTART"), *rl=global_cell("RLENGTH");
        if(r==0){ rs->v=v_num(m.rm_so+1); rl->v=v_num(m.rm_eo-m.rm_so); free(s); if(own){regfree(re);free(re);} return v_num(m.rm_so+1); }
        rs->v=v_num(0); rl->v=v_num(-1); free(s); if(own){regfree(re);free(re);} return v_num(0);
    }
    if (!strcmp(b,"int")) { Val a=A(0); double d=to_num(&a); v_free(&a); return v_num(d<0?ceil(d):floor(d)); }
    if (!strcmp(b,"sqrt")) { Val a=A(0); double d=to_num(&a); v_free(&a); return v_num(sqrt(d)); }
    if (!strcmp(b,"sin")) { Val a=A(0); double d=to_num(&a); v_free(&a); return v_num(sin(d)); }
    if (!strcmp(b,"cos")) { Val a=A(0); double d=to_num(&a); v_free(&a); return v_num(cos(d)); }
    if (!strcmp(b,"exp")) { Val a=A(0); double d=to_num(&a); v_free(&a); return v_num(exp(d)); }
    if (!strcmp(b,"log")) { Val a=A(0); double d=to_num(&a); v_free(&a); return v_num(log(d)); }
    if (!strcmp(b,"atan2")) { Val a=A(0),c=A(1); double r=atan2(to_num(&a),to_num(&c)); v_free(&a);v_free(&c); return v_num(r); }
    if (!strcmp(b,"rand")) return v_num((double)rand()/((double)RAND_MAX+1));
    if (!strcmp(b,"srand")) { if(n->nlist){Val a=A(0);srand((unsigned)to_num(&a));v_free(&a);} else srand((unsigned)time(NULL)); return v_num(0); }
    if (!strcmp(b,"system")) { Val a=A(0); char*s=to_str(&a); v_free(&a); fflush(NULL); int r=system(s); free(s); return v_num(r); }
    if (!strcmp(b,"close")) { return v_num(0); }
    if (!strcmp(b,"fflush")) { fflush(NULL); return v_num(0); }
    return v_num(0);
    #undef A
}

static char *do_subst(int global, const char *re_src, regex_t *re, const char *repl, const char *src, int *count)
{
    (void)re_src;
    size_t cap = strlen(src) + 64, len = 0;
    char *out = xmalloc(cap);
    const char *p = src; regmatch_t m; int cnt = 0; int flags = 0;
    #define PUT(ch) do { if(len+1>=cap){cap*=2;out=xrealloc(out,cap);} out[len++]=(ch); } while(0)
    while (*p || p == src) {
        if (regexec(re, p, 1, &m, flags) != 0) break;
        for (int i = 0; i < m.rm_so; i++) PUT(p[i]);
        for (const char *r = repl; *r; r++) {
            if (*r == '&') { for (int i=m.rm_so;i<m.rm_eo;i++) PUT(p[i]); }
            else if (*r == '\\' && r[1]=='&') { PUT('&'); r++; }
            else if (*r == '\\' && r[1]=='\\') { PUT('\\'); r++; }
            else PUT(*r);
        }
        cnt++;
        int adv = m.rm_eo;
        if (m.rm_eo == m.rm_so) { if (p[m.rm_eo]) PUT(p[m.rm_eo]); adv = m.rm_eo + 1; }
        p += adv;
        flags = REG_NOTBOL;
        if (!global) break;
        if (!*p && m.rm_eo!=m.rm_so) break;
        if (!p[-0] && adv==0) break;
        if (!*(p-0) && !*p) { }
        if (*p==0) break;
    }
    while (*p) PUT(*p++);
    out[len]=0;
    #undef PUT
    *count = cnt;
    return out;
}

/* ---------- printf ---------- */
static void format_into(FILE *out, Node *n, int start, char **retbuf);

char *awk_sprintf(Node *n)
{
    char *buf = NULL;
    format_into(NULL, n, 0, &buf);
    return buf ? buf : xstrdup("");
}

static void format_into(FILE *out, Node *n, int start, char **retbuf)
{
    Val fv = eval(n->list[start]);
    char *fmt = to_str(&fv); v_free(&fv);
    size_t cap = 256, len = 0; char *res = xmalloc(cap);
    #define OUT(str,n2) do { size_t _n=(n2); if(len+_n+1>cap){while(len+_n+1>cap)cap*=2;res=xrealloc(res,cap);} memcpy(res+len,(str),_n); len+=_n; } while(0)
    int ai = start + 1;
    for (char *f = fmt; *f; f++) {
        if (*f != '%') { OUT(f,1); continue; }
        char spec[64]; int si=0; spec[si++]='%'; f++;
        if (*f=='%'){ OUT("%",1); continue; }
        while (*f && strchr("-+ #0",*f) && si<60) spec[si++]=*f++;
        while (*f && (isdigit((unsigned char)*f)) && si<60) spec[si++]=*f++;
        if (*f=='*'){ Val a=ai<n->nlist?eval(n->list[ai++]):v_uninit(); si+=snprintf(spec+si,8,"%d",(int)to_num(&a)); v_free(&a); f++; }
        if (*f=='.'){ spec[si++]=*f++; while(*f&&isdigit((unsigned char)*f)&&si<60)spec[si++]=*f++;
            if(*f=='*'){Val a=ai<n->nlist?eval(n->list[ai++]):v_uninit(); si+=snprintf(spec+si,8,"%d",(int)to_num(&a)); v_free(&a); f++;} }
        char conv=*f; spec[si++]=conv; spec[si]=0;
        char tmp[4096];
        Val a = ai<n->nlist ? eval(n->list[ai++]) : v_uninit();
        if (conv=='d'||conv=='i') { char sp2[68]; snprintf(sp2,sizeof sp2,"%.*sll%c",si-1,spec,conv=='i'?'d':conv); snprintf(tmp,sizeof tmp,sp2,(long long)to_num(&a)); OUT(tmp,strlen(tmp)); }
        else if (strchr("ouxX",conv)) { char sp2[68]; snprintf(sp2,sizeof sp2,"%.*sll%c",si-1,spec,conv); snprintf(tmp,sizeof tmp,sp2,(unsigned long long)(long long)to_num(&a)); OUT(tmp,strlen(tmp)); }
        else if (strchr("eEfgG",conv)) { snprintf(tmp,sizeof tmp,spec,to_num(&a)); OUT(tmp,strlen(tmp)); }
        else if (conv=='c') { char*s=to_str(&a); if(a.flags&V_NUM){char cc=(char)(int)a.num; snprintf(tmp,sizeof tmp,spec,cc);} else snprintf(tmp,sizeof tmp,spec,s[0]); free(s); OUT(tmp,strlen(tmp)); }
        else if (conv=='s') { char*s=to_str(&a); int need=snprintf(NULL,0,spec,s); char*big=xmalloc(need+1); snprintf(big,need+1,spec,s); OUT(big,strlen(big)); free(big); free(s); }
        else { OUT(spec,strlen(spec)); }
        v_free(&a);
    }
    res[len]=0; free(fmt);
    #undef OUT
    if (retbuf) *retbuf = res;
    else { fwrite(res,1,len,out); free(res); }
}

static Val call_func(Node *n)
{
    Func *f = NULL;
    for (int i=0;i<g_nfuncs;i++) if(!strcmp(g_funcs[i].name,n->str)){f=&g_funcs[i];break;}
    if (!f) fatal("call to undefined function");
    Scope sc; sc.n = f->nparams; sc.names = f->params;
    sc.cells = xmalloc(sizeof(Cell)*(f->nparams>0?f->nparams:1));
    for (int i=0;i<f->nparams;i++) {
        memset(&sc.cells[i],0,sizeof(Cell)); sc.cells[i].v=v_uninit();
        if (i < n->nlist) {
            Node *arg = n->list[i];
            if (arg->type==N_VAR) {
                Cell *c = find_cell(arg->str);
                if (c->is_arr) { sc.cells[i].is_arr=1; sc.cells[i].arr=c->arr; continue; }
            }
            Val v = eval(arg); sc.cells[i].v = dup_val(v); v_free(&v);
        }
    }
    sc.prev = g_scope; g_scope = &sc;
    g_retval = v_uninit();
    exec(f->body);
    if (g_flow==FL_RETURN) g_flow=FL_NONE;
    g_scope = sc.prev;
    Val r = g_retval; g_retval = v_uninit();
    for (int i=0;i<f->nparams;i++) if(!sc.cells[i].is_arr) v_free(&sc.cells[i].v);
    free(sc.cells);
    return r;
}

/* ---------- statements ---------- */
static FILE *out_stream(int op, Node *dest)
{
    if (!dest) return stdout;
    Val v = eval(dest); char *name = to_str(&v); v_free(&v);
    FILE *f;
    if (!strcmp(name,"/dev/stderr")) f = stderr;
    else if (!strcmp(name,"/dev/stdout")) f = stdout;
    else if (op=='|') f = popen(name, "w");
    else f = fopen(name, op=='a'?"a":"w");
    /* NOTE: leaks fd across calls; acceptable for short scripts */
    free(name);
    return f ? f : stdout;
}

static void exec(Node *n)
{
    if (!n || g_flow) return;
    switch (n->type) {
        case S_BLOCK: for (int i=0;i<n->nlist && !g_flow;i++) exec(n->list[i]); break;
        case S_EXPR: { Val v = eval(n->a); v_free(&v); break; }
        case S_PRINT: {
            FILE *o = out_stream(n->op, n->a);
            if (n->nlist == 0) { fputs(get_field(0), o); fputs(g_ors, o); }
            else {
                for (int i=0;i<n->nlist;i++){ if(i) fputs(g_ofs,o); Val v=eval(n->list[i]); char*s=to_ostr(&v); fputs(s,o); free(s); v_free(&v); }
                fputs(g_ors, o);
            }
            if (n->op=='|') pclose(o); else if (o!=stdout && o!=stderr && n->op) fclose(o);
            break;
        }
        case S_PRINTF: {
            FILE *o = out_stream(n->op, n->a);
            format_into(o, n, 0, NULL);
            if (n->op=='|') pclose(o); else if (o!=stdout && o!=stderr && n->op) fclose(o);
            break;
        }
        case S_IF: { Val v=eval(n->a); int t=to_bool(&v); v_free(&v); if(t) exec(n->b); else if(n->c) exec(n->c); break; }
        case S_WHILE: { for(;;){ Val v=eval(n->a); int t=to_bool(&v); v_free(&v); if(!t)break; exec(n->b); if(g_flow==FL_BREAK){g_flow=FL_NONE;break;} if(g_flow==FL_CONTINUE)g_flow=FL_NONE; else if(g_flow)break; } break; }
        case S_DO: { for(;;){ exec(n->b); if(g_flow==FL_BREAK){g_flow=FL_NONE;break;} if(g_flow==FL_CONTINUE)g_flow=FL_NONE; else if(g_flow)break; Val v=eval(n->a); int t=to_bool(&v); v_free(&v); if(!t)break; } break; }
        case S_FOR: {
            if (n->a) exec(n->a);
            for(;;){ if(n->c){Val v=eval(n->c);int t=to_bool(&v);v_free(&v);if(!t)break;} exec(n->b);
                if(g_flow==FL_BREAK){g_flow=FL_NONE;break;} if(g_flow==FL_CONTINUE)g_flow=FL_NONE; else if(g_flow)break;
                if(n->d) exec(n->d); }
            break;
        }
        case S_FORIN: {
            Cell *c = find_cell(n->d->str);
            Node lv; memset(&lv,0,sizeof lv); lv.type=N_VAR; lv.str=n->str;
            if (c->is_arr && c->arr) {
                char **keys=NULL; int nk=0;
                for(int i=0;i<ABK;i++) for(ArrEnt*e=c->arr->b[i];e;e=e->next){ keys=xrealloc(keys,(size_t)(nk+1)*sizeof(char*)); keys[nk++]=xstrdup(e->key); }
                for(int i=0;i<nk;i++){ assign_to(&lv, v_str(xstrdup(keys[i]))); exec(n->b);
                    if(g_flow==FL_BREAK){g_flow=FL_NONE;break;} if(g_flow==FL_CONTINUE)g_flow=FL_NONE; else if(g_flow)break; }
                for(int i=0;i<nk;i++) free(keys[i]); free(keys);
            }
            break;
        }
        case S_DELETE: {
            Cell *c = find_cell(n->str);
            if (!c->is_arr) { c->is_arr=1; if(!c->arr)c->arr=arr_new(); }
            if (n->nlist==0) arr_clear(c->arr);
            else { char*key=xstrdup(""); for(int i=0;i<n->nlist;i++){Val v=eval(n->list[i]);char*s=to_str(&v);v_free(&v);
                if(i){char*t=xmalloc(strlen(key)+strlen(g_subsep)+1);strcpy(t,key);strcat(t,g_subsep);free(key);key=t;}
                char*t=xmalloc(strlen(key)+strlen(s)+1);strcpy(t,key);strcat(t,s);free(key);key=t;free(s);} arr_del(c->arr,key); free(key); }
            break;
        }
        case S_NEXT: g_flow=FL_NEXT; break;
        case S_NEXTFILE: g_flow=FL_NEXTFILE; break;
        case S_BREAK: g_flow=FL_BREAK; break;
        case S_CONTINUE: g_flow=FL_CONTINUE; break;
        case S_EXIT: if(n->a){Val v=eval(n->a);g_exit_code=(int)to_num(&v);v_free(&v);} g_flow=FL_EXIT; break;
        case S_RETURN: if(n->a){Val v=eval(n->a);g_retval=dup_val(v);v_free(&v);} g_flow=FL_RETURN; break;
        default: { Val v = eval(n); v_free(&v); break; }
    }
}

/* ---------- main loop ---------- */
static int pat_match(Node *p) { Val v = eval(p); int t = to_bool(&v); v_free(&v); return t; }

static void run_record(void)
{
    for (int i=0;i<g_nrules && g_flow==FL_NONE;i++) {
        Rule *r = &g_rules[i];
        if (r->when != 0) continue;
        int fire = 0;
        if (!r->pat) fire = 1;
        else if (r->pat2) {
            if (!r->range_active) { if (pat_match(r->pat)) { r->range_active=1; if(pat_match(r->pat2)) r->range_active=0; fire=1; } }
            else { fire=1; if (pat_match(r->pat2)) r->range_active=0; }
        } else fire = pat_match(r->pat);
        if (fire) {
            if (r->action) exec(r->action);
            else { fputs(get_field(0), stdout); fputs(g_ors, stdout); }
        }
        if (g_flow==FL_NEXT || g_flow==FL_NEXTFILE) { g_flow = (g_flow==FL_NEXTFILE)?FL_NEXTFILE:FL_NONE; if(g_flow==FL_NONE) return; else return; }
    }
    if (g_flow==FL_NEXT) g_flow=FL_NONE;
}

static char *read_record(FILE *f, int *had_nl)
{
    sync_from_special_cells();
    int rsc = (g_rs[0] && !g_rs[1]) ? g_rs[0] : '\n';
    size_t cap=256,len=0; char*buf=xmalloc(cap); int ch, any=0; *had_nl=0;
    while ((ch=fgetc(f))!=EOF){ any=1; if(ch==rsc){*had_nl=1;break;} if(len+1>=cap){cap*=2;buf=xrealloc(buf,cap);} buf[len++]=(char)ch; }
    if(!any){free(buf);return NULL;} buf[len]=0; return buf;
}

static int g_argc; static char **g_argv;

static void assign_var_eq(const char *s)
{
    const char *eq = strchr(s,'=');
    if (!eq) return;
    char *name = xmalloc((size_t)(eq-s)+1); memcpy(name,s,(size_t)(eq-s)); name[eq-s]=0;
    Cell *c = global_cell(name);
    v_free(&c->v); c->is_arr=0; c->v = v_strnum(xstrdup(eq+1));
    if(!strcmp(name,"FS")||!strcmp(name,"OFS")||!strcmp(name,"ORS")||!strcmp(name,"SUBSEP")||!strcmp(name,"CONVFMT")||!strcmp(name,"OFMT")) sync_from_special_cells();
    free(name);
}

static void run_file(FILE *f, const char *fname)
{
    g_filename = (char*)fname; g_fnr = 0;
    int had_nl;
    char *rec;
    while ((rec = read_record(f, &had_nl)) != NULL) {
        g_nr++; g_fnr++;
        set_record(rec);
        run_record();
        if (g_flow==FL_EXIT) return;
        if (g_flow==FL_NEXTFILE) { g_flow=FL_NONE; return; }
    }
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv,
        "Usage: awk [-F fs] [-v var=val] [-f progfile | 'prog'] [file ...]\n"
        "Pattern-directed scanning and processing language.\n", "awk")) return 0;

    set_special_defaults();
    global_cell("RSTART")->v = v_num(0);
    global_cell("RLENGTH")->v = v_num(-1);

    char *progtext = NULL; size_t progcap = 0, proglen = 0;
    int have_prog = 0;
    int i = 1;
    char *fs_opt = NULL;
    char *vqueue[128]; int nvq = 0;

    for (; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '-' || a[1] == 0) break;
        if (!strcmp(a,"--")) { i++; break; }
        if (a[1]=='F') { fs_opt = a[2] ? a+2 : argv[++i]; }
        else if (a[1]=='v') { char *v = a[2]?a+2:argv[++i]; if(nvq<128)vqueue[nvq++]=v; }
        else if (a[1]=='f') {
            char *fn = a[2]?a+2:argv[++i];
            FILE *pf = fopen(fn,"r"); if(!pf){fprintf(stderr,"awk: cannot open %s\n",fn);return 2;}
            int ch; while((ch=fgetc(pf))!=EOF){ if(proglen+2>progcap){progcap=progcap?progcap*2:1024;progtext=xrealloc(progtext,progcap);} progtext[proglen++]=(char)ch; }
            if(proglen+2>progcap){progcap+=2;progtext=xrealloc(progtext,progcap);} progtext[proglen++]='\n';
            fclose(pf); have_prog=1;
        }
        else { fprintf(stderr,"awk: unknown option %s\n",a); return 2; }
    }

    if (!have_prog) {
        if (i>=argc) { fprintf(stderr,"usage: awk [-F fs][-v v=x] 'prog' [file]\n"); return 2; }
        progtext = xstrdup(argv[i++]);
    } else if (progtext) { progtext[proglen]=0; }

    if (fs_opt) { if(!strcmp(fs_opt,"\\t")) fs_opt="\t"; free(g_fs); g_fs=xstrdup(fs_opt); global_cell("FS")->v=v_str(xstrdup(fs_opt)); }
    for (int k=0;k<nvq;k++) assign_var_eq(vqueue[k]);

    parse_program_text(progtext);

    /* BEGIN */
    for (int r=0;r<g_nrules;r++) if(g_rules[r].when==1){ exec(g_rules[r].action); if(g_flow==FL_EXIT)break; }

    int has_main=0, has_end=0;
    for (int r=0;r<g_nrules;r++){ if(g_rules[r].when==0)has_main=1; if(g_rules[r].when==2)has_end=1; }

    if (g_flow!=FL_EXIT && (has_main||has_end)) {
        /* ARGV files or stdin; also handle var=val among files */
        int nf=0;
        for (int k=i;k<argc;k++) { if (strchr(argv[k],'=') && (isalpha((unsigned char)argv[k][0])||argv[k][0]=='_')) continue; nf++; }
        if (nf==0) { g_flow=FL_NONE; run_file(stdin,""); }
        else {
            for (int k=i;k<argc && g_flow!=FL_EXIT;k++){
                char *a=argv[k];
                if (strchr(a,'=') && (isalpha((unsigned char)a[0])||a[0]=='_')) { assign_var_eq(a); continue; }
                FILE *f = !strcmp(a,"-")?stdin:fopen(a,"r");
                if(!f){fprintf(stderr,"awk: cannot open %s\n",a);g_exit_code=2;continue;}
                g_flow=FL_NONE; run_file(f,a); if(f!=stdin)fclose(f);
            }
        }
    }

    g_flow = FL_NONE;
    for (int r=0;r<g_nrules;r++) if(g_rules[r].when==2){ exec(g_rules[r].action); if(g_flow==FL_EXIT)break; }

    return g_exit_code;
}

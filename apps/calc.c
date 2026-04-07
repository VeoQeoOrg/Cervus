#include "../apps/cervus_user.h"

static void print_double(double v) {
    if (v != v)                          { ws("NaN"); return; }
    if (v > 1e300 && v == v + 1)         { ws("Inf"); return; }
    if (v < -1e300 && v == v - 1)        { ws("-Inf"); return; }
    if (v < 0.0) { wc('-'); v = -v; }

    int use_exp = (v != 0.0 && (v >= 1e15 || v < 1e-6));
    int exp_val = 0;
    if (use_exp && v != 0.0) {
        while (v >= 10.0) { v /= 10.0; exp_val++; }
        while (v <  1.0)  { v *= 10.0; exp_val--; }
    }

    const int PREC = 10;
    double scale = 1.0;
    for (int i = 0; i < PREC; i++) scale *= 10.0;
    double rounded = (double)(int64_t)(v * scale + 0.5) / scale;

    int64_t int_part = (int64_t)rounded;
    double  frac = rounded - (double)int_part;

    char ibuf[22]; int ii = 21; ibuf[ii] = '\0';
    int64_t tmp = int_part;
    if (!tmp) ibuf[--ii] = '0';
    else while (tmp) { ibuf[--ii] = '0' + (int)(tmp % 10); tmp /= 10; }
    ws(ibuf + ii);

    char fbuf[12]; int flen = 0;
    for (int i = 0; i < PREC; i++) {
        frac *= 10.0;
        int d = (int)frac;
        if (d > 9) d = 9;
        fbuf[flen++] = '0' + d;
        frac -= (double)d;
    }
    while (flen > 0 && fbuf[flen - 1] == '0') flen--;
    if (flen > 0) { wc('.'); write(1, fbuf, flen); }

    if (use_exp) {
        wc('e');
        if (exp_val < 0) { wc('-'); exp_val = -exp_val; } else wc('+');
        char eb[8]; int ei = 7; eb[ei] = '\0';
        if (!exp_val) eb[--ei] = '0';
        else while (exp_val) { eb[--ei] = '0' + exp_val % 10; exp_val /= 10; }
        ws(eb + ei);
    }
}

static const char *g_pos;
static int g_err;

static void skip_ws(void) { while (isspace((unsigned char)*g_pos)) g_pos++; }

static double parse_expr(void);

static double parse_number(void) {
    skip_ws();
    int neg = 0;
    if (*g_pos == '-') { neg = 1; g_pos++; skip_ws(); }
    if (!isdigit((unsigned char)*g_pos) && *g_pos != '.') {
        ws("calc: expected number\n"); g_err = 1; return 0.0;
    }
    double v = 0.0;
    while (isdigit((unsigned char)*g_pos)) { v = v * 10.0 + (*g_pos - '0'); g_pos++; }
    if (*g_pos == '.') {
        g_pos++;
        double frac = 0.1;
        while (isdigit((unsigned char)*g_pos)) { v += (*g_pos - '0') * frac; frac *= 0.1; g_pos++; }
    }
    if (*g_pos == 'e' || *g_pos == 'E') {
        g_pos++;
        int eneg = 0;
        if (*g_pos == '+') g_pos++;
        else if (*g_pos == '-') { eneg = 1; g_pos++; }
        int eexp = 0;
        while (isdigit((unsigned char)*g_pos)) { eexp = eexp * 10 + (*g_pos - '0'); g_pos++; }
        double mul = 1.0;
        for (int i = 0; i < eexp; i++) mul *= 10.0;
        v = eneg ? v / mul : v * mul;
    }
    return neg ? -v : v;
}

static double parse_primary(void) {
    skip_ws();
    if (*g_pos == '(') {
        g_pos++;
        double v = parse_expr();
        skip_ws();
        if (*g_pos == ')') g_pos++;
        else { ws("calc: missing ')'\n"); g_err = 1; }
        return v;
    }
    return parse_number();
}

static double parse_power(void) {
    double left = parse_primary();
    skip_ws();
    if (*g_pos == '^') {
        g_pos++;
        double right = parse_power();
        if (g_err) return 0.0;
        return pow(left, right);
    }
    return left;
}

static double parse_term(void) {
    double left = parse_power();
    for (;;) {
        skip_ws();
        char op = *g_pos;
        if (op != '*' && op != '/' && op != '%') break;
        g_pos++;
        double right = parse_power();
        if (g_err) return 0.0;
        if (op == '*') left *= right;
        else if (right == 0.0) { ws("calc: division by zero\n"); g_err = 1; return 0.0; }
        else if (op == '/') left /= right;
        else { int64_t a = (int64_t)left, b = (int64_t)right; left = (double)(a % b); }
    }
    return left;
}

static double parse_expr(void) {
    double left = parse_term();
    for (;;) {
        skip_ws();
        char op = *g_pos;
        if (op != '+' && op != '-') break;
        g_pos++;
        double right = parse_term();
        if (g_err) return 0.0;
        if (op == '+') left += right; else left -= right;
    }
    return left;
}

static char g_expr[512];

static void build_expr(int argc, char **argv) {
    int pos = 0, first = 1;
    for (int i = 1; i < argc && pos < 510; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (!first && pos < 510) g_expr[pos++] = ' ';
        first = 0;
        for (int j = 0; argv[i][j] && pos < 510; j++) g_expr[pos++] = argv[i][j];
    }
    g_expr[pos] = '\0';
}

static int readline_edit(char *buf, int max) {
    int len = 0, pos = 0;
    buf[0] = '\0';
    for (;;) {
        char c;
        if (read(0, &c, 1) <= 0) return len > 0 ? len : -1;
        if (c == '\n' || c == '\r') { buf[len] = '\0'; wc('\n'); return len; }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                for (int i = pos - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; pos--; buf[len] = '\0';
                write(1, "\b", 1); write(1, buf + pos, len - pos);
                write(1, " ", 1);
                if (len - pos + 1 > 0) { char mv[16]; snprintf(mv, sizeof(mv), "\x1b[%dD", len - pos + 1); ws(mv); }
            }
            continue;
        }
        if (c == 3) { ws("^C\n"); buf[0] = '\0'; return 0; }
        if (c == 0x1b) {
            char s; if (read(0, &s, 1) <= 0) continue;
            if (s != '[') continue;
            int param = 0; char code;
            for (;;) {
                if (read(0, &code, 1) <= 0) break;
                if (isdigit((unsigned char)code)) { param = param * 10 + (code - '0'); continue; }
                break;
            }
            if (code == 'D' && pos > 0) { pos--; ws("\x1b[D"); }
            else if (code == 'C' && pos < len) { pos++; ws("\x1b[C"); }
            else if (code == 'H' || (code == '~' && param == 1)) {
                if (pos > 0) { char mv[16]; snprintf(mv, sizeof(mv), "\x1b[%dD", pos); ws(mv); pos = 0; }
            }
            else if (code == 'F' || (code == '~' && param == 4)) {
                if (pos < len) { char mv[16]; snprintf(mv, sizeof(mv), "\x1b[%dC", len - pos); ws(mv); pos = len; }
            }
            else if (code == '~' && param == 3 && pos < len) {
                for (int i = pos; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; buf[len] = '\0';
                write(1, buf + pos, len - pos); write(1, " ", 1);
                if (len - pos + 1 > 0) { char mv[16]; snprintf(mv, sizeof(mv), "\x1b[%dD", len - pos + 1); ws(mv); }
            }
            continue;
        }
        if ((unsigned char)c >= 0x20 && len < max - 1) {
            for (int i = len; i > pos; i--) buf[i] = buf[i - 1];
            buf[pos] = c; len++; buf[len] = '\0';
            write(1, buf + pos, len - pos);
            if (len - pos - 1 > 0) { char mv[16]; snprintf(mv, sizeof(mv), "\x1b[%dD", len - pos - 1); ws(mv); }
            pos++;
        }
    }
}

CERVUS_MAIN(calc_main) {
    int real_argc = 0;
    for (int i = 1; i < argc; i++) if (!is_shell_flag(argv[i])) real_argc++;

    if (real_argc >= 1) {
        build_expr(argc, argv);
        g_pos = g_expr; g_err = 0;
        double result = parse_expr();
        if (!g_err) { ws("= "); print_double(result); wn(); }
        exit(g_err ? 1 : 0);
    }

    ws("  Cervus Calc — floating-point arithmetic\n");
    ws("  Operators: + - * / % ^ ( )\n");
    ws("  Supports decimals (3.14) and exponents (1e3)\n");
    ws("  Type 'exit' to quit.\n\n");

    char line[256];
    for (;;) {
        ws("> ");
        int n = readline_edit(line, sizeof(line));
        if (n < 0) break;
        if (n == 0) continue;
        if (strcmp(line, "exit") == 0) break;
        g_pos = line; g_err = 0;
        double result = parse_expr();
        if (!g_err) { ws("= "); print_double(result); wn(); }
    }
    exit(0);
}
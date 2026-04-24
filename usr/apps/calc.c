#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <cervus_util.h>

#define SCALE   1000000LL

typedef long long fx;

static const char *p;

static void skip_ws(void) { while (*p && isspace((unsigned char)*p)) p++; }

static fx parse_number(int *err)
{
    skip_ws();
    long long intpart = 0, frac = 0, fscale = 1;
    int had_digit = 0;
    while (isdigit((unsigned char)*p)) {
        intpart = intpart * 10 + (*p - '0');
        had_digit = 1; p++;
    }
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p)) {
            if (fscale < SCALE) { frac = frac * 10 + (*p - '0'); fscale *= 10; }
            had_digit = 1; p++;
        }
    }
    if (!had_digit) { *err = 1; return 0; }
    return intpart * SCALE + (frac * SCALE) / fscale;
}

static fx parse_expr(int *err);

static fx parse_primary(int *err)
{
    skip_ws();
    if (*p == '(') {
        p++;
        fx v = parse_expr(err);
        skip_ws();
        if (*p == ')') p++;
        else           *err = 1;
        return v;
    }
    if (*p == '-') { p++; return -parse_primary(err); }
    if (*p == '+') { p++; return  parse_primary(err); }
    return parse_number(err);
}

static fx parse_term(int *err)
{
    fx v = parse_primary(err);
    while (!*err) {
        skip_ws();
        if (*p == '*') {
            p++;
            fx r = parse_primary(err);
            v = (v * r) / SCALE;
        } else if (*p == '/') {
            p++;
            fx r = parse_primary(err);
            if (r == 0) { *err = 2; return 0; }
            v = (v * SCALE) / r;
        } else break;
    }
    return v;
}

static fx parse_expr(int *err)
{
    fx v = parse_term(err);
    while (!*err) {
        skip_ws();
        if (*p == '+') { p++; v += parse_term(err); }
        else if (*p == '-') { p++; v -= parse_term(err); }
        else break;
    }
    return v;
}

static void print_fx(fx v)
{
    if (v < 0) { putchar('-'); v = -v; }
    long long ip = v / SCALE;
    long long fp = v % SCALE;

    char ibuf[32];
    int ilen = 0;
    if (ip == 0) {
        ibuf[ilen++] = '0';
    } else {
        long long tmp = ip;
        while (tmp > 0) { ibuf[ilen++] = '0' + (int)(tmp % 10); tmp /= 10; }
        for (int a = 0, b = ilen - 1; a < b; a++, b--) {
            char t = ibuf[a]; ibuf[a] = ibuf[b]; ibuf[b] = t;
        }
    }
    ibuf[ilen] = '\0';
    fputs(ibuf, stdout);

    if (fp != 0) {
        char fbuf[8];
        int flen = 0;
        long long tmp = fp;
        long long scale = SCALE;
        fbuf[flen++] = '.';
        while (scale > 1) {
            scale /= 10;
            fbuf[flen++] = '0' + (int)(tmp / scale);
            tmp %= scale;
        }
        fbuf[flen] = '\0';
        while (flen > 1 && fbuf[flen - 1] == '0') { fbuf[--flen] = '\0'; }
        fputs(fbuf, stdout);
    }
    putchar('\n');
}

static void calc_help(void)
{
    putchar('\n');
    fputs(C_CYAN "Cervus calc" C_RESET " - fixed-point calculator (6 decimal digits)\n", stdout);
    fputs(C_GRAY "-------------------------------------------" C_RESET "\n", stdout);
    fputs(C_BOLD "Operators:" C_RESET "  +  -  *  /  ( )\n", stdout);
    fputs(C_GRAY "-------------------------------------------" C_RESET "\n", stdout);
    fputs(C_BOLD "Examples:" C_RESET "\n", stdout);
    fputs("  calc> " C_YELLOW "3.14 + 5" C_RESET "        = 8.14\n", stdout);
    fputs("  calc> " C_YELLOW "10 / 3" C_RESET "          = 3.333333\n", stdout);
    fputs("  calc> " C_YELLOW "2 * (3 + 4)" C_RESET "     = 14\n", stdout);
    fputs("  calc> " C_YELLOW "(1.5 + 2.5) * 4" C_RESET " = 16\n", stdout);
    fputs("  calc> " C_YELLOW "100 / 7" C_RESET "         = 14.285714\n", stdout);
    fputs("  calc> " C_YELLOW "-5 + 3" C_RESET "          = -2\n", stdout);
    fputs(C_GRAY "-------------------------------------------" C_RESET "\n", stdout);
    fputs("  Type " C_BOLD "q" C_RESET " or " C_BOLD "exit" C_RESET " to quit.\n", stdout);
    putchar('\n');
}

static int readline_calc(char *buf, int maxlen)
{
    int i = 0;
    for (;;) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) {
            buf[i] = '\0';
            return (i > 0) ? i : -1;
        }
        if (c == '\r') continue;
        if (c == '\n') {
            write(1, "\n", 1);
            buf[i] = '\0';
            return i;
        }
        if (c == '\b' || c == 0x7F) {
            if (i > 0) {
                i--;
                write(1, "\b \b", 3);
            }
            continue;
        }
        if (c == 0x03) {
            write(1, "^C\n", 3);
            buf[0] = '\0';
            return 0;
        }
        if (c >= 0x20 && c < 0x7F && i < maxlen - 1) {
            buf[i++] = c;
            write(1, &c, 1);
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    fputs(C_CYAN "Cervus calc" C_RESET " - fixed-point (6 digits). "
          "Type " C_BOLD "help" C_RESET " for examples, " C_BOLD "q" C_RESET " to exit.\n", stdout);

    char line[256];
    for (;;) {
        fputs("calc> ", stdout);
        int n = readline_calc(line, sizeof(line));
        if (n < 0) { putchar('\n'); break; }
        while (n > 0 && isspace((unsigned char)line[n - 1])) line[--n] = '\0';
        if (n == 0) continue;
        if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0 ||
            strcmp(line, "exit") == 0) break;
        if (strcmp(line, "help") == 0) { calc_help(); continue; }

        p = line;
        int err = 0;
        fx v = parse_expr(&err);
        skip_ws();
        if (*p != '\0') err = 1;

        if (err == 1)       fputs(C_RED "  parse error\n" C_RESET, stdout);
        else if (err == 2)  fputs(C_RED "  division by zero\n" C_RESET, stdout);
        else { fputs("  = ", stdout); print_fx(v); }
    }
    putchar('\n');
    return 0;
}
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

extern uint64_t __cervus_strtod_bits(const char *s, char **endptr);

static int __is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int vsscanf(const char *str, const char *fmt, va_list ap)
{
    if (!str || !fmt) return -1;
    const char *s = str;
    const char *f = fmt;
    int matched = 0;
    int saw_anything = 0;

    while (*f) {
        if (__is_space((unsigned char)*f)) {
            while (__is_space((unsigned char)*s)) s++;
            f++;
            continue;
        }
        if (*f != '%') {
            if (*s != *f) return saw_anything ? matched : (matched ? matched : -1);
            s++; f++;
            continue;
        }
        f++;

        int suppress = 0;
        if (*f == '*') { suppress = 1; f++; }

        int width = 0;
        int has_width = 0;
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            has_width = 1;
            f++;
        }
        if (!has_width) width = 0;

        int len_mod = 0;
        if (*f == 'h') { f++; if (*f == 'h') { f++; len_mod = 2; } else len_mod = 1; }
        else if (*f == 'l') { f++; if (*f == 'l') { f++; len_mod = 4; } else len_mod = 3; }
        else if (*f == 'z') { f++; len_mod = 5; }
        else if (*f == 'j' || *f == 't') { f++; len_mod = 4; }

        char conv = *f;
        if (!conv) break;
        f++;

        if (conv == '%') {
            if (*s != '%') return matched ? matched : -1;
            s++;
            continue;
        }

        if (conv == 'n') {
            if (!suppress) {
                int read = (int)(s - str);
                if (len_mod == 3 || len_mod == 5) *va_arg(ap, long *) = read;
                else if (len_mod == 4) *va_arg(ap, long long *) = read;
                else *va_arg(ap, int *) = read;
            }
            continue;
        }

        if (conv == 'c') {
            int w = has_width ? width : 1;
            char *out = suppress ? NULL : va_arg(ap, char *);
            for (int i = 0; i < w; i++) {
                if (!*s) return matched ? matched : -1;
                if (out) out[i] = *s;
                s++;
            }
            if (!suppress) matched++;
            saw_anything = 1;
            continue;
        }

        if (conv != '[') {
            while (__is_space((unsigned char)*s)) s++;
        }

        if (conv == 's') {
            int w = has_width ? width : 0x7FFFFFFF;
            char *out = suppress ? NULL : va_arg(ap, char *);
            int wrote = 0;
            if (!*s) return matched ? matched : -1;
            while (*s && !__is_space((unsigned char)*s) && wrote < w) {
                if (out) out[wrote] = *s;
                s++; wrote++;
            }
            if (out) out[wrote] = '\0';
            if (!suppress) matched++;
            saw_anything = 1;
            continue;
        }

        if (conv == '[') {
            int negate = 0;
            if (*f == '^') { negate = 1; f++; }
            char set[256];
            memset(set, 0, sizeof(set));
            if (*f == ']') { set[(unsigned char)']'] = 1; f++; }
            while (*f && *f != ']') { set[(unsigned char)*f] = 1; f++; }
            if (*f == ']') f++;
            int w = has_width ? width : 0x7FFFFFFF;
            char *out = suppress ? NULL : va_arg(ap, char *);
            int wrote = 0;
            while (*s && wrote < w) {
                int in = set[(unsigned char)*s] != 0;
                if (negate) in = !in;
                if (!in) break;
                if (out) out[wrote] = *s;
                s++; wrote++;
            }
            if (out) out[wrote] = '\0';
            if (wrote == 0) return matched ? matched : -1;
            if (!suppress) matched++;
            saw_anything = 1;
            continue;
        }

        if (conv == 'd' || conv == 'i' || conv == 'u' ||
            conv == 'o' || conv == 'x' || conv == 'X')
        {
            int base = 10;
            int allow_neg = (conv == 'd' || conv == 'i');
            if (conv == 'o') base = 8;
            else if (conv == 'x' || conv == 'X') base = 16;

            const char *start = s;
            int neg = 0;
            if (allow_neg && (*s == '+' || *s == '-')) { neg = (*s == '-'); s++; }
            else if (conv == 'u' && (*s == '+')) s++;
            if (conv == 'i' || conv == 'x' || conv == 'X') {
                if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                    base = 16; s += 2;
                } else if (conv == 'i' && s[0] == '0' && s[1] >= '0' && s[1] <= '7') {
                    base = 8; s++;
                }
            }
            unsigned long long val = 0;
            int got_digit = 0;
            int wleft = has_width ? width - (int)(s - start) : 0x7FFFFFFF;
            while (*s && wleft > 0) {
                int d = -1;
                if (*s >= '0' && *s <= '9') d = *s - '0';
                else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
                else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
                if (d < 0 || d >= base) break;
                val = val * base + (unsigned)d;
                s++; wleft--;
                got_digit = 1;
            }
            if (!got_digit) return matched ? matched : -1;
            if (!suppress) {
                if (allow_neg && neg) {
                    long long sv = -(long long)val;
                    if (len_mod == 4) *va_arg(ap, long long *) = sv;
                    else if (len_mod == 3 || len_mod == 5) *va_arg(ap, long *) = (long)sv;
                    else if (len_mod == 1) *va_arg(ap, short *) = (short)sv;
                    else if (len_mod == 2) *va_arg(ap, signed char *) = (signed char)sv;
                    else *va_arg(ap, int *) = (int)sv;
                } else {
                    if (len_mod == 4) *va_arg(ap, unsigned long long *) = val;
                    else if (len_mod == 3 || len_mod == 5) *va_arg(ap, unsigned long *) = (unsigned long)val;
                    else if (len_mod == 1) *va_arg(ap, unsigned short *) = (unsigned short)val;
                    else if (len_mod == 2) *va_arg(ap, unsigned char *) = (unsigned char)val;
                    else *va_arg(ap, unsigned int *) = (unsigned int)val;
                }
                matched++;
            }
            saw_anything = 1;
            continue;
        }

        if (conv == 'f' || conv == 'e' || conv == 'g' ||
            conv == 'E' || conv == 'G' || conv == 'a' || conv == 'A')
        {
            char *endp;
            uint64_t bits = __cervus_strtod_bits(s, &endp);
            if (endp == s) return matched ? matched : -1;
            if (!suppress) {
                if (len_mod == 3) {
                    double v;
                    __builtin_memcpy(&v, &bits, sizeof(double));
                    *va_arg(ap, double *) = v;
                } else {
                    double dv;
                    __builtin_memcpy(&dv, &bits, sizeof(double));
                    *va_arg(ap, float *) = (float)dv;
                }
                matched++;
            }
            s = endp;
            saw_anything = 1;
            continue;
        }

        return matched ? matched : -1;
    }
    return matched;
}

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsscanf(str, fmt, ap);
    va_end(ap);
    return n;
}

int vscanf(const char *fmt, va_list ap)
{
    char buf[1024];
    if (!fgets(buf, (int)sizeof(buf), stdin)) return EOF;
    return vsscanf(buf, fmt, ap);
}

int scanf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vscanf(fmt, ap);
    va_end(ap);
    return n;
}

int vfscanf(FILE *stream, const char *fmt, va_list ap)
{
    char buf[1024];
    if (!fgets(buf, (int)sizeof(buf), stream)) return EOF;
    return vsscanf(buf, fmt, ap);
}

int fscanf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfscanf(stream, fmt, ap);
    va_end(ap);
    return n;
}
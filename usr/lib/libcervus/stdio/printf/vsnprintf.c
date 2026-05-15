#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static void __u64_to_str(uint64_t v, char *out, int base, int upper)
{
    char tmp[32];
    int i = 0;
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = digs[v % (uint64_t)base]; v /= (uint64_t)base; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

static int __f_classify(double v)
{
    union { double d; uint64_t u; } x;
    x.d = v;
    uint64_t bits = x.u;
    uint64_t exp  = (bits >> 52) & 0x7ffULL;
    uint64_t frac =  bits & 0xfffffffffffffULL;
    if (exp == 0x7ff) {
        if (frac == 0) return 1;
        return 2;
    }
    return 0;
}

static int __f_signbit(double v)
{
    union { double d; uint64_t u; } x;
    x.d = v;
    return (int)((x.u >> 63) & 1ULL);
}

static double __f_pow10(int n)
{
    double r = 1.0;
    if (n >= 0) { while (n--) r *= 10.0; }
    else        { while (n++) r /= 10.0; }
    return r;
}

static void __f_to_str(double v, int prec, int upper, char *out)
{
    int cls = __f_classify(v);
    int neg = __f_signbit(v);
    if (cls == 2) {
        const char *s = upper ? "NAN" : "nan";
        int i = 0; while (s[i]) { out[i] = s[i]; i++; } out[i] = 0;
        return;
    }
    if (cls == 1) {
        int i = 0;
        if (neg) out[i++] = '-';
        const char *s = upper ? "INF" : "inf";
        int k = 0; while (s[k]) out[i++] = s[k++];
        out[i] = 0;
        return;
    }
    if (prec < 0) prec = 6;
    if (prec > 17) prec = 17;

    double av = neg ? -v : v;
    double scale = __f_pow10(prec);
    double rounded = av * scale + 0.5;
    uint64_t big;
    if (rounded > 1.8446744073709551e19) big = 0xffffffffffffffffULL;
    else big = (uint64_t)rounded;

    uint64_t divp = 1;
    for (int i = 0; i < prec; i++) divp *= 10ULL;
    uint64_t ip = divp ? big / divp : big;
    uint64_t fp = divp ? big % divp : 0;

    char ibuf[32];
    __u64_to_str(ip, ibuf, 10, 0);

    int o = 0;
    if (neg) out[o++] = '-';
    int k = 0; while (ibuf[k]) out[o++] = ibuf[k++];

    if (prec > 0) {
        out[o++] = '.';
        char fbuf[32];
        __u64_to_str(fp, fbuf, 10, 0);
        int flen = 0; while (fbuf[flen]) flen++;
        for (int i = flen; i < prec; i++) out[o++] = '0';
        int m = 0; while (fbuf[m]) out[o++] = fbuf[m++];
    }
    out[o] = 0;
}

int vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap)
{
    size_t pos = 0;
#define __PUT(s, n) do { \
    size_t __n = (n); const char *__s = (s); \
    for (size_t __i = 0; __i < __n; __i++) { \
        if (pos + 1 < sz) buf[pos] = __s[__i]; \
        pos++; \
    } \
} while (0)

    while (*fmt) {
        if (*fmt != '%') { __PUT(fmt, 1); fmt++; continue; }
        fmt++;
        int pad_zero = 0, left_align = 0, plus_flag = 0;
        while (*fmt == '0' || *fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#') {
            if (*fmt == '0') pad_zero = 1;
            else if (*fmt == '-') left_align = 1;
            else if (*fmt == '+') plus_flag = 1;
            fmt++;
        }
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left_align = 1; width = -width; } fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); if (prec < 0) prec = 0; fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }
        int is_long = 0, is_size_t = 0;
        while (*fmt == 'l') { is_long++; fmt++; }
        if (*fmt == 'z') { is_size_t = 1; fmt++; }
        if (*fmt == 'h') { fmt++; }

        char nb[40];
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                size_t l = strlen(s);
                if (prec >= 0 && (size_t)prec < l) l = (size_t)prec;
                int pad = (int)(width > (int)l ? width - (int)l : 0);
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                __PUT(s, l);
                if (left_align)  for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'd': case 'i': {
                int64_t v;
                if (is_long >= 2)   v = va_arg(ap, long long);
                else if (is_long)   v = va_arg(ap, long);
                else if (is_size_t) v = (int64_t)va_arg(ap, size_t);
                else                v = va_arg(ap, int);
                int neg = v < 0;
                uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
                __u64_to_str(u, nb, 10, 0);
                int numlen = (int)strlen(nb) + (neg || plus_flag ? 1 : 0);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align && !pad_zero) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                if (neg)           __PUT("-", 1);
                else if (plus_flag) __PUT("+", 1);
                if (!left_align && pad_zero) for (int i = 0; i < pad; i++) __PUT("0", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'u': {
                uint64_t v;
                if (is_long >= 2)   v = va_arg(ap, unsigned long long);
                else if (is_long)   v = va_arg(ap, unsigned long);
                else if (is_size_t) v = va_arg(ap, size_t);
                else                v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 10, 0);
                int numlen = (int)strlen(nb);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(pad_zero ? "0" : " ", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'x': case 'X': {
                uint64_t v;
                if (is_long >= 2)   v = va_arg(ap, unsigned long long);
                else if (is_long)   v = va_arg(ap, unsigned long);
                else if (is_size_t) v = va_arg(ap, size_t);
                else                v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 16, *fmt == 'X');
                int numlen = (int)strlen(nb);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(pad_zero ? "0" : " ", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'o': {
                uint64_t v;
                if (is_long >= 2) v = va_arg(ap, unsigned long long);
                else if (is_long) v = va_arg(ap, unsigned long);
                else              v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 8, 0);
                __PUT(nb, strlen(nb));
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
                __PUT("0x", 2);
                __u64_to_str(v, nb, 16, 0);
                __PUT(nb, strlen(nb));
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                __PUT(&c, 1);
                break;
            }
            case 'f': case 'F': case 'g': case 'G': case 'e': case 'E': {
                double v = va_arg(ap, double);
                int upper = (*fmt == 'F' || *fmt == 'G' || *fmt == 'E');
                int eff_prec = prec;
                if ((*fmt == 'g' || *fmt == 'G') && eff_prec < 0) eff_prec = 6;
                if ((*fmt == 'g' || *fmt == 'G') && eff_prec == 0) eff_prec = 1;
                char fbuf[64];
                __f_to_str(v, eff_prec, upper, fbuf);
                int numlen = (int)strlen(fbuf);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align && !pad_zero) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                if (!left_align && pad_zero) for (int i = 0; i < pad; i++) __PUT("0", 1);
                __PUT(fbuf, (size_t)numlen);
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case '%': __PUT("%", 1); break;
            default: {
                char c = *fmt;
                __PUT("%", 1);
                __PUT(&c, 1);
                break;
            }
        }
        fmt++;
    }
    if (sz > 0) buf[pos < sz ? pos : sz - 1] = '\0';
#undef __PUT
    return (int)pos;
}

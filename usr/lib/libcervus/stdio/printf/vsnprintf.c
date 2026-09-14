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

#define __BN_WORDS 96
#define __BN_BASE   1000000000ULL

typedef struct { uint32_t w[__BN_WORDS]; int n; } __bn;

static void __bn_set(__bn *b, uint64_t v)
{
    b->n = 0;
    while (v && b->n < __BN_WORDS) {
        b->w[b->n++] = (uint32_t)(v % __BN_BASE);
        v /= __BN_BASE;
    }
    if (!b->n) { b->w[0] = 0; b->n = 1; }
}

static void __bn_mul(__bn *b, uint32_t k)
{
    uint64_t carry = 0;
    for (int i = 0; i < b->n; i++) {
        uint64_t cur = (uint64_t)b->w[i] * k + carry;
        b->w[i] = (uint32_t)(cur % __BN_BASE);
        carry = cur / __BN_BASE;
    }
    while (carry && b->n < __BN_WORDS) {
        b->w[b->n++] = (uint32_t)(carry % __BN_BASE);
        carry /= __BN_BASE;
    }
}

static int __bn_to_digits(const __bn *b, char *out)
{
    int n = 0;
    uint32_t hi = b->w[b->n - 1];
    char tmp[12];
    int t = 0;
    if (!hi) tmp[t++] = '0';
    while (hi) { tmp[t++] = (char)('0' + hi % 10); hi /= 10; }
    while (t) out[n++] = tmp[--t];

    for (int i = b->n - 2; i >= 0; i--) {
        uint32_t w = b->w[i];
        for (int d = 8; d >= 0; d--) {
            out[n + d] = (char)('0' + w % 10);
            w /= 10;
        }
        n += 9;
    }
    out[n] = '\0';
    return n;
}

static int __f_digits(double av, char *digits, int *pndig)
{
    union { double d; uint64_t u; } x;
    x.d = av;

    uint64_t frac = x.u & 0xfffffffffffffULL;
    int      be   = (int)((x.u >> 52) & 0x7ffULL);

    uint64_t m;
    int e2;
    if (be == 0) { m = frac;                    e2 = -1074; }
    else         { m = frac | (1ULL << 52);     e2 = be - 1075; }

    __bn b;
    __bn_set(&b, m);

    int shift10 = 0;
    if (e2 > 0) {
        int q = e2;
        while (q >= 29) { __bn_mul(&b, 1u << 29); q -= 29; }
        if (q) __bn_mul(&b, 1u << q);
    } else if (e2 < 0) {
        int q = -e2;
        shift10 = -q;
        while (q >= 13) { __bn_mul(&b, 1220703125u); q -= 13; }
        while (q--) __bn_mul(&b, 5u);
    }

    int len = __bn_to_digits(&b, digits);

    int lead = 0;
    while (lead < len - 1 && digits[lead] == '0') lead++;
    if (lead) {
        for (int i = 0; i + lead <= len; i++) digits[i] = digits[i + lead];
        len -= lead;
    }

    *pndig = len;
    return len - 1 + shift10;
}

static void __f_round(char *digits, int *pn, int keep, int *pexp)
{
    int n = *pn;
    if (keep >= n) return;

    if (keep < 0) {
        digits[0] = '0';
        digits[1] = '\0';
        *pn = 1;
        *pexp = 0;
        return;
    }

    int up;
    if (digits[keep] > '5') {
        up = 1;
    } else if (digits[keep] < '5') {
        up = 0;
    } else {
        up = 0;
        for (int i = keep + 1; i < n; i++)
            if (digits[i] != '0') { up = 1; break; }
        if (!up) up = ((keep > 0 ? digits[keep - 1] : '0') - '0') & 1;
    }

    if (keep == 0) {
        digits[0] = up ? '1' : '0';
        digits[1] = '\0';
        *pn = 1;
        if (up) (*pexp)++;
        else    *pexp = 0;
        return;
    }

    if (up) {
        int i = keep - 1;
        for (; i >= 0; i--) {
            if (digits[i] != '9') { digits[i]++; break; }
            digits[i] = '0';
        }
        if (i < 0) {
            for (int k = keep - 1; k > 0; k--) digits[k] = digits[k - 1];
            digits[0] = '1';
            (*pexp)++;
        }
    }
    digits[keep] = '\0';
    *pn = keep;
}

static int __f_emit_fixed(char *out, int o, const char *digits, int ndig,
                          int exp10, int prec, int trim)
{
    int intlen = exp10 + 1;

    if (intlen <= 0) {
        out[o++] = '0';
    } else {
        for (int i = 0; i < intlen; i++)
            out[o++] = (i < ndig) ? digits[i] : '0';
    }

    if (prec <= 0) { out[o] = '\0'; return o; }

    int start = o;
    out[o++] = '.';
    for (int i = 0; i < prec; i++) {
        int idx = intlen + i;
        out[o++] = (idx >= 0 && idx < ndig) ? digits[idx] : '0';
    }

    if (trim) {
        while (o > start + 1 && out[o - 1] == '0') o--;
        if (o == start + 1) o = start;
    }
    out[o] = '\0';
    return o;
}

static int __f_emit_sci(char *out, int o, const char *digits, int ndig,
                        int exp10, int prec, int upper, int trim)
{
    out[o++] = digits[0];

    if (prec > 0) {
        int start = o;
        out[o++] = '.';
        for (int i = 1; i <= prec; i++)
            out[o++] = (i < ndig) ? digits[i] : '0';
        if (trim) {
            while (o > start + 1 && out[o - 1] == '0') o--;
            if (o == start + 1) o = start;
        }
    }

    out[o++] = upper ? 'E' : 'e';
    out[o++] = exp10 < 0 ? '-' : '+';

    int ae = exp10 < 0 ? -exp10 : exp10;
    char eb[8];
    int n = 0;
    do { eb[n++] = (char)('0' + ae % 10); ae /= 10; } while (ae);
    while (n < 2) eb[n++] = '0';
    while (n > 0) out[o++] = eb[--n];

    out[o] = '\0';
    return o;
}

static void __f_to_str(double v, int prec, int upper, char conv, char *out)
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
    if (prec > 300) prec = 300;

    double av = neg ? -v : v;
    int o = 0;
    if (neg) out[o++] = '-';

    char digits[900];
    int ndig;
    int exp10;

    if (av == 0.0) {
        digits[0] = '0';
        digits[1] = '\0';
        ndig = 1;
        exp10 = 0;
    } else {
        exp10 = __f_digits(av, digits, &ndig);
    }

    if (conv == 'e') {
        if (av != 0.0) __f_round(digits, &ndig, prec + 1, &exp10);
        __f_emit_sci(out, o, digits, ndig, exp10, prec, upper, 0);
        return;
    }

    if (conv == 'g') {
        int p = prec < 1 ? 1 : prec;
        if (av != 0.0) __f_round(digits, &ndig, p, &exp10);
        if (exp10 < -4 || exp10 >= p)
            __f_emit_sci(out, o, digits, ndig, exp10, p - 1, upper, 1);
        else
            __f_emit_fixed(out, o, digits, ndig, exp10, p - 1 - exp10, 1);
        return;
    }

    if (av != 0.0) __f_round(digits, &ndig, exp10 + prec + 1, &exp10);
    __f_emit_fixed(out, o, digits, ndig, exp10, prec, 0);
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
                if (is_long >= 2)   v = va_arg(ap, unsigned long long);
                else if (is_long)   v = va_arg(ap, unsigned long);
                else if (is_size_t) v = va_arg(ap, size_t);
                else                v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 8, 0);
                int numlen = (int)strlen(nb);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(pad_zero ? "0" : " ", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
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
                char conv = (*fmt == 'g' || *fmt == 'G') ? 'g'
                          : (*fmt == 'e' || *fmt == 'E') ? 'e' : 'f';
                int eff_prec = prec;
                if (conv == 'g' && eff_prec < 0) eff_prec = 6;
                if (conv == 'g' && eff_prec == 0) eff_prec = 1;
                char fbuf[700];
                __f_to_str(v, eff_prec, upper, conv, fbuf);
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

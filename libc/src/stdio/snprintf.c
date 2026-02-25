#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    char*  buf;
    size_t pos;
    size_t size;
} snprintf_ctx_t;

static void ctx_putc(snprintf_ctx_t* ctx, char c) {
    if (ctx->pos + 1 < ctx->size)
        ctx->buf[ctx->pos] = c;
    ctx->pos++;
}

static void ctx_puts(snprintf_ctx_t* ctx, const char* s, int len) {
    for (int i = 0; i < len; i++) ctx_putc(ctx, s[i]);
}

static void write_uint_full(snprintf_ctx_t* ctx,
                             unsigned long long val, int base, int upper,
                             int width, int zero_pad, int left,
                             int alt, int show_sign, int space_sign,
                             char forced_sign) {
    const char* digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[72]; int len = 0;
    if (val == 0) { tmp[len++] = '0'; }
    else { while (val) { tmp[len++] = digs[val % (unsigned)base]; val /= (unsigned)base; } }

    char prefix[4]; int pfx = 0;
    if      (forced_sign)  prefix[pfx++] = forced_sign;
    else if (show_sign)    prefix[pfx++] = '+';
    else if (space_sign)   prefix[pfx++] = ' ';
    if (alt && base == 16) { prefix[pfx++] = '0'; prefix[pfx++] = upper ? 'X':'x'; }
    else if (alt && base == 8 && !(len==1 && tmp[0]=='0')) { prefix[pfx++] = '0'; }
    else if (alt && base == 2) { prefix[pfx++] = '0'; prefix[pfx++] = upper ? 'B':'b'; }

    int total = len + pfx;
    int pad   = (width > total) ? width - total : 0;

    if (!left && !zero_pad) { for (int i=0;i<pad;i++) ctx_putc(ctx,' '); }
    ctx_puts(ctx, prefix, pfx);
    if (!left &&  zero_pad) { for (int i=0;i<pad;i++) ctx_putc(ctx,'0'); }
    for (int i = len-1; i >= 0; i--) ctx_putc(ctx, tmp[i]);
    if ( left)              { for (int i=0;i<pad;i++) ctx_putc(ctx,' '); }
}

static void write_sint(snprintf_ctx_t* ctx, long long val,
                        int width, int zero_pad, int left,
                        int show_sign, int space_sign) {
    char sign = 0;
    unsigned long long uval;
    if (val < 0) { sign = '-'; uval = (unsigned long long)(-val); }
    else         { uval = (unsigned long long)val; }
    write_uint_full(ctx, uval, 10, 0, width, zero_pad, left,
                    0, show_sign, space_sign, sign);
}

static const unsigned long long pow10_tbl[18] = {
    1ULL, 10ULL, 100ULL, 1000ULL, 10000ULL, 100000ULL,
    1000000ULL, 10000000ULL, 100000000ULL, 1000000000ULL,
    10000000000ULL, 100000000000ULL, 1000000000000ULL,
    10000000000000ULL, 100000000000000ULL, 1000000000000000ULL,
    10000000000000000ULL, 100000000000000000ULL
};

static int ilog10(double val) {
    int e = 0;
    if (val >= 1.0) {
        if (val >= 1e16) { val /= 1e16; e += 16; }
        if (val >= 1e8)  { val /= 1e8;  e += 8;  }
        if (val >= 1e4)  { val /= 1e4;  e += 4;  }
        if (val >= 1e2)  { val /= 1e2;  e += 2;  }
        if (val >= 1e1)  { val /= 1e1;  e += 1;  }
    } else {
        if (val < 1e-15) { val *= 1e16; e -= 16; }
        if (val < 1e-7)  { val *= 1e8;  e -= 8;  }
        if (val < 1e-3)  { val *= 1e4;  e -= 4;  }
        if (val < 1e-1)  { val *= 1e2;  e -= 2;  }
        if (val < 1e0)   { val *= 1e1;  e -= 1;  }
    }
    (void)val;
    return e;
}

static int double_to_str(char* buf, double val, char fmt_char, int precision,
                          int show_sign, int space_sign, int alt, int is_neg) {
    if (precision < 0)  precision = 6;
    if (precision > 17) precision = 17;

    int upper = (fmt_char == 'F' || fmt_char == 'E' || fmt_char == 'G');
    int i = 0;

    if      (is_neg)     buf[i++] = '-';
    else if (show_sign)  buf[i++] = '+';
    else if (space_sign) buf[i++] = ' ';

    int exp10 = (val != 0.0) ? ilog10(val) : 0;

    int use_exp;
    if (fmt_char == 'e' || fmt_char == 'E') {
        use_exp = 1;
    } else if (fmt_char == 'g' || fmt_char == 'G') {
        int eff = (precision == 0) ? 1 : precision;
        use_exp = (exp10 < -4 || exp10 >= eff);
        precision = use_exp ? (eff - 1) : (eff - 1 - exp10);
        if (precision < 0)  precision = 0;
        if (precision > 17) precision = 17;
    } else {
        use_exp = (exp10 > 15 || (val != 0.0 && exp10 < -17));
        if (use_exp) { fmt_char = upper ? 'E' : 'e'; upper = (fmt_char == 'E'); }
    }

    double src = val;
    if (use_exp && val != 0.0) {
        int e = exp10;
        if (e > 0 && e <= 17)       src /= (double)pow10_tbl[e];
        else if (e > 17)            { src /= (double)pow10_tbl[17]; src /= (double)pow10_tbl[e - 17 < 17 ? e-17 : 17]; }
        else if (e < 0 && -e <= 17) src *= (double)pow10_tbl[-e];
        else if (e < -17)           { src *= (double)pow10_tbl[17]; src *= (double)pow10_tbl[-e-17 < 17 ? -e-17 : 17]; }
        if (src >= 10.0) { src /= 10.0; exp10++; }
        if (src <  1.0 && src != 0.0) { src *= 10.0; exp10--; }
    }

    unsigned long long scale    = pow10_tbl[precision];
    double             scaled_d = src * (double)scale + 0.5;
    if (scaled_d >= 1.8e19) scaled_d = 1.8e19 - 1.0;
    unsigned long long iscaled   = (unsigned long long)scaled_d;
    unsigned long long int_part  = iscaled / scale;
    unsigned long long frac_part = iscaled % scale;

    if (use_exp && int_part >= 10) { int_part = 1; frac_part = 0; exp10++; }

    {
        char itmp[24]; int ilen = 0;
        unsigned long long ip = int_part;
        if (ip == 0) itmp[ilen++] = '0';
        else while (ip) { itmp[ilen++] = '0' + (int)(ip % 10); ip /= 10; }
        for (int j = ilen-1; j >= 0; j--) buf[i++] = itmp[j];
    }

    if (precision > 0 || alt) {
        char ftmp[20];
        unsigned long long fp2 = frac_part;
        for (int k = precision - 1; k >= 0; k--) {
            ftmp[k] = '0' + (int)(fp2 % 10);
            fp2 /= 10;
        }
        int fend = precision;
        if ((fmt_char == 'g' || fmt_char == 'G') && !alt) {
            while (fend > 0 && ftmp[fend-1] == '0') fend--;
        }
        if (fend > 0 || alt) {
            buf[i++] = '.';
            for (int k = 0; k < fend; k++) buf[i++] = ftmp[k];
        }
    }

    if (use_exp) {
        buf[i++] = upper ? 'E' : 'e';
        int ae = exp10 < 0 ? -exp10 : exp10;
        buf[i++] = exp10 < 0 ? '-' : '+';
        if (ae >= 100) buf[i++] = '0' + ae / 100;
        buf[i++] = '0' + (ae / 10) % 10;
        buf[i++] = '0' + ae % 10;
    }

    return i;
}

static void write_float(snprintf_ctx_t* ctx, double val, char fmt_char,
                         int precision, int width, int zero_pad, int left,
                         int show_sign, int space_sign, int alt) {
    char tmp[64]; int len;
    int upper = (fmt_char == 'F' || fmt_char == 'E' || fmt_char == 'G');

    if (val != val) {
        const char* s = upper ? "NAN" : "nan";
        len = 3; tmp[0]=s[0]; tmp[1]=s[1]; tmp[2]=s[2];
    } else {
        int is_neg = 0;
        if (val < 0.0) { is_neg = 1; val = -val; }
        if (val > 1.7976931348623157e+308) {
            len = 0;
            if      (is_neg)     tmp[len++] = '-';
            else if (show_sign)  tmp[len++] = '+';
            else if (space_sign) tmp[len++] = ' ';
            const char* s = upper ? "INF" : "inf";
            tmp[len++]=s[0]; tmp[len++]=s[1]; tmp[len++]=s[2];
        } else {
            len = double_to_str(tmp, val, fmt_char, precision,
                                show_sign, space_sign, alt, is_neg);
        }
    }

    int has_sign = (tmp[0]=='-' || tmp[0]=='+' || tmp[0]==' ');
    int pad = (width > len) ? width - len : 0;
    if (!left && !zero_pad) { for (int j=0;j<pad;j++) ctx_putc(ctx,' '); }
    if (!left &&  zero_pad) {
        if (has_sign) ctx_putc(ctx, tmp[0]);
        for (int j=0;j<pad;j++) ctx_putc(ctx,'0');
        ctx_puts(ctx, tmp + has_sign, len - has_sign);
    } else {
        ctx_puts(ctx, tmp, len);
    }
    if (left) { for (int j=0;j<pad;j++) ctx_putc(ctx,' '); }
}

int vsnprintf(char* restrict buf, size_t size, const char* restrict fmt,
              va_list ap) {
    snprintf_ctx_t ctx = { buf, 0, size ? size : 1 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') { ctx_putc(&ctx, *fmt); continue; }
        fmt++;
        if (!*fmt) break;

        int left=0, zero_pad=0, alt=0, show_sign=0, space_sign=0;
        for (;;) {
            if      (*fmt=='-') { left=1;       fmt++; }
            else if (*fmt=='0') { zero_pad=1;   fmt++; }
            else if (*fmt=='#') { alt=1;        fmt++; }
            else if (*fmt=='+') { show_sign=1;  fmt++; }
            else if (*fmt==' ') { space_sign=1; fmt++; }
            else break;
        }

        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) { left = 1; width = -width; }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') width = width*10 + (*fmt++ - '0');
        }

        int precision=-1, has_prec=0;
        if (*fmt == '.') {
            has_prec=1; precision=0; fmt++;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                if (precision < 0) { has_prec=0; precision=-1; }
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    precision = precision*10 + (*fmt++ - '0');
            }
        }

        int is_hh=0,is_h=0,is_l=0,is_ll=0,is_z=0;
        if (*fmt=='h') {
            fmt++;
            if (*fmt=='h') { is_hh=1; fmt++; } else is_h=1;
        } else if (*fmt=='l') {
            is_l=1; fmt++;
            if (*fmt=='l') { is_ll=1; fmt++; }
        } else if (*fmt=='z') { is_z=1; fmt++; }
          else if (*fmt=='t') { is_l=1; fmt++; }
          else if (*fmt=='L') { fmt++; }

        if (!*fmt) break;

        switch (*fmt) {
        case 'd': case 'i': {
            long long v;
            if      (is_ll) v = va_arg(ap, long long);
            else if (is_l)  v = va_arg(ap, long);
            else if (is_hh) v = (signed char) va_arg(ap, int);
            else if (is_h)  v = (short)       va_arg(ap, int);
            else if (is_z)  v = (long long)   va_arg(ap, size_t);
            else            v = va_arg(ap, int);
            write_sint(&ctx, v, width, zero_pad, left, show_sign, space_sign);
            break;
        }
        case 'u': {
            unsigned long long v;
            if      (is_ll) v = va_arg(ap, unsigned long long);
            else if (is_l)  v = va_arg(ap, unsigned long);
            else if (is_hh) v = (unsigned char) va_arg(ap, unsigned int);
            else if (is_h)  v = (unsigned short)va_arg(ap, unsigned int);
            else if (is_z)  v = va_arg(ap, size_t);
            else            v = va_arg(ap, unsigned int);
            write_uint_full(&ctx,v,10,0,width,zero_pad,left,0,show_sign,space_sign,0);
            break;
        }
        case 'o': {
            unsigned long long v;
            if      (is_ll) v = va_arg(ap, unsigned long long);
            else if (is_l)  v = va_arg(ap, unsigned long);
            else            v = va_arg(ap, unsigned int);
            write_uint_full(&ctx,v,8,0,width,zero_pad,left,alt,0,0,0);
            break;
        }
        case 'x': case 'X': {
            unsigned long long v;
            if      (is_ll) v = va_arg(ap, unsigned long long);
            else if (is_l)  v = va_arg(ap, unsigned long);
            else            v = va_arg(ap, unsigned int);
            write_uint_full(&ctx,v,16,(*fmt=='X'),width,zero_pad,left,alt,0,0,0);
            break;
        }
        case 'b': case 'B': {
            unsigned long long v;
            if      (is_ll) v = va_arg(ap, unsigned long long);
            else if (is_l)  v = va_arg(ap, unsigned long);
            else            v = va_arg(ap, unsigned int);
            write_uint_full(&ctx,v,2,(*fmt=='B'),width,zero_pad,left,alt,0,0,0);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void*);
            ctx_putc(&ctx,'0'); ctx_putc(&ctx,'x');
            write_uint_full(&ctx,(unsigned long long)v,16,0,width,1,left,0,0,0,0);
            break;
        }
        case 'f': case 'F':
        case 'e': case 'E':
        case 'g': case 'G': {
            double v = va_arg(ap, double);
            write_float(&ctx, v, *fmt,
                        has_prec ? precision : 6,
                        width, zero_pad, left, show_sign, space_sign, alt);
            break;
        }
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            if (has_prec && precision < slen) slen = precision;
            int pad = (width > slen) ? width - slen : 0;
            if (!left) { for (int j=0;j<pad;j++) ctx_putc(&ctx,' '); }
            ctx_puts(&ctx, s, slen);
            if ( left) { for (int j=0;j<pad;j++) ctx_putc(&ctx,' '); }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            int pad = (width > 1) ? width - 1 : 0;
            if (!left) { for (int j=0;j<pad;j++) ctx_putc(&ctx,' '); }
            ctx_putc(&ctx, c);
            if ( left) { for (int j=0;j<pad;j++) ctx_putc(&ctx,' '); }
            break;
        }
        case 'n': {
            int* p = va_arg(ap, int*);
            if (p) *p = (int)ctx.pos;
            break;
        }
        case '%': ctx_putc(&ctx,'%'); break;
        default:  ctx_putc(&ctx,'%'); ctx_putc(&ctx,*fmt); break;
        }
    }

    if (size > 0) buf[ctx.pos < size ? ctx.pos : size-1] = '\0';
    return (int)ctx.pos;
}

int vprintf(const char* restrict fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    for (int i = 0; i < n && buf[i]; i++) putchar(buf[i]);
    return n;
}

int snprintf(char* restrict buf, size_t size, const char* restrict fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap); return n;
}

int sprintf(char* restrict buf, const char* restrict fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap); return n;
}
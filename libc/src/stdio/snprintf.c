#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    char*  buf;
    size_t pos;
    size_t size;
} snprintf_ctx_t;

static void snprintf_putc(snprintf_ctx_t* ctx, char c) {
    if (ctx->pos + 1 < ctx->size)
        ctx->buf[ctx->pos] = c;
    ctx->pos++;
}

static void write_str(snprintf_ctx_t* ctx, const char* s) {
    while (*s) snprintf_putc(ctx, *s++);
}

static void write_uint(snprintf_ctx_t* ctx, unsigned long long val,
                       int base, int upper, int width, int zero_pad, int left) {
    char tmp[64];
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int len = 0;

    if (val == 0) { tmp[len++] = '0'; }
    else {
        while (val) {
            tmp[len++] = digits[val % (unsigned)base];
            val /= (unsigned)base;
        }
    }

    int pad = width - len;
    char pad_char = zero_pad ? '0' : ' ';

    if (!left) while (pad-- > 0) snprintf_putc(ctx, pad_char);
    for (int i = len - 1; i >= 0; i--) snprintf_putc(ctx, tmp[i]);
    if ( left) while (pad-- > 0) snprintf_putc(ctx, ' ');
}

int vsnprintf(char* restrict buf, size_t size, const char* restrict fmt, va_list ap) {
    snprintf_ctx_t ctx = { buf, 0, size ? size : 1 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') { snprintf_putc(&ctx, *fmt); continue; }
        fmt++;

        int left = 0, zero_pad = 0;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left     = 1;
            if (*fmt == '0') zero_pad = 1;
            fmt++;
        }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        int is_long = 0, is_llong = 0;
        if (*fmt == 'l') {
            is_long = 1; fmt++;
            if (*fmt == 'l') { is_llong = 1; fmt++; }
        } else if (*fmt == 'z') {
            is_llong = (sizeof(size_t) == 8);
            is_long  = !is_llong;
            fmt++;
        }

        switch (*fmt) {
        case 'd': case 'i': {
            long long v = is_llong ? va_arg(ap, long long)
                        : is_long  ? va_arg(ap, long)
                                   : va_arg(ap, int);
            if (v < 0) { snprintf_putc(&ctx, '-'); v = -v; }
            write_uint(&ctx, (unsigned long long)v, 10, 0, width, zero_pad, left);
            break;
        }
        case 'u':
            write_uint(&ctx,
                is_llong ? va_arg(ap, unsigned long long)
              : is_long  ? va_arg(ap, unsigned long)
                         : va_arg(ap, unsigned int),
                10, 0, width, zero_pad, left);
            break;
        case 'x':
            write_uint(&ctx,
                is_llong ? va_arg(ap, unsigned long long)
              : is_long  ? va_arg(ap, unsigned long)
                         : va_arg(ap, unsigned int),
                16, 0, width, zero_pad, left);
            break;
        case 'X':
            write_uint(&ctx,
                is_llong ? va_arg(ap, unsigned long long)
              : is_long  ? va_arg(ap, unsigned long)
                         : va_arg(ap, unsigned int),
                16, 1, width, zero_pad, left);
            break;
        case 'p':
            write_str(&ctx, "0x");
            write_uint(&ctx, (unsigned long long)(uintptr_t)va_arg(ap, void*),
                       16, 0, width, 1, left);
            break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            if (width && !left) {
                int len = (int)strlen(s);
                int pad = width - len;
                while (pad-- > 0) snprintf_putc(&ctx, ' ');
            }
            write_str(&ctx, s);
            break;
        }
        case 'c':
            snprintf_putc(&ctx, (char)va_arg(ap, int));
            break;
        case '%':
            snprintf_putc(&ctx, '%');
            break;
        default:
            snprintf_putc(&ctx, '%');
            snprintf_putc(&ctx, *fmt);
            break;
        }
    }

    if (size > 0) buf[ctx.pos < size ? ctx.pos : size - 1] = '\0';
    return (int)ctx.pos;
}

int vprintf(const char* restrict fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    for (int i = 0; i < n && buf[i]; i++) putchar(buf[i]);
    return n;
}

int snprintf(char* restrict buf, size_t size, const char* restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char* restrict buf, const char* restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}
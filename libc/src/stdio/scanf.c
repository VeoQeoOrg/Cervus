#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "../../kernel/include/drivers/ps2.h"
#include "../../kernel/include/graphics/fb/fb.h"

extern struct limine_framebuffer *global_framebuffer;
extern uint32_t bg_color;

static int readline_buf(char *buf, size_t size) {
    if (!buf || size == 0) return 0;

    uint32_t prompt_x, prompt_y;
    get_cursor_position(&prompt_x, &prompt_y);

    size_t len = 0;

    while (len < size - 1) {
        int c = getchar();
        if (c == EOF) break;

        if (c == '\n' || c == '\r') {
            putchar('\n');
            break;
        }

        if (c == '\b') {
            if (len == 0) continue;

            len--;

            if (cursor_x >= 8) {
                cursor_x -= 8;
            } else if (cursor_y > prompt_y ||
                       (cursor_y == prompt_y && cursor_x > prompt_x)) {
                cursor_y -= 16;
                cursor_x = (get_screen_width() / 8) * 8 - 8;
            } else {
                continue;
            }

            if (global_framebuffer)
                fb_fill_rect(global_framebuffer,
                             cursor_x, cursor_y, 8, 16, bg_color);
            continue;
        }

        if ((unsigned char)c < 32 && c != '\t') continue;

        buf[len++] = (char)c;
        putchar(c);
    }

    buf[len] = '\0';
    return (int)len;
}

int vsscanf(const char *str, const char *fmt, va_list ap) {
    if (!str || !fmt) return EOF;

    const char *s   = str;
    const char *f   = fmt;
    int n  = 0;

    while (*f) {
        if (*f == ' ' || *f == '\t' || *f == '\n' || *f == '\r') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
                s++;
            f++;
            continue;
        }

        if (*f != '%') {
            if (*s != *f) break;
            s++; f++;
            continue;
        }

        f++;

        bool suppress = false;
        if (*f == '*') { suppress = true; f++; }

        int width = 0;
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f++;
        }

        enum { LEN_INT, LEN_LONG, LEN_LLONG } len = LEN_INT;
        if (*f == 'l') {
            f++;
            if (*f == 'l') { f++; len = LEN_LLONG; }
            else            { len = LEN_LONG; }
        } else if (*f == 'h') {
            f++;
            if (*f == 'h') f++;
        }

        char spec = *f++;

        if (spec == '%') {
            while (*s == ' ' || *s == '\t') s++;
            if (*s != '%') break;
            s++;
            continue;
        }

        if (spec == 'n') {
            if (!suppress) {
                *va_arg(ap, int *) = (int)(s - str);
            }
            continue;
        }

        if (spec == 'c') {
            if (!*s) break;
            if (!suppress) {
                char *out = va_arg(ap, char *);
                *out = *s;
                n++;
            }
            s++;
            continue;
        }

        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
            s++;

        if (!*s) break;

        if (spec == 's') {
            char *out = suppress ? NULL : va_arg(ap, char *);
            int   cnt = 0;
            while (*s && *s != ' ' && *s != '\t' &&
                   *s != '\n' && *s != '\r') {
                if (width && cnt >= width) break;
                if (out) out[cnt] = *s;
                cnt++;
                s++;
            }
            if (cnt == 0) break;
            if (out) { out[cnt] = '\0'; n++; }
            continue;
        }

        int base = 10;
        bool is_signed = false;

        switch (spec) {
            case 'd': base = 10;  is_signed = true;  break;
            case 'i': base = 0;   is_signed = true;  break;
            case 'u': base = 10;  is_signed = false; break;
            case 'o': base = 8;   is_signed = false; break;
            case 'x':
            case 'X': base = 16;  is_signed = false; break;
            default:
                goto done;
        }

        char  tmp[32];
        int   tlen = 0;
        int   maxw = width ? width : (int)sizeof(tmp) - 1;

        if (is_signed && (*s == '-' || *s == '+') && tlen < maxw)
            tmp[tlen++] = *s++;

        if ((base == 16 || base == 0) &&
            *s == '0' && (s[1] == 'x' || s[1] == 'X') && tlen + 2 <= maxw) {
            tmp[tlen++] = *s++; tmp[tlen++] = *s++;
            if (base == 0) base = 16;
        } else if (base == 0 && *s == '0') {
            tmp[tlen++] = *s++;
            base = 8;
        } else if (base == 0) {
            base = 10;
        }

        while (*s && tlen < maxw) {
            char c = *s;
            bool ok = false;
            if (base == 10 && c >= '0' && c <= '9') ok = true;
            if (base ==  8 && c >= '0' && c <= '7') ok = true;
            if (base == 16 && ((c >= '0' && c <= '9') ||
                               (c >= 'a' && c <= 'f') ||
                               (c >= 'A' && c <= 'F'))) ok = true;
            if (!ok) break;
            tmp[tlen++] = c;
            s++;
        }

        if (tlen == 0) break;
        tmp[tlen] = '\0';

        if (!suppress) {
            char *end = NULL;
            if (is_signed) {
                long long val = strtoll(tmp, &end, base);
                if (end == tmp) break;
                switch (len) {
                    case LEN_INT:   *va_arg(ap, int *)       = (int)val;       break;
                    case LEN_LONG:  *va_arg(ap, long *)      = (long)val;      break;
                    case LEN_LLONG: *va_arg(ap, long long *) = val;            break;
                }
            } else {
                unsigned long long val = strtoull(tmp, &end, base);
                if (end == tmp) break;
                switch (len) {
                    case LEN_INT:   *va_arg(ap, unsigned int *)       = (unsigned int)val;       break;
                    case LEN_LONG:  *va_arg(ap, unsigned long *)      = (unsigned long)val;      break;
                    case LEN_LLONG: *va_arg(ap, unsigned long long *) = val;                     break;
                }
            }
            n++;
        }
    }

done:
    return n;
}

int sscanf(const char * restrict str, const char * restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

int vscanf(const char * restrict fmt, va_list ap) {
    char buf[256];
    readline_buf(buf, sizeof(buf));
    return vsscanf(buf, fmt, ap);
}

int scanf(const char * restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vscanf(fmt, ap);
    va_end(ap);
    return r;
}
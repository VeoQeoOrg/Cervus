#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

long long strtoll(const char * restrict s, char ** restrict end, int base) {
    while (*s == ' ' || *s == '\t') s++;

    bool neg = false;
    if (*s == '-') { neg = true;  s++; }
    else if (*s == '+') {         s++; }

    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0')                              { base = 8;  s++;    }
        else                                             { base = 10; }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    unsigned long long acc = 0;
    const char *start = s;

    while (*s) {
        int digit;
        char c = *s;
        if      (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else break;

        if (digit >= base) break;
        acc = acc * (unsigned)base + (unsigned)digit;
        s++;
    }

    if (end) *end = (s == start) ? (char *)start : (char *)s;

    if (neg) return -(long long)acc;
    return (long long)acc;
}
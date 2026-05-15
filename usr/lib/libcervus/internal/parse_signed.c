#include <ctype.h>
#include <libcervus.h>

long long __cervus_parse_signed(const char *s, char **end, int base, int is_unsigned)
{
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    } else if (base == 0 && *s == '0') {
        s++; base = 8;
    } else if (base == 0) {
        base = 10;
    }
    unsigned long long v = 0;
    while (*s) {
        int d;
        if (isdigit((unsigned char)*s)) d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long long)base + (unsigned long long)d;
        s++;
    }
    if (end) *end = (char *)s;
    if (is_unsigned) return (long long)v;
    return neg ? -(long long)v : (long long)v;
}

#include <string.h>
#include <ctype.h>

long strtol(const char* restrict s, char** restrict end, int base) {
    while (isspace((unsigned char)*s)) s++;

    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    if ((base == 0 || base == 16) && s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (base == 0 && s[0] == '0') {
        base = 8;
        s++;
    } else if (base == 0) {
        base = 10;
    }

    long result = 0;
    int any = 0;
    for (; *s; s++) {
        int digit;
        unsigned char c = (unsigned char)*s;
        if (isdigit(c))          digit = c - '0';
        else if (isupper(c))     digit = c - 'A' + 10;
        else if (islower(c))     digit = c - 'a' + 10;
        else break;

        if (digit >= base) break;
        result = result * base + digit;
        any = 1;
    }

    if (end) *end = (char*)(any ? s : (const char*)s);
    return neg ? -result : result;
}
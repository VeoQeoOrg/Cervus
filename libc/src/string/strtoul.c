#include <string.h>
#include <ctype.h>

unsigned long strtoul(const char* restrict s, char** restrict end, int base) {
    while (isspace((unsigned char)*s)) s++;

    if (*s == '+') s++;

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

    unsigned long result = 0;
    int any = 0;
    for (; *s; s++) {
        int digit;
        unsigned char c = (unsigned char)*s;
        if (isdigit(c))          digit = c - '0';
        else if (isupper(c))     digit = c - 'A' + 10;
        else if (islower(c))     digit = c - 'a' + 10;
        else break;

        if (digit >= base) break;
        result = result * (unsigned long)base + (unsigned long)digit;
        any = 1;
    }

    if (end) *end = (char*)(any ? s : (const char*)s);
    return result;
}
#include <string.h>

size_t strnlen(const char* s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}
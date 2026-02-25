#include <string.h>

size_t strcspn(const char* s, const char* reject) {
    size_t n = 0;
    while (s[n]) {
        for (const char* r = reject; *r; r++)
            if (s[n] == *r) return n;
        n++;
    }
    return n;
}
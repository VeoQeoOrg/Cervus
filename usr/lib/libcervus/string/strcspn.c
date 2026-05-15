#include <string.h>

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n]) {
        for (size_t i = 0; reject[i]; i++)
            if (s[n] == reject[i]) return n;
        n++;
    }
    return n;
}

#include <string.h>

size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;
    while (s[n]) {
        int found = 0;
        for (size_t i = 0; accept[i]; i++)
            if (s[n] == accept[i]) { found = 1; break; }
        if (!found) break;
        n++;
    }
    return n;
}

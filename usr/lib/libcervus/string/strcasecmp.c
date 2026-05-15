#include <string.h>
#include <ctype.h>

int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

#include <string.h>

char *strchr(const char *str, int c) {
    char ch = (char)c;
    while (*str) {
        if (*str == ch)
            return (char*)str;

        str++;
    }

    if (ch == '\0')
        return (char*)str;

    return NULL;
}

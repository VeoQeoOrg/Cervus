#include <string.h>

size_t strspn(const char *str1, const char *str2) {
    size_t count = 0;
    while (*str1) {
        const char *p = str2;
        int found = 0;
        while (*p) {
            if (*str1 == *p) {
                found = 1;
                break;
            }
            p++;
        }

        if (!found)
            break;

        count++;
        str1++;
    }

    return count;
}

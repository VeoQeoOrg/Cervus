#include <string.h>

int strncmp(const char *str1, const char *str2, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c1 = (unsigned char)str1[i], 
            c2 = (unsigned char)str2[i];
        if (c1 != c2)
            return c1 - c2;

        if (c1 == '\0') // значит и c2 также уже нуль
            return 0;

        i++;
    }

    return 0;
}

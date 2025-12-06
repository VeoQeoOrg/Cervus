#include <string.h>

char *strpbrk(const char *str1, const char *str2) {
    for (; *str1 != '\0'; str1++) {
        for (const char *p = str2; *p != '\0'; p++) {
            if (*str1 == *p)
                return (char*)str1;
        }
    }
    return NULL;
}

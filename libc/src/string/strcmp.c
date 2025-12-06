#include <string.h>

int strcmp(const char *str1, const char *str2) {
    size_t i = 0;
    while (str1[i] != '\0' && str2[i] != '\0') {
        if (str1[i] != str2[i])
            goto ret;

        i++;
    }
ret:
    return (unsigned char)str1[i] - (unsigned char)str2[i];
}

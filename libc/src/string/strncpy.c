#include <string.h>

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];

    // заполняем все оставщиеся байты в строке
    // на нули
    for (; i < n; i++)
        dest[i] = '\0';

    return dest;
}

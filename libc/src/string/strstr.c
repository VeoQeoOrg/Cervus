#include <string.h>

char *strstr(const char *haystack, const char *needle) {
    if (*needle == '\0')
        return (char*)haystack;

    for (size_t i = 0; haystack[i] != '\0'; i++) {
        size_t j = 0;
        while (needle[j] != '\0' && haystack[j + i] == needle[j])
            j++;

        if (needle[j] == '\0')
            return (char*)&haystack[i];
    }

    return NULL;
}

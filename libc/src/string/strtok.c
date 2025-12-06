#include <string.h>

static char *old_string = NULL;

char *strtok(char *str, const char *delim) {
    if (str == NULL) str = old_string;
    str += strspn(str, delim);
    if (*str == '\0') {
        old_string = str;
        return NULL;
    }

    char *token = str;
    str = strpbrk(token, delim);
    if (str == NULL)
        old_string = rawmemchr(token, '\0');
    else {
        *str = '\0';
        old_string = str + 1;
    }
    return token;
}

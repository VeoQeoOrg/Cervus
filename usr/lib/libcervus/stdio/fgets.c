#include <stdio.h>
#include <stddef.h>

char *fgets(char *str, int n, FILE *s)
{
    if (!str || n <= 0 || !s) return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(s);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        str[i++] = (char)c;
        if (c == '\n') break;
    }
    str[i] = '\0';
    return str;
}

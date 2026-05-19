#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/types.h>
#include <errno.h>
#include <libcervus.h>

ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream) {
    if (!lineptr || !n || !stream) {
        __cervus_errno = EINVAL;
        return -1;
    }
    if (*lineptr == NULL || *n == 0) {
        size_t cap = 128;
        char *buf = (char *)malloc(cap);
        if (!buf) { __cervus_errno = ENOMEM; return -1; }
        *lineptr = buf;
        *n = cap;
    }
    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t nc = *n * 2;
            char *nb = (char *)realloc(*lineptr, nc);
            if (!nb) { __cervus_errno = ENOMEM; return -1; }
            *lineptr = nb;
            *n = nc;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == delim) break;
    }
    if (pos == 0 && c == EOF) return -1;
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    return getdelim(lineptr, n, '\n', stream);
}

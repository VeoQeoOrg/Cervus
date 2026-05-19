#include <libgen.h>
#include <string.h>
#include <stddef.h>

char *dirname(char *path) {
    static char dot[] = ".";
    static char slash[] = "/";
    if (!path || !*path) return dot;
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
    char *p = strrchr(path, '/');
    if (!p) return dot;
    if (p == path) return slash;
    *p = '\0';
    while (p > path + 1 && *(p - 1) == '/') { p--; *p = '\0'; }
    return path;
}

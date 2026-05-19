#include <libgen.h>
#include <string.h>
#include <stddef.h>

char *basename(char *path) {
    static char dot[] = ".";
    static char slash[] = "/";
    if (!path || !*path) return dot;
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
    if (len == 1 && path[0] == '/') return slash;
    char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

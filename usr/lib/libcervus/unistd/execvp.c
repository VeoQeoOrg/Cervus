#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

int execvp(const char *file, char *const argv[])
{
    if (!file || !*file) { __cervus_errno = ENOENT; return -1; }

    int has_slash = 0;
    for (const char *p = file; *p; p++) if (*p == '/') { has_slash = 1; break; }
    if (has_slash) return execve(file, argv, NULL);

    const char *path = getenv("PATH");
    if (!path || !*path) path = "/bin:/apps";

    char buf[512];
    const char *p = path;
    while (*p) {
        const char *colon = p;
        while (*colon && *colon != ':') colon++;
        size_t dlen = (size_t)(colon - p);
        size_t flen = strlen(file);
        if (dlen + 1 + flen + 1 <= sizeof(buf)) {
            memcpy(buf, p, dlen);
            buf[dlen] = '/';
            memcpy(buf + dlen + 1, file, flen);
            buf[dlen + 1 + flen] = '\0';
            execve(buf, argv, NULL);
        }
        p = colon;
        if (*p == ':') p++;
    }
    __cervus_errno = ENOENT;
    return -1;
}

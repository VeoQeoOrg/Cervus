#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

char *realpath(const char *path, char *resolved)
{
    if (!path) { __cervus_errno = EINVAL; return NULL; }
    static char sbuf[512];
    char *out = resolved ? resolved : sbuf;

    if (path[0] == '/') {
        strncpy(out, path, 511);
        out[511] = '\0';
    } else {
        char cwd[512];
        if (!getcwd(cwd, sizeof(cwd))) { cwd[0] = '/'; cwd[1] = '\0'; }
        strncpy(out, cwd, 511);
        out[511] = '\0';
        size_t bl = strlen(out);
        if (bl > 0 && out[bl - 1] != '/' && bl < 510) {
            out[bl++] = '/';
            out[bl] = '\0';
        }
        strncat(out, path, 511 - strlen(out));
    }

    char tmp[512];
    strncpy(tmp, out, 511);
    tmp[511] = '\0';
    char *parts[64];
    int np = 0;
    char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *s = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = '\0';
        if (strcmp(s, ".") == 0) continue;
        if (strcmp(s, "..") == 0) { if (np > 0) np--; continue; }
        if (np < 64) parts[np++] = s;
    }
    size_t ol = 0;
    for (int i = 0; i < np; i++) {
        out[ol++] = '/';
        size_t pl = strlen(parts[i]);
        if (ol + pl >= 511) break;
        memcpy(out + ol, parts[i], pl);
        ol += pl;
    }
    out[ol] = '\0';
    if (ol == 0) { out[0] = '/'; out[1] = '\0'; }
    return out;
}

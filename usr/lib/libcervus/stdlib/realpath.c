#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <libcervus.h>

#define REALPATH_MAX_LINKS 40

static int append_name(char *out, size_t *ol, const char *name, size_t n)
{
    if (*ol + 1 + n >= PATH_MAX) return -1;
    out[(*ol)++] = '/';
    memcpy(out + *ol, name, n);
    *ol += n;
    out[*ol] = '\0';
    return 0;
}

char *realpath(const char *path, char *resolved)
{
    char out[PATH_MAX], rest[PATH_MAX], target[PATH_MAX], joined[PATH_MAX];
    size_t ol = 0;
    int links = 0;

    if (!path) { __cervus_set_errno(EINVAL); return NULL; }
    if (!*path) { __cervus_set_errno(ENOENT); return NULL; }
    size_t pl = strlen(path);
    if (pl >= PATH_MAX) { __cervus_set_errno(ENAMETOOLONG); return NULL; }
    memcpy(rest, path, pl + 1);

    out[0] = '\0';
    if (rest[0] != '/') {
        if (!getcwd(out, sizeof out)) return NULL;
        ol = strlen(out);
        while (ol > 0 && out[ol - 1] == '/') ol--;
        out[ol] = '\0';
    }

    char *p = rest;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *s = p;
        while (*p && *p != '/') p++;
        size_t n = (size_t)(p - s);

        if (n == 1 && s[0] == '.') continue;
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            while (ol > 0 && out[ol - 1] != '/') ol--;
            if (ol > 0) ol--;
            out[ol] = '\0';
            continue;
        }

        size_t parent = ol;
        if (append_name(out, &ol, s, n) < 0) { __cervus_set_errno(ENAMETOOLONG); return NULL; }

        struct stat st;
        if (lstat(out, &st) < 0) return NULL;

        if (S_ISLNK(st.st_mode)) {
            if (++links > REALPATH_MAX_LINKS) { __cervus_set_errno(ELOOP); return NULL; }
            ssize_t tl = readlink(out, target, sizeof target - 1);
            if (tl < 0) return NULL;
            target[tl] = '\0';
            size_t rl = strlen(p);
            if ((size_t)tl + rl + 1 >= PATH_MAX) { __cervus_set_errno(ENAMETOOLONG); return NULL; }
            memcpy(joined, target, (size_t)tl);
            memcpy(joined + tl, p, rl + 1);
            memcpy(rest, joined, (size_t)tl + rl + 1);
            p = rest;
            ol = target[0] == '/' ? 0 : parent;
            out[ol] = '\0';
            continue;
        }

        if (*p && !S_ISDIR(st.st_mode)) { __cervus_set_errno(ENOTDIR); return NULL; }
    }

    if (ol == 0) { out[0] = '/'; out[1] = '\0'; ol = 1; }
    if (!resolved) {
        resolved = malloc(ol + 1);
        if (!resolved) { __cervus_set_errno(ENOMEM); return NULL; }
    }
    memcpy(resolved, out, ol + 1);
    return resolved;
}

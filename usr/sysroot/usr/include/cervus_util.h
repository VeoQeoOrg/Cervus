#ifndef _CERVUS_UTIL_H
#define _CERVUS_UTIL_H

#include <stddef.h>
#include <string.h>
#include <unistd.h>

#define C_RESET  "\x1b[0m"
#define C_BOLD   "\x1b[1m"
#define C_RED    "\x1b[1;31m"
#define C_GREEN  "\x1b[1;32m"
#define C_YELLOW "\x1b[1;33m"
#define C_BLUE   "\x1b[1;34m"
#define C_MAGENTA "\x1b[1;35m"
#define C_CYAN   "\x1b[1;36m"
#define C_GRAY   "\x1b[90m"

static inline void path_join(const char *base, const char *name,
                             char *out, size_t sz)
{
    if (!name || !name[0]) {
        strncpy(out, base ? base : "", sz - 1);
        out[sz - 1] = '\0';
        return;
    }
    if (name[0] == '/') {
        strncpy(out, name, sz - 1);
        out[sz - 1] = '\0';
        return;
    }
    strncpy(out, base ? base : "", sz - 1);
    out[sz - 1] = '\0';
    size_t bl = strlen(out);
    if (bl > 0 && out[bl - 1] != '/' && bl + 1 < sz) {
        out[bl++] = '/';
        out[bl] = '\0';
    }
    size_t nl = strlen(name);
    if (bl + nl + 1 < sz) memcpy(out + bl, name, nl + 1);
    else out[sz - 1] = '\0';
}

static inline void path_norm(char *path)
{
    char tmp[512];
    strncpy(tmp, path, 511);
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
    char out[512];
    size_t ol = 0;
    for (int i = 0; i < np; i++) {
        if (ol + 1 >= sizeof(out)) break;
        out[ol++] = '/';
        size_t pl = strlen(parts[i]);
        if (ol + pl >= sizeof(out)) pl = sizeof(out) - 1 - ol;
        memcpy(out + ol, parts[i], pl);
        ol += pl;
    }
    out[ol] = '\0';
    if (ol == 0) { out[0] = '/'; out[1] = '\0'; }
    strncpy(path, out, 512);
}

static inline void resolve_path(const char *cwd, const char *path,
                                char *out, size_t sz)
{
    if (!path || !path[0]) {
        strncpy(out, cwd ? cwd : "/", sz - 1);
        out[sz - 1] = '\0';
        return;
    }
    if (path[0] == '/') {
        strncpy(out, path, sz - 1);
        out[sz - 1] = '\0';
        return;
    }
    path_join(cwd, path, out, sz);
    path_norm(out);
}

extern int    __cervus_argc;
extern char **__cervus_argv;

static inline int is_shell_flag(const char *a)
{
    if (!a) return 0;
    if (a[0] == '-' && a[1] == '-' &&
        a[2] == 'c' && a[3] == 'w' && a[4] == 'd' && a[5] == '=') return 1;
    if (a[0] == '-' && a[1] == '-' &&
        a[2] == 'e' && a[3] == 'n' && a[4] == 'v' && a[5] == ':') return 1;
    return 0;
}

static inline const char *get_cwd_flag(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int i = 1; i < __cervus_argc; i++) {
        char *a = __cervus_argv[i];
        if (a && a[0] == '-' && a[1] == '-' &&
            a[2] == 'c' && a[3] == 'w' && a[4] == 'd' && a[5] == '=')
            return a + 6;
    }
    return "/";
}

static inline const char *getenv_argv(int argc, char **argv,
                                      const char *name, const char *def)
{
    (void)argc; (void)argv;
    if (!name) return def;
    size_t nl = strlen(name);
    for (int i = 1; i < __cervus_argc; i++) {
        const char *a = __cervus_argv[i];
        if (!a) continue;
        if (a[0] == '-' && a[1] == '-' &&
            a[2] == 'e' && a[3] == 'n' && a[4] == 'v' && a[5] == ':') {
            const char *kv = a + 6;
            if (strncmp(kv, name, nl) == 0 && kv[nl] == '=')
                return kv + nl + 1;
        }
    }
    return def;
}

static inline int util_readline(int fd, char *buf, int maxlen)
{
    int i = 0;
    while (i < maxlen - 1) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) { buf[i] = '\0'; return i > 0 ? i : -1; }
        if (c == '\r') continue;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return i;
}

static inline void util_write_stdout(const char *s)
{
    write(1, s, strlen(s));
}
static inline void util_write_stderr(const char *s)
{
    write(2, s, strlen(s));
}

#endif

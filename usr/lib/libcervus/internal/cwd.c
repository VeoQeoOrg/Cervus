#include <string.h>
#include <cervus_util.h>
#include <libcervus.h>

char __cervus_cwd[CERVUS_PATH_MAX];
int  __cervus_cwd_inited = 0;

const char *__cervus_get_cwd(void)
{
    if (!__cervus_cwd_inited) {
        const char *c = get_cwd_flag(__cervus_argc, __cervus_argv);
        if (!c || !*c) c = "/";
        size_t n = strlen(c);
        if (n >= sizeof(__cervus_cwd)) n = sizeof(__cervus_cwd) - 1;
        memcpy(__cervus_cwd, c, n);
        __cervus_cwd[n] = '\0';
        __cervus_cwd_inited = 1;
    }
    return __cervus_cwd;
}

const char *__cervus_resolve(const char *path, char *buf, size_t bufsz)
{
    if (!path) return path;
    if (path[0] == '/') return path;
    resolve_path(__cervus_get_cwd(), path, buf, bufsz);
    return buf;
}

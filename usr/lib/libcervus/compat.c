#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/cervus.h>

extern int __cervus_errno;

static long __compat_sys_ret(long r)
{
    if (r < 0 && r > -4096) {
        __cervus_errno = (int)-r;
        return -1;
    }
    return r;
}

int access(const char *path, int mode)
{
    struct stat st;
    if (!path) { __cervus_errno = EFAULT; return -1; }
    long r = syscall2(SYS_STAT, path, &st);
    (void)mode;
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return -1; }
    return 0;
}

static char __cervus_cwd[512] = "/";

int fchdir(int fd)
{
    (void)fd;
    __cervus_errno = ENOSYS;
    return -1;
}

int symlink(const char *target, const char *linkpath)
{
    (void)target; (void)linkpath;
    __cervus_errno = ENOSYS;
    return -1;
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)path; (void)buf; (void)bufsiz;
    __cervus_errno = ENOSYS;
    return -1;
}

int sched_yield(void)
{
    syscall0(SYS_YIELD);
    return 0;
}

long pathconf(const char *path, int name)
{
    (void)path;
    switch (name) {
        case 0:  return 255;
        case 1:  return 512;
        default: __cervus_errno = EINVAL; return -1;
    }
}

long fpathconf(int fd, int name)
{
    (void)fd;
    switch (name) {
        case 0:  return 255;
        case 1:  return 512;
        default: __cervus_errno = EINVAL; return -1;
    }
}

int mprotect(void *addr, size_t len, int prot)
{
    return (int)__compat_sys_ret(syscall3(SYS_MPROTECT, addr, len, prot));
}

char *realpath(const char *path, char *resolved)
{
    if (!path) { __cervus_errno = EINVAL; return NULL; }
    static char sbuf[512];
    char *out = resolved ? resolved : sbuf;

    if (path[0] == '/') {
        strncpy(out, path, 511);
        out[511] = '\0';
    } else {
        strncpy(out, __cervus_cwd, 511);
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

char *mkdtemp(char *tmpl)
{
    if (!tmpl) { __cervus_errno = EINVAL; return NULL; }
    size_t len = strlen(tmpl);
    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX") != 0) {
        __cervus_errno = EINVAL;
        return NULL;
    }
    static uint64_t seq = 0;
    uint64_t pid = (uint64_t)getpid();
    const char *alpha = "0123456789abcdefghijklmnopqrstuvwxyz";
    for (int attempt = 0; attempt < 100; attempt++) {
        uint64_t seed = (cervus_uptime_ns() ^ (pid << 32)) + (seq++);
        for (int i = 0; i < 6; i++) {
            tmpl[len - 6 + i] = alpha[seed % 36];
            seed /= 36;
        }
        if (mkdir(tmpl, 0700) == 0) return tmpl;
        if (__cervus_errno != EEXIST) return NULL;
    }
    __cervus_errno = EEXIST;
    return NULL;
}

static char **__env_table = NULL;
static int    __env_count = 0;
static int    __env_cap   = 0;

int putenv(char *str)
{
    if (!str) return -1;
    char *eq = strchr(str, '=');
    if (!eq) return -1;
    size_t nl = (size_t)(eq - str);
    for (int i = 0; i < __env_count; i++) {
        if (strncmp(__env_table[i], str, nl) == 0 && __env_table[i][nl] == '=') {
            __env_table[i] = str;
            return 0;
        }
    }
    if (__env_count >= __env_cap) {
        int nc = __env_cap ? __env_cap * 2 : 16;
        char **nt = (char **)realloc(__env_table, (size_t)nc * sizeof(char *));
        if (!nt) return -1;
        __env_table = nt;
        __env_cap = nc;
    }
    __env_table[__env_count++] = str;
    return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !value) { __cervus_errno = EINVAL; return -1; }
    size_t nl = strlen(name);
    size_t vl = strlen(value);
    for (int i = 0; i < __env_count; i++) {
        if (strncmp(__env_table[i], name, nl) == 0 && __env_table[i][nl] == '=') {
            if (!overwrite) return 0;
            char *nv = (char *)malloc(nl + vl + 2);
            if (!nv) return -1;
            memcpy(nv, name, nl);
            nv[nl] = '=';
            memcpy(nv + nl + 1, value, vl + 1);
            __env_table[i] = nv;
            return 0;
        }
    }
    char *nv = (char *)malloc(nl + vl + 2);
    if (!nv) return -1;
    memcpy(nv, name, nl);
    nv[nl] = '=';
    memcpy(nv + nl + 1, value, vl + 1);
    return putenv(nv);
}

int unsetenv(const char *name)
{
    if (!name) { __cervus_errno = EINVAL; return -1; }
    size_t nl = strlen(name);
    for (int i = 0; i < __env_count; i++) {
        if (strncmp(__env_table[i], name, nl) == 0 && __env_table[i][nl] == '=') {
            __env_table[i] = __env_table[--__env_count];
            return 0;
        }
    }
    return 0;
}

int uname(struct utsname *buf)
{
    if (!buf) { __cervus_errno = EFAULT; return -1; }
    strncpy(buf->sysname,  "Cervus", _UTSNAME_LENGTH - 1);
    strncpy(buf->nodename, "cervus", _UTSNAME_LENGTH - 1);
    strncpy(buf->release,  "0.0.2",  _UTSNAME_LENGTH - 1);
    strncpy(buf->version,  "#1",     _UTSNAME_LENGTH - 1);
    strncpy(buf->machine,  "x86_64", _UTSNAME_LENGTH - 1);
    buf->sysname[_UTSNAME_LENGTH - 1]  = '\0';
    buf->nodename[_UTSNAME_LENGTH - 1] = '\0';
    buf->release[_UTSNAME_LENGTH - 1]  = '\0';
    buf->version[_UTSNAME_LENGTH - 1]  = '\0';
    buf->machine[_UTSNAME_LENGTH - 1]  = '\0';
    return 0;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *))
{
    const unsigned char *lo = (const unsigned char *)base;
    const unsigned char *hi = lo + nmemb * size;
    while (lo < hi) {
        size_t half = (size_t)((hi - lo) / (ptrdiff_t)size) / 2;
        const unsigned char *mid = lo + half * size;
        int r = cmp(key, mid);
        if (r == 0) return (void *)mid;
        if (r < 0) hi = mid;
        else lo = mid + size;
    }
    return NULL;
}

struct __cervus_FILE {
    int    fd;
    int    eof;
    int    err;
    int    flags;
    char  *buf;
    size_t buf_size;
    size_t buf_pos;
};

FILE *fdopen(int fd, const char *mode)
{
    (void)mode;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { __cervus_errno = ENOMEM; return NULL; }
    f->fd       = fd;
    f->eof      = 0;
    f->err      = 0;
    f->flags    = 0;
    f->buf      = NULL;
    f->buf_size = 0;
    f->buf_pos  = 0;
    return f;
}

FILE *popen(const char *cmd, const char *type)
{
    if (!cmd || !type) { __cervus_errno = EINVAL; return NULL; }
    int fds[2];
    if (pipe(fds) < 0) return NULL;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }
    if (pid == 0) {
        if (type[0] == 'r') { dup2(fds[1], 1); }
        else                { dup2(fds[0], 0); }
        close(fds[0]);
        close(fds[1]);
        char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
        execve("/bin/sh", argv, NULL);
        _exit(127);
    }
    if (type[0] == 'r') {
        close(fds[1]);
        return fdopen(fds[0], "r");
    } else {
        close(fds[0]);
        return fdopen(fds[1], "w");
    }
}

int pclose(FILE *f)
{
    if (!f) return -1;
    int fd = f->fd;
    free(f);
    close(fd);
    int status = 0;
    waitpid(-1, &status, 0);
    return status;
}

int ungetc(int c, FILE *f)
{
    (void)c; (void)f;
    return EOF;
}

void rewind(FILE *f)
{
    if (f) {
        syscall3(SYS_SEEK, f->fd, 0, 0);
        f->eof = 0;
        f->err = 0;
    }
}

char *tmpnam(char *buf)
{
    static char sbuf[32];
    static uint64_t seq = 0;
    char *out = buf ? buf : sbuf;
    uint64_t seed = cervus_uptime_ns() ^ (seq++);
    snprintf(out, 32, "/tmp/tmp%llu", (unsigned long long)seed);
    return out;
}
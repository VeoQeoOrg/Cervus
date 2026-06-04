#ifndef _CERVUS_UTIL_H
#define _CERVUS_UTIL_H

#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

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

#define CERVUS_VERSION_STR "Cervus 0.0.2"

static inline void __cervus_help_write(const char *s)
{
    if (!s) return;
    size_t n = 0;
    while (s[n]) n++;
    if (n) write(1, s, n);
}

static inline int cervus_check_help_version(int argc, char **argv,
                                            const char *usage,
                                            const char *prog)
{
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-?") == 0) {
            __cervus_help_write(usage);
            return 1;
        }
        if (strcmp(argv[i], "--version") == 0) {
            if (prog) {
                __cervus_help_write(prog);
                __cervus_help_write(" (");
                __cervus_help_write(CERVUS_VERSION_STR);
                __cervus_help_write(")\n");
            } else {
                __cervus_help_write(CERVUS_VERSION_STR);
                __cervus_help_write("\n");
            }
            return 1;
        }
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] == '\0') break;
    }
    return 0;
}

static inline int cervus_confirm(const char *what,
                                 const char *target,
                                 const char *reason)
{
    __cervus_help_write("\x1b[1;31m  WARNING:\x1b[0m ");
    if (what)   { __cervus_help_write(what); __cervus_help_write(" "); }
    if (target) { __cervus_help_write("'"); __cervus_help_write(target); __cervus_help_write("'"); }
    __cervus_help_write("\n");
    if (reason) {
        __cervus_help_write("  \x1b[90m");
        __cervus_help_write(reason);
        __cervus_help_write("\x1b[0m\n");
    }
    __cervus_help_write("  Continue? [y/N] ");
    char buf[8];
    ssize_t n = read(0, buf, sizeof(buf));
    if (n <= 0) { __cervus_help_write("\n"); return 0; }
    int ok = (buf[0] == 'y' || buf[0] == 'Y');
    int saw_nl = 0;
    for (ssize_t i = 0; i < n; i++) if (buf[i] == '\n') { saw_nl = 1; break; }
    if (!saw_nl) __cervus_help_write("\n");
    return ok;
}

static inline const char *cervus_path_danger(const char *path)
{
    if (!path) return (const char *)0;
    static const struct { const char *p; const char *r; } LIST[] = {
        {"/",      "this is the root of the filesystem — every file would be lost"},
        {"/bin",   "removing /bin destroys every system command"},
        {"/apps",  "removing /apps destroys every installed application"},
        {"/etc",   "removing /etc loses all system configuration"},
        {"/home",  "removing /home loses user files and history"},
        {"/usr",   "removing /usr destroys the sysroot (headers, libs, tcc)"},
        {"/dev",   "/dev is the virtual device filesystem — I/O will break"},
        {"/mnt",   "/mnt holds mounted disk partitions"},
        {"/boot",  "removing /boot makes the system unbootable"},
        {"/tmp",   "/tmp may hold open files for running programs"},
        {"/var",   "/var holds logs and persistent runtime state"},
        {(const char *)0, (const char *)0}
    };
    const char *p = path;
    while (*p == '/' && p[1] == '/') p++;
    size_t pl = strlen(p);
    while (pl > 1 && p[pl - 1] == '/') pl--;
    for (int i = 0; LIST[i].p; i++) {
        size_t lp = strlen(LIST[i].p);
        if (pl == lp && strncmp(p, LIST[i].p, lp) == 0) return LIST[i].r;
    }
    return (const char *)0;
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

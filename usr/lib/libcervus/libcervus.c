#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/cervus.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>
#include <cervus_util.h>

int    __cervus_argc;
char **__cervus_argv;

int __cervus_errno = 0;

static long __sys_ret(long r)
{
    if (r < 0 && r > -4096) {
        __cervus_errno = (int)-r;
        return -1;
    }
    return r;
}

#define CERVUS_PATH_MAX 512

static char __cervus_cwd[CERVUS_PATH_MAX];
static int  __cervus_cwd_inited = 0;

static const char *__cervus_get_cwd(void)
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

static const char *__cervus_resolve(const char *path, char *buf, size_t bufsz)
{
    if (!path) return path;
    if (path[0] == '/') return path;
    resolve_path(__cervus_get_cwd(), path, buf, bufsz);
    return buf;
}

ssize_t read(int fd, void *buf, size_t n)
{
    return (ssize_t)__sys_ret(syscall3(SYS_READ, fd, buf, n));
}
ssize_t write(int fd, const void *buf, size_t n)
{
    return (ssize_t)__sys_ret(syscall3(SYS_WRITE, fd, buf, n));
}
int close(int fd)
{
    return (int)__sys_ret(syscall1(SYS_CLOSE, fd));
}
off_t lseek(int fd, off_t off, int whence)
{
    return (off_t)__sys_ret(syscall3(SYS_SEEK, fd, (uint64_t)off, whence));
}
int dup(int fd)
{
    return (int)__sys_ret(syscall1(SYS_DUP, fd));
}
int dup2(int oldfd, int newfd)
{
    return (int)__sys_ret(syscall2(SYS_DUP2, oldfd, newfd));
}
int pipe(int fds[2])
{
    return (int)__sys_ret(syscall1(SYS_PIPE, fds));
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    return (int)__sys_ret(syscall3(SYS_IOCTL, fd, request, arg));
}

int isatty(int fd)
{
    struct winsize ws;
    long r = syscall3(SYS_IOCTL, fd, TIOCGWINSZ, &ws);
    if (r < 0) {
        __cervus_errno = (int)-r;
        return 0;
    }
    return 1;
}

int unlink(const char *path)
{
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__sys_ret(syscall1(SYS_UNLINK, path));
}
int rmdir(const char *path)
{
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__sys_ret(syscall1(SYS_RMDIR, path));
}

pid_t getpid(void)  { return (pid_t)syscall0(SYS_GETPID); }
pid_t getppid(void) { return (pid_t)syscall0(SYS_GETPPID); }
uid_t getuid(void)  { return (uid_t)syscall0(SYS_GETUID); }
gid_t getgid(void)  { return (gid_t)syscall0(SYS_GETGID); }
int   setuid(uid_t u) { return (int)__sys_ret(syscall1(SYS_SETUID, u)); }
int   setgid(gid_t g) { return (int)__sys_ret(syscall1(SYS_SETGID, g)); }
pid_t fork(void)    { return (pid_t)__sys_ret(syscall0(SYS_FORK)); }

int execve(const char *path, char *const argv[], char *const envp[])
{
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__sys_ret(syscall3(SYS_EXECVE, path, argv, envp));
}

int execv(const char *path, char *const argv[])
{
    char *empty[] = { NULL };
    return execve(path, argv, empty);
}

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

void _exit(int status) { syscall1(SYS_EXIT, status); __builtin_unreachable(); }

unsigned int sleep(unsigned int sec)
{
    cervus_nanosleep((uint64_t)sec * 1000000000ULL);
    return 0;
}
int usleep(unsigned int usec)
{
    return cervus_nanosleep((uint64_t)usec * 1000ULL);
}

void sched_yield_cervus(void) { syscall0(SYS_YIELD); }

char *getcwd(char *buf, size_t size)
{
    if (!buf || size < 2) { __cervus_errno = EINVAL; return NULL; }
    const char *c = __cervus_get_cwd();
    size_t n = strlen(c);
    if (n + 1 > size) { __cervus_errno = ERANGE; return NULL; }
    memcpy(buf, c, n + 1);
    return buf;
}

int chdir(const char *path)
{
    if (!path || !*path) { __cervus_errno = ENOENT; return -1; }

    char abs[CERVUS_PATH_MAX];
    const char *p = __cervus_resolve(path, abs, sizeof(abs));

    struct stat st;
    if ((int)__sys_ret(syscall2(SYS_STAT, p, &st)) < 0) return -1;
    if (!S_ISDIR(st.st_mode)) { __cervus_errno = ENOTDIR; return -1; }

    size_t n = strlen(p);
    if (n >= sizeof(__cervus_cwd)) { __cervus_errno = ENAMETOOLONG; return -1; }
    memcpy(__cervus_cwd, p, n + 1);
    __cervus_cwd_inited = 1;
    return 0;
}

void *sbrk(intptr_t incr)
{
    uintptr_t cur = (uintptr_t)syscall1(SYS_BRK, 0);
    if (incr == 0) return (void *)cur;
    uintptr_t nw  = (uintptr_t)syscall1(SYS_BRK, cur + (uintptr_t)incr);
    if (nw != cur + (uintptr_t)incr) {
        __cervus_errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)cur;
}
int brk(void *addr)
{
    uintptr_t r = (uintptr_t)syscall1(SYS_BRK, (uintptr_t)addr);
    if (r != (uintptr_t)addr) { __cervus_errno = ENOMEM; return -1; }
    return 0;
}

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__sys_ret(syscall3(SYS_OPEN, path, flags, mode));
}
int creat(const char *path, mode_t mode)
{
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}
int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);
    return (int)__sys_ret(syscall3(SYS_FCNTL, fd, cmd, arg));
}

int stat(const char *path, struct stat *out)
{
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__sys_ret(syscall2(SYS_STAT, path, out));
}
int fstat(int fd, struct stat *out)
{
    return (int)__sys_ret(syscall2(SYS_FSTAT, fd, out));
}
int mkdir(const char *path, mode_t mode)
{
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__sys_ret(syscall2(SYS_MKDIR, path, mode));
}
int chmod(const char *path, mode_t mode)
{
    (void)path; (void)mode;
    return 0;
}
int rename(const char *oldp, const char *newp)
{
    char absa[CERVUS_PATH_MAX], absb[CERVUS_PATH_MAX];
    oldp = __cervus_resolve(oldp, absa, sizeof(absa));
    newp = __cervus_resolve(newp, absb, sizeof(absb));
    return (int)__sys_ret(syscall2(SYS_RENAME, oldp, newp));
}

void *mmap(void *a, size_t l, int p, int f, int fd, off_t o)
{
    long r = syscall6(SYS_MMAP, a, l, p, f, fd, (uint64_t)o);
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return MAP_FAILED; }
    return (void *)r;
}
int munmap(void *a, size_t l) { return (int)__sys_ret(syscall2(SYS_MUNMAP, a, l)); }

pid_t waitpid(pid_t p, int *s, int f) { return (pid_t)__sys_ret(syscall3(SYS_WAIT, p, s, f)); }
pid_t wait(int *s) { return waitpid(-1, s, 0); }

ssize_t cervus_dbg_print(const char *b, size_t n) { return (ssize_t)syscall2(SYS_DBG_PRINT, b, n); }

char *optarg = NULL;
int   optind = 1;
int   optopt = 0;
int   opterr = 1;

static int __opt_subidx = 1;

int getopt(int argc, char *const argv[], const char *optstring)
{
    if (!optstring) optstring = "";
    int colon_mode = (optstring[0] == ':');
    const char *opts = colon_mode ? optstring + 1 : optstring;

    optarg = NULL;

    if (optind >= argc) return -1;

    char *cur = argv[optind];
    if (!cur || cur[0] != '-' || cur[1] == '\0') return -1;
    if (cur[0] == '-' && cur[1] == '-' && cur[2] == '\0') {
        optind++;
        return -1;
    }

    char ch = cur[__opt_subidx];
    if (ch == '\0') {
        optind++;
        __opt_subidx = 1;
        return getopt(argc, argv, optstring);
    }

    const char *pp = opts;
    while (*pp && *pp != ch) pp++;
    if (*pp == '\0' || ch == ':') {
        optopt = ch;
        if (opterr && !colon_mode) {
            const char *prog = argv[0] ? argv[0] : "?";
            fprintf(stderr, "%s: invalid option -- '%c'\n", prog, ch);
        }
        __opt_subidx++;
        if (cur[__opt_subidx] == '\0') {
            optind++;
            __opt_subidx = 1;
        }
        return '?';
    }

    if (pp[1] == ':') {
        if (cur[__opt_subidx + 1] != '\0') {
            optarg = &cur[__opt_subidx + 1];
            optind++;
            __opt_subidx = 1;
            return ch;
        }
        if (optind + 1 >= argc) {
            optopt = ch;
            optind++;
            __opt_subidx = 1;
            if (opterr && !colon_mode) {
                const char *prog = argv[0] ? argv[0] : "?";
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", prog, ch);
            }
            return colon_mode ? ':' : '?';
        }
        optarg = argv[optind + 1];
        optind += 2;
        __opt_subidx = 1;
        return ch;
    }

    __opt_subidx++;
    if (cur[__opt_subidx] == '\0') {
        optind++;
        __opt_subidx = 1;
    }
    return ch;
}

int tcgetattr(int fd, struct termios *t)
{
    if (!t) { __cervus_errno = EINVAL; return -1; }
    return (int)__sys_ret(syscall3(SYS_IOCTL, fd, TCGETS, t));
}

int tcsetattr(int fd, int optional_actions, const struct termios *t)
{
    if (!t) { __cervus_errno = EINVAL; return -1; }
    unsigned long req;
    switch (optional_actions) {
        case TCSADRAIN: req = TCSETSW; break;
        case TCSAFLUSH: req = TCSETSF; break;
        case TCSANOW:
        default:        req = TCSETS;  break;
    }
    return (int)__sys_ret(syscall3(SYS_IOCTL, fd, req, t));
}

int      cervus_task_info(pid_t p, cervus_task_info_t *o)  { return (int)__sys_ret(syscall2(SYS_TASK_INFO, p, o)); }
int      cervus_task_kill(pid_t p)                         { return (int)__sys_ret(syscall1(SYS_TASK_KILL, p)); }
uint64_t cervus_cap_get(void)                              { return (uint64_t)syscall0(SYS_CAP_GET); }
int      cervus_cap_drop(uint64_t m)                       { return (int)__sys_ret(syscall1(SYS_CAP_DROP, m)); }

int      cervus_meminfo(cervus_meminfo_t *m)               { return (int)__sys_ret(syscall1(SYS_MEMINFO, m)); }
uint64_t cervus_uptime_ns(void)                            { return (uint64_t)syscall0(SYS_UPTIME); }
int      cervus_clock_gettime(int id, cervus_timespec_t *t){ return (int)__sys_ret(syscall2(SYS_CLOCK_GET, id, t)); }
int      cervus_nanosleep(uint64_t ns)                     { return (int)__sys_ret(syscall1(SYS_SLEEP_NS, ns)); }

int      cervus_shutdown(void) { return (int)__sys_ret(syscall0(SYS_SHUTDOWN)); }
int      cervus_reboot(void)   { return (int)__sys_ret(syscall0(SYS_REBOOT)); }

int      cervus_disk_info(int i, cervus_disk_info_t *o)                               { return (int)__sys_ret(syscall2(SYS_DISK_INFO, (uint64_t)i, o)); }
int      cervus_disk_mount(const char *d, const char *p)                              { return (int)__sys_ret(syscall2(SYS_DISK_MOUNT, d, p)); }
int      cervus_disk_umount(const char *p)                                            { return (int)__sys_ret(syscall1(SYS_DISK_UMOUNT, p)); }
int      cervus_disk_format(const char *d, const char *l)                             { return (int)__sys_ret(syscall2(SYS_DISK_FORMAT, d, l)); }
int      cervus_disk_mkfs_fat32(const char *d, const char *l)                         { return (int)__sys_ret(syscall2(SYS_DISK_MKFS_FAT32, d, l)); }
int      cervus_disk_partition(const char *d, const cervus_mbr_part_t *s, uint64_t n) { return (int)__sys_ret(syscall3(SYS_DISK_PARTITION, d, s, n)); }
int      cervus_disk_read_raw(const char *d, uint64_t lba, uint64_t c, void *b)       { return (int)__sys_ret(syscall4(SYS_DISK_READ_RAW, d, lba, c, b)); }
int      cervus_disk_write_raw(const char *d, uint64_t lba, uint64_t c, const void *b){ return (int)__sys_ret(syscall4(SYS_DISK_WRITE_RAW, d, lba, c, b)); }
long     cervus_disk_list_parts(cervus_part_info_t *o, int m)                         { return __sys_ret(syscall2(SYS_DISK_LIST_PARTS, o, m)); }
long     cervus_disk_bios_install(const char *d, const void *sd, uint32_t ss)         { return __sys_ret(syscall3(SYS_DISK_BIOS_INSTALL, d, sd, ss)); }

long     cervus_list_mounts(cervus_mount_info_t *o, int m)  { return __sys_ret(syscall2(SYS_LIST_MOUNTS, o, m)); }
long     cervus_statvfs(const char *p, cervus_statvfs_t *o) { return __sys_ret(syscall2(SYS_STATVFS, p, o)); }

uint32_t cervus_ioport_read(uint16_t p, int w)              { return (uint32_t)syscall2(SYS_IOPORT_READ, p, w); }
int      cervus_ioport_write(uint16_t p, int w, uint32_t v) { return (int)__sys_ret(syscall3(SYS_IOPORT_WRITE, p, w, v)); }

void __cervus_assert_fail(const char *expr, const char *file, int line, const char *func)
{
    printf("assertion failed: %s  (%s:%d, %s)\n",
           expr ? expr : "(null)",
           file ? file : "(null)",
           line,
           func ? func : "(null)");
    syscall1(SYS_EXIT, 134);
    for (;;) { }
}

#define _CERVUS_FILT_MAX 128
char *__cervus_filtered_argv[_CERVUS_FILT_MAX + 1];

int __cervus_filter_args(int argc, char **argv)
{
    int out = 0;
    for (int i = 0; i < argc && out < _CERVUS_FILT_MAX; i++) {
        const char *a = argv[i];
        if (i > 0 && a && a[0] == '-' && a[1] == '-' &&
            ((a[2]=='c' && a[3]=='w' && a[4]=='d' && a[5]=='=') ||
             (a[2]=='e' && a[3]=='n' && a[4]=='v' && a[5]==':'))) {
            continue;
        }
        __cervus_filtered_argv[out++] = (char *)a;
    }
    __cervus_filtered_argv[out] = NULL;
    return out;
}
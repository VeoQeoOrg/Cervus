#ifndef CERVUS_USER_H
#define CERVUS_USER_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int64_t intptr_t;

#define SYS_EXIT 0
#define SYS_EXIT_GROUP 1
#define SYS_GETPID 2
#define SYS_GETPPID 3
#define SYS_FORK 4
#define SYS_WAIT 5
#define SYS_YIELD 6
#define SYS_GETUID 7
#define SYS_GETGID 8
#define SYS_SETUID 9
#define SYS_SETGID 10
#define SYS_CAP_GET 11
#define SYS_CAP_DROP 12
#define SYS_TASK_INFO 13
#define SYS_EXECVE 14
#define SYS_READ 20
#define SYS_WRITE 21
#define SYS_OPEN 22
#define SYS_CLOSE 23
#define SYS_SEEK 24
#define SYS_STAT 25
#define SYS_FSTAT 26
#define SYS_IOCTL 27
#define SYS_DUP 28
#define SYS_DUP2 29
#define SYS_PIPE 30
#define SYS_FCNTL 31
#define SYS_READDIR 32
#define SYS_MMAP 40
#define SYS_MUNMAP 41
#define SYS_MPROTECT 42
#define SYS_BRK 43
#define SYS_CLOCK_GET 60
#define SYS_SLEEP_NS 61
#define SYS_UPTIME 62
#define SYS_MEMINFO 63
#define SYS_DBG_PRINT 512
#define SYS_TASK_KILL 515
#define SYS_IOPORT_READ 521
#define SYS_IOPORT_WRITE 522
#define SYS_SHUTDOWN 523
#define SYS_REBOOT 524

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define EINVAL 22
#define EMFILE 24
#define ENOTTY 25
#define ENOSPC 28
#define EPIPE 32
#define ENOSYS 38

#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR 0x002
#define O_CREAT 0x040
#define O_TRUNC 0x200
#define O_APPEND 0x400
#define O_NONBLOCK 0x800
#define O_CLOEXEC 0x80000
#define O_DIRECTORY 0x10000
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED 0x10
#define MAP_FAILED ((void *)-1)

#define WNOHANG 0x1
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFEXITED(s) (((s) & 0x7F) == 0)

#define CAP_IOPORT (1ULL << 0)
#define CAP_RAWMEM (1ULL << 1)
#define CAP_KILL_ANY (1ULL << 4)
#define CAP_SET_PRIO (1ULL << 5)
#define CAP_TASK_SPAWN (1ULL << 6)
#define CAP_TASK_INFO (1ULL << 7)
#define CAP_MMAP_EXEC (1ULL << 8)
#define CAP_SETUID (1ULL << 17)
#define CAP_DBG_SERIAL (1ULL << 20)

typedef struct {
    uint32_t pid, ppid, uid, gid;
    uint64_t capabilities;
    char name[32];
    uint32_t state, priority;
    uint64_t total_runtime_ns;
} cervus_task_info_t;

typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t used_bytes;
    uint64_t usable_bytes;
    uint64_t page_size;
} cervus_meminfo_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} cervus_timespec_t;

typedef struct {
    uint64_t st_ino;
    uint32_t st_type, st_mode, st_uid, st_gid;
    uint64_t st_size, st_blocks;
} cervus_stat_t;

#define DT_UNKNOWN 0
#define DT_FILE 0
#define DT_DIR 1
#define DT_CHR 2
#define DT_BLK 3
#define DT_LNK 4
#define DT_PIPE 5

typedef struct {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[256];
} cervus_dirent_t;

static inline int64_t
__syscall(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
          uint64_t a4, uint64_t a5, uint64_t a6) {
    int64_t ret;
    register uint64_t r10 asm("r10") = a4;
    register uint64_t r8 asm("r8") = a5;
    register uint64_t r9 asm("r9") = a6;
    asm volatile("syscall" : "=a"(ret)
                 : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}
#define syscall0(n) __syscall((n), 0, 0, 0, 0, 0, 0)
#define syscall1(n, a) __syscall((n), (uint64_t)(a), 0, 0, 0, 0, 0)
#define syscall2(n, a, b) __syscall((n), (uint64_t)(a), (uint64_t)(b), 0, 0, 0, 0)
#define syscall3(n, a, b, c) __syscall((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), 0, 0, 0)
#define syscall4(n, a, b, c, d) __syscall((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), (uint64_t)(d), 0, 0)
#define syscall5(n, a, b, c, d, e) __syscall((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), (uint64_t)(d), (uint64_t)(e), 0)
#define syscall6(n, a, b, c, d, e, f) __syscall((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), (uint64_t)(d), (uint64_t)(e), (uint64_t)(f))

static inline __attribute__((noreturn)) void exit(int c) {
    syscall1(SYS_EXIT, c);
    __builtin_unreachable();
}
static inline __attribute__((noreturn)) void exit_group(int c) {
    syscall1(SYS_EXIT_GROUP, c);
    __builtin_unreachable();
}
static inline pid_t getpid(void) {
    return (pid_t)syscall0(SYS_GETPID);
}
static inline pid_t getppid(void) {
    return (pid_t)syscall0(SYS_GETPPID);
}
static inline uid_t getuid(void) {
    return (uid_t)syscall0(SYS_GETUID);
}
static inline gid_t getgid(void) {
    return (gid_t)syscall0(SYS_GETGID);
}
static inline int setuid(uid_t u) {
    return (int)syscall1(SYS_SETUID, u);
}
static inline int setgid(gid_t g) {
    return (int)syscall1(SYS_SETGID, g);
}
static inline pid_t fork(void) {
    return (pid_t)syscall0(SYS_FORK);
}
static inline void yield(void) {
    syscall0(SYS_YIELD);
}
static inline int execve(const char *p, const char *argv[], const char *envp[]) {
    return (int)syscall3(SYS_EXECVE, p, argv, envp);
}
static inline pid_t waitpid(pid_t pid, int *st, int fl) {
    return (pid_t)syscall3(SYS_WAIT, pid, st, fl);
}
static inline pid_t wait(int *st) {
    return waitpid(-1, st, 0);
}
static inline uint64_t cap_get(void) {
    return (uint64_t)syscall0(SYS_CAP_GET);
}
static inline int cap_drop(uint64_t m) {
    return (int)syscall1(SYS_CAP_DROP, m);
}
static inline int task_info(pid_t pid, cervus_task_info_t *b) {
    return (int)syscall2(SYS_TASK_INFO, pid, b);
}
static inline int task_kill(pid_t pid) {
    return (int)syscall1(SYS_TASK_KILL, pid);
}

static inline ssize_t write(int fd, const void *buf, size_t n) {
    return (ssize_t)syscall3(SYS_WRITE, fd, buf, n);
}
static inline ssize_t read(int fd, void *buf, size_t n) {
    return (ssize_t)syscall3(SYS_READ, fd, buf, n);
}
static inline int open(const char *path, int flags, int mode) {
    return (int)syscall3(SYS_OPEN, path, flags, mode);
}
static inline int close(int fd) {
    return (int)syscall1(SYS_CLOSE, fd);
}
static inline off_t lseek(int fd, off_t o, int w) {
    return (off_t)syscall3(SYS_SEEK, fd, (uint64_t)o, w);
}
static inline int stat(const char *p, cervus_stat_t *b) {
    return (int)syscall2(SYS_STAT, p, b);
}
static inline int fstat(int fd, cervus_stat_t *b) {
    return (int)syscall2(SYS_FSTAT, fd, b);
}
static inline int dup(int fd) {
    return (int)syscall1(SYS_DUP, fd);
}
static inline int dup2(int old, int nw) {
    return (int)syscall2(SYS_DUP2, old, nw);
}
static inline int pipe(int fds[2]) {
    return (int)syscall1(SYS_PIPE, fds);
}
static inline int fcntl(int fd, int cmd, int arg) {
    return (int)syscall3(SYS_FCNTL, fd, cmd, arg);
}
static inline int readdir(int fd, cervus_dirent_t *d) {
    return (int)syscall2(SYS_READDIR, fd, d);
}

static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    return (void *)syscall6(SYS_MMAP, addr, len, prot, flags, fd, (uint64_t)off);
}
static inline int munmap(void *addr, size_t len) {
    return (int)syscall2(SYS_MUNMAP, addr, len);
}
static inline void *sbrk(intptr_t delta) {
    uintptr_t cur = (uintptr_t)syscall1(SYS_BRK, 0);
    if (!delta)
        return (void *)cur;
    uintptr_t nw = (uintptr_t)syscall1(SYS_BRK, cur + (uintptr_t)delta);
    return (nw == cur + (uintptr_t)delta) ? (void *)cur : (void *)-1;
}
static inline int meminfo(cervus_meminfo_t *m) {
    return (int)syscall1(SYS_MEMINFO, m);
}

static inline int clock_gettime(int id, cervus_timespec_t *ts) {
    return (int)syscall2(SYS_CLOCK_GET, id, ts);
}
static inline int nanosleep_simple(uint64_t ns) {
    return (int)syscall1(SYS_SLEEP_NS, ns);
}
static inline uint64_t uptime_ns(void) {
    return (uint64_t)syscall0(SYS_UPTIME);
}

static inline ssize_t dbg_print(const char *s, size_t n) {
    return (ssize_t)syscall2(SYS_DBG_PRINT, s, n);
}
static inline uint32_t ioport_read(uint16_t p, int w) {
    return (uint32_t)syscall2(SYS_IOPORT_READ, p, w);
}
static inline int ioport_write(uint16_t p, int w, uint32_t v) {
    return (int)syscall3(SYS_IOPORT_WRITE, p, w, v);
}

static inline int cervus_shutdown(void) {
    return (int)syscall0(SYS_SHUTDOWN);
}
static inline int cervus_reboot(void) {
    return (int)syscall0(SYS_REBOOT);
}

static inline size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (uint8_t)*a - (uint8_t)*b;
}
static inline int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (uint8_t)a[i] - (uint8_t)b[i];
        if (!a[i])
            return 0;
    }
    return 0;
}
static inline char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}
static inline char *strncpy(char *d, const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}
static inline char *strcat(char *d, const char *s) {
    char *r = d;
    while (*d)
        d++;
    while ((*d++ = *s++))
        ;
    return r;
}
static inline char *strchr(const char *s, int c) {
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c == 0 ? (char *)s : 0;
}

static inline char *strrchr(const char *s, int c) {
    const char *r = 0;
    for (; *s; s++)
        if (*s == (char)c)
            r = s;
    return (char *)(c == 0 ? s : r);
}
static inline char *strstr(const char *h, const char *n) {
    if (!*n)
        return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (!*b)
            return (char *)h;
    }
    return 0;
}

static inline void *memset(void *d, int c, size_t n) {
    uint8_t *p = (uint8_t *)d;
    while (n--)
        *p++ = (uint8_t)c;
    return d;
}
static inline void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    while (n--)
        *dd++ = *ss++;
    return d;
}
static inline int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}

static inline int u64_to_dec(uint64_t v, char *buf) {
    if (!v) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    char t[21];
    int i = 20;
    t[i] = '\0';
    while (v) {
        t[--i] = '0' + v % 10;
        v /= 10;
    }
    int l = 20 - i;
    memcpy(buf, t + i, (size_t)(l + 1));
    return l;
}

static inline int u64_to_hex(uint64_t v, char *buf) {
    static const char h[] = "0123456789abcdef";
    buf[0] = '0';
    buf[1] = 'x';
    if (!v) {
        buf[2] = '0';
        buf[3] = '\0';
        return 3;
    }
    char t[17];
    int i = 16;
    t[16] = '\0';
    while (v) {
        t[--i] = h[v & 0xF];
        v >>= 4;
    }
    int l = 16 - i;
    memcpy(buf + 2, t + i, (size_t)l);
    buf[2 + l] = '\0';
    return 2 + l;
}

static inline int64_t atoi64(const char *s) {
    int64_t v = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+')
        s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

static inline void ws(const char *s) {
    write(1, s, strlen(s));
}
static inline void wc(char c) {
    write(1, &c, 1);
}
static inline void wn(void) {
    write(1, "\n", 1);
}
static inline void wse(const char *s) {
    write(2, s, strlen(s));
}

static inline void print_u64(uint64_t v) {
    char b[22];
    u64_to_dec(v, b);
    ws(b);
}
static inline void print_i64(int64_t v) {
    if (v < 0) {
        wc('-');
        print_u64((uint64_t)(-v));
    } else
        print_u64((uint64_t)v);
}
static inline void print_hex(uint64_t v) {
    char b[20];
    u64_to_hex(v, b);
    ws(b);
}
static inline void print_pad(uint64_t v, int width) {
    char t[22];
    int i = 21;
    t[i] = '\0';
    if (!v)
        t[--i] = '0';
    else
        while (v) {
            t[--i] = '0' + v % 10;
            v /= 10;
        }
    int len = 21 - i;
    for (int p = len; p < width; p++)
        wc(' ');
    ws(t + i);
}

static inline void print_pad0(uint64_t v, int width) {
    char t[22];
    int i = 21;
    t[i] = '\0';
    if (!v)
        t[--i] = '0';
    else
        while (v) {
            t[--i] = '0' + v % 10;
            v /= 10;
        }
    int len = 21 - i;
    for (int p = len; p < width; p++)
        wc('0');
    ws(t + i);
}

static inline void printf_fd(int fd, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    char nb[24];
    while (*fmt) {
        if (*fmt != '%') {
            write(fd, fmt, 1);
            fmt++;
            continue;
        }
        fmt++;
        int is_long = 0;
        while (*fmt == 'l') {
            is_long++;
            fmt++;
        }
        switch (*fmt) {
            case 's': {
                const char *s = __builtin_va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                write(fd, s, strlen(s));
                break;
            }
            case 'd': {
                int64_t v = is_long ? __builtin_va_arg(ap, int64_t) : (int64_t) __builtin_va_arg(ap, int);
                if (v < 0) {
                    write(fd, "-", 1);
                    v = -v;
                }
                u64_to_dec((uint64_t)v, nb);
                write(fd, nb, strlen(nb));
                break;
            }
            case 'u': {
                uint64_t v = is_long ? __builtin_va_arg(ap, uint64_t) : (uint64_t) __builtin_va_arg(ap, unsigned);
                u64_to_dec(v, nb);
                write(fd, nb, strlen(nb));
                break;
            }
            case 'x':
            case 'X': {
                uint64_t v = is_long ? __builtin_va_arg(ap, uint64_t) : (uint64_t) __builtin_va_arg(ap, unsigned);
                u64_to_hex(v, nb);
                write(fd, nb + 2, strlen(nb + 2));
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t) __builtin_va_arg(ap, void *);
                u64_to_hex(v, nb);
                write(fd, nb, strlen(nb));
                break;
            }
            case 'c': {
                char c = (char)__builtin_va_arg(ap, int);
                write(fd, &c, 1);
                break;
            }
            case '%':
                write(fd, "%", 1);
                break;
            default:
                write(fd, "%", 1);
                write(fd, fmt, 1);
                break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}

#define printf(...) printf_fd(1, __VA_ARGS__)
#define fprintf(fd, ...) printf_fd(fd, __VA_ARGS__)
#define puts(s)                   \
    do {                          \
        write(1, (s), strlen(s)); \
        write(1, "\n", 1);        \
    } while (0)

static inline int readline(int fd, char *buf, int maxlen) {
    int i = 0;
    while (i < maxlen - 1) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) {
            buf[i] = '\0';
            return i > 0 ? i : -1;
        }
        if (c == '\r')
            continue;
        buf[i++] = c;
        if (c == '\n')
            break;
    }
    buf[i] = '\0';
    return i;
}

static inline void path_join(const char *base, const char *name, char *out, size_t sz) {
    if (!name || !name[0]) {
        strncpy(out, base, sz - 1);
        out[sz - 1] = '\0';
        return;
    }
    if (name[0] == '/') {
        strncpy(out, name, sz - 1);
        out[sz - 1] = '\0';
        return;
    }
    strncpy(out, base, sz - 1);
    out[sz - 1] = '\0';
    size_t bl = strlen(out);
    if (bl > 0 && out[bl - 1] != '/' && bl + 1 < sz) {
        out[bl] = '/';
        out[bl + 1] = '\0';
        bl++;
    }
    size_t nl = strlen(name);
    if (bl + nl + 1 < sz)
        memcpy(out + bl, name, nl + 1);
}

static inline void path_norm(char *path) {
    char tmp[512];
    strncpy(tmp, path, 511);
    tmp[511] = '\0';
    char *parts[64];
    int np = 0;
    char *p = tmp;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        char *s = p;
        while (*p && *p != '/')
            p++;
        if (*p)
            *p++ = '\0';
        if (strcmp(s, ".") == 0)
            continue;
        if (strcmp(s, "..") == 0) {
            if (np > 0)
                np--;
            continue;
        }
        if (np < 64)
            parts[np++] = s;
    }
    char out[512];
    size_t ol = 0;
    for (int i = 0; i < np; i++) {
        out[ol++] = '/';
        size_t pl = strlen(parts[i]);
        memcpy(out + ol, parts[i], pl);
        ol += pl;
    }
    out[ol] = '\0';
    if (ol == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
    strncpy(path, out, 512);
}

static inline const char *get_cwd_flag(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-' &&
            argv[i][2] == 'c' && argv[i][3] == 'w' &&
            argv[i][4] == 'd' && argv[i][5] == '=')
            return argv[i] + 6;
    }
    return "/";
}
static inline void resolve_path(const char *cwd, const char *path, char *out, size_t sz) {
    if (!path || !path[0]) {
        strncpy(out, cwd, sz - 1);
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

#define CERVUS_MAIN(fn)                                               \
    static void fn(int argc, char **argv);                            \
    __attribute__((naked)) void _start(void) {                        \
        asm volatile(                                                 \
            "mov %%rsp, %%rdi\n"                                      \
            "and $-16,  %%rsp\n"                                      \
            "call _cvm_tramp_" #fn "\n"                               \
            "ud2\n" ::: "memory");                                    \
    }                                                                 \
    __attribute__((used)) static void _cvm_tramp_##fn(uint64_t *sp) { \
        int argc = (int)sp[0];                                        \
        char **argv = (char **)(sp + 1);                              \
        fn(argc, argv);                                               \
        exit(0);                                                      \
    }                                                                 \
    static void fn(int argc, char **argv)

#define C_RESET "\x1b[0m"
#define C_BOLD "\x1b[1m"
#define C_RED "\x1b[1;31m"
#define C_GREEN "\x1b[1;32m"
#define C_YELLOW "\x1b[1;33m"
#define C_BLUE "\x1b[1;34m"
#define C_CYAN "\x1b[1;36m"
#define C_GRAY "\x1b[90m"

#endif
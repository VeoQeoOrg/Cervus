#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/cervus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>

int __cervus_errno = 0;

static long __sys_ret(long r)
{
    if (r < 0 && r > -4096) {
        __cervus_errno = (int)-r;
        return -1;
    }
    return r;
}

int isdigit(int c)  { return c >= '0' && c <= '9'; }
int isalpha(int c)  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c)  { return isdigit(c) || isalpha(c); }
int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
int islower(int c)  { return c >= 'a' && c <= 'z'; }
int isspace(int c)  { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isprint(int c)  { return (unsigned char)c >= 0x20 && (unsigned char)c < 0x7F; }
int isgraph(int c)  { return (unsigned char)c > 0x20 && (unsigned char)c < 0x7F; }
int ispunct(int c)  { return isgraph(c) && !isalnum(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int iscntrl(int c)  { return (unsigned char)c < 0x20 || c == 0x7F; }
int isblank(int c)  { return c == ' ' || c == '\t'; }
int toupper(int c)  { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c)  { return isupper(c) ? c - 'A' + 'a' : c; }

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    while (n--) { if (*p == (unsigned char)c) return (void *)p; p++; }
    return NULL;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) { }
    return r;
}
char *strncpy(char *d, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s)
{
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++)) { }
    return r;
}
char *strncat(char *d, const char *s, size_t n)
{
    char *r = d;
    while (*d) d++;
    for (size_t i = 0; i < n && s[i]; i++) *d++ = s[i];
    *d = '\0';
    return r;
}
char *strchr(const char *s, int c)
{
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return c == 0 ? (char *)s : NULL;
}
char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (; *s; s++) if (*s == (char)c) r = s;
    return (char *)(c == 0 ? s : r);
}
char *strstr(const char *h, const char *n)
{
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return NULL;
}
size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;
    while (s[n]) {
        int found = 0;
        for (size_t i = 0; accept[i]; i++)
            if (s[n] == accept[i]) { found = 1; break; }
        if (!found) break;
        n++;
    }
    return n;
}
size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n]) {
        for (size_t i = 0; reject[i]; i++)
            if (s[n] == reject[i]) return n;
        n++;
    }
    return n;
}

void *malloc(size_t);

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}

char *strerror(int err)
{
    switch (err) {
        case 0:       return "Success";
        case EPERM:   return "Operation not permitted";
        case ENOENT:  return "No such file or directory";
        case ESRCH:   return "No such process";
        case EINTR:   return "Interrupted system call";
        case EIO:     return "Input/output error";
        case EBADF:   return "Bad file descriptor";
        case ECHILD:  return "No child processes";
        case EAGAIN:  return "Resource temporarily unavailable";
        case ENOMEM:  return "Cannot allocate memory";
        case EACCES:  return "Permission denied";
        case EFAULT:  return "Bad address";
        case EBUSY:   return "Device or resource busy";
        case EEXIST:  return "File exists";
        case ENODEV:  return "No such device";
        case ENOTDIR: return "Not a directory";
        case EISDIR:  return "Is a directory";
        case EINVAL:  return "Invalid argument";
        case EMFILE:  return "Too many open files";
        case ENOTTY:  return "Inappropriate ioctl for device";
        case ENOSPC:  return "No space left on device";
        case EPIPE:   return "Broken pipe";
        case ENOSYS:  return "Function not implemented";
        default:      return "Unknown error";
    }
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
int unlink(const char *path)
{
    return (int)__sys_ret(syscall1(SYS_UNLINK, path));
}
int rmdir(const char *path)
{
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
    return (int)__sys_ret(syscall3(SYS_EXECVE, path, argv, envp));
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
    buf[0] = '/';
    buf[1] = '\0';
    return buf;
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
    return (int)__sys_ret(syscall2(SYS_STAT, path, out));
}
int fstat(int fd, struct stat *out)
{
    return (int)__sys_ret(syscall2(SYS_FSTAT, fd, out));
}
int mkdir(const char *path, mode_t mode)
{
    return (int)__sys_ret(syscall2(SYS_MKDIR, path, mode));
}
int chmod(const char *path, mode_t mode)
{
    (void)path; (void)mode;
    return 0;
}

int      cervus_task_info(pid_t p, cervus_task_info_t *o) { return (int)__sys_ret(syscall2(SYS_TASK_INFO, p, o)); }
int      cervus_task_kill(pid_t p)                        { return (int)__sys_ret(syscall1(SYS_TASK_KILL, p)); }
uint64_t cervus_cap_get(void)                             { return (uint64_t)syscall0(SYS_CAP_GET); }
int      cervus_cap_drop(uint64_t m)                      { return (int)__sys_ret(syscall1(SYS_CAP_DROP, m)); }

int      cervus_meminfo(cervus_meminfo_t *m)              { return (int)__sys_ret(syscall1(SYS_MEMINFO, m)); }
uint64_t cervus_uptime_ns(void)                           { return (uint64_t)syscall0(SYS_UPTIME); }
int      cervus_clock_gettime(int id, cervus_timespec_t *t) { return (int)__sys_ret(syscall2(SYS_CLOCK_GET, id, t)); }
int      cervus_nanosleep(uint64_t ns)                    { return (int)__sys_ret(syscall1(SYS_SLEEP_NS, ns)); }

int      cervus_shutdown(void) { return (int)__sys_ret(syscall0(SYS_SHUTDOWN)); }
int      cervus_reboot(void)   { return (int)__sys_ret(syscall0(SYS_REBOOT)); }

int      cervus_disk_info(int i, cervus_disk_info_t *o)                             { return (int)__sys_ret(syscall2(SYS_DISK_INFO, (uint64_t)i, o)); }
int      cervus_disk_mount(const char *d, const char *p)                            { return (int)__sys_ret(syscall2(SYS_DISK_MOUNT, d, p)); }
int      cervus_disk_umount(const char *p)                                          { return (int)__sys_ret(syscall1(SYS_DISK_UMOUNT, p)); }
int      cervus_disk_format(const char *d, const char *l)                           { return (int)__sys_ret(syscall2(SYS_DISK_FORMAT, d, l)); }
int      cervus_disk_mkfs_fat32(const char *d, const char *l)                       { return (int)__sys_ret(syscall2(SYS_DISK_MKFS_FAT32, d, l)); }
int      cervus_disk_partition(const char *d, const cervus_mbr_part_t *s, uint64_t n) { return (int)__sys_ret(syscall3(SYS_DISK_PARTITION, d, s, n)); }
int      cervus_disk_read_raw(const char *d, uint64_t lba, uint64_t c, void *b)     { return (int)__sys_ret(syscall4(SYS_DISK_READ_RAW, d, lba, c, b)); }
int      cervus_disk_write_raw(const char *d, uint64_t lba, uint64_t c, const void *b) { return (int)__sys_ret(syscall4(SYS_DISK_WRITE_RAW, d, lba, c, b)); }
long     cervus_disk_list_parts(cervus_part_info_t *o, int m)                       { return __sys_ret(syscall2(SYS_DISK_LIST_PARTS, o, m)); }
long     cervus_disk_bios_install(const char *d, const void *sd, uint32_t ss)       { return __sys_ret(syscall3(SYS_DISK_BIOS_INSTALL, d, sd, ss)); }

long     cervus_list_mounts(cervus_mount_info_t *o, int m)                          { return __sys_ret(syscall2(SYS_LIST_MOUNTS, o, m)); }
long     cervus_statvfs(const char *p, cervus_statvfs_t *o)                         { return __sys_ret(syscall2(SYS_STATVFS, p, o)); }

uint32_t cervus_ioport_read(uint16_t p, int w)                                      { return (uint32_t)syscall2(SYS_IOPORT_READ, p, w); }
int      cervus_ioport_write(uint16_t p, int w, uint32_t v)                         { return (int)__sys_ret(syscall3(SYS_IOPORT_WRITE, p, w, v)); }

void    *mmap(void *a, size_t l, int p, int f, int fd, off_t o) {
    long r = syscall6(SYS_MMAP, a, l, p, f, fd, (uint64_t)o);
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return MAP_FAILED; }
    return (void *)r;
}
int      munmap(void *a, size_t l) { return (int)__sys_ret(syscall2(SYS_MUNMAP, a, l)); }

pid_t    waitpid(pid_t p, int *s, int f) { return (pid_t)__sys_ret(syscall3(SYS_WAIT, p, s, f)); }
pid_t    wait(int *s) { return waitpid(-1, s, 0); }

ssize_t  cervus_dbg_print(const char *b, size_t n) { return (ssize_t)syscall2(SYS_DBG_PRINT, b, n); }

void *malloc(size_t n)
{
    if (n == 0) n = 1;
    n = (n + 15u) & ~(size_t)15u;
    void *p = sbrk((intptr_t)n);
    return (p == (void *)-1) ? NULL : p;
}
void *calloc(size_t nm, size_t sz)
{
    size_t t = nm * sz;
    if (nm && t / nm != sz) { __cervus_errno = ENOMEM; return NULL; }
    void *p = malloc(t);
    if (p) memset(p, 0, t);
    return p;
}
void *realloc(void *p, size_t n)
{
    void *np = malloc(n);
    if (!np) return NULL;
    if (p && n) memcpy(np, p, n);
    return np;
}
void free(void *p) { (void)p; }

#define ATEXIT_MAX 32
static void (*__atexit_fns[ATEXIT_MAX])(void);
static int __atexit_cnt = 0;

int atexit(void (*fn)(void))
{
    if (__atexit_cnt >= ATEXIT_MAX) return -1;
    __atexit_fns[__atexit_cnt++] = fn;
    return 0;
}

int fflush(FILE *stream);

void exit(int status)
{
    while (__atexit_cnt > 0) {
        __atexit_cnt--;
        if (__atexit_fns[__atexit_cnt]) __atexit_fns[__atexit_cnt]();
    }
    extern FILE *stdout;
    extern FILE *stderr;
    fflush(stdout);
    fflush(stderr);
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}
void abort(void)
{
    syscall1(SYS_EXIT, 134);
    __builtin_unreachable();
}

int abs(int x)         { return x < 0 ? -x : x; }
long labs(long x)      { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

static long long __parse_signed(const char *s, char **end, int base, int is_unsigned)
{
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    } else if (base == 0 && *s == '0') {
        s++; base = 8;
    } else if (base == 0) {
        base = 10;
    }
    unsigned long long v = 0;
    while (*s) {
        int d;
        if (isdigit((unsigned char)*s)) d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long long)base + (unsigned long long)d;
        s++;
    }
    if (end) *end = (char *)s;
    if (is_unsigned) return (long long)v;
    return neg ? -(long long)v : (long long)v;
}

long          strtol(const char *s, char **e, int b)    { return (long)__parse_signed(s, e, b, 0); }
long long     strtoll(const char *s, char **e, int b)   { return __parse_signed(s, e, b, 0); }
unsigned long strtoul(const char *s, char **e, int b)   { return (unsigned long)__parse_signed(s, e, b, 1); }
unsigned long long strtoull(const char *s, char **e, int b) { return (unsigned long long)__parse_signed(s, e, b, 1); }

int       atoi(const char *s)  { return (int)strtol(s, NULL, 10); }
long      atol(const char *s)  { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtoll(s, NULL, 10); }

static unsigned long __rand_state = 1;
int  rand(void)              { __rand_state = __rand_state * 1103515245UL + 12345UL; return (int)((__rand_state >> 16) & 0x7FFF); }
void srand(unsigned int seed) { __rand_state = seed; }

static void __qswap(void *a, void *b, size_t sz)
{
    unsigned char tmp;
    unsigned char *pa = (unsigned char *)a;
    unsigned char *pb = (unsigned char *)b;
    while (sz--) { tmp = *pa; *pa++ = *pb; *pb++ = tmp; }
}
void qsort(void *base, size_t nmemb, size_t sz, int (*cmp)(const void *, const void *))
{
    if (nmemb < 2) return;
    unsigned char *arr = (unsigned char *)base;
    unsigned char *pivot = arr + (nmemb - 1) * sz;
    size_t i = 0;
    for (size_t j = 0; j < nmemb - 1; j++) {
        if (cmp(arr + j * sz, pivot) <= 0) {
            if (i != j) __qswap(arr + i * sz, arr + j * sz, sz);
            i++;
        }
    }
    __qswap(arr + i * sz, pivot, sz);
    qsort(arr, i, sz, cmp);
    qsort(arr + (i + 1) * sz, nmemb - i - 1, sz, cmp);
}

extern int    __cervus_argc;
extern char **__cervus_argv;

char *getenv(const char *name)
{
    if (!name) return NULL;
    size_t nl = strlen(name);
    for (int i = 1; i < __cervus_argc; i++) {
        const char *a = __cervus_argv[i];
        if (a && a[0] == '-' && a[1] == '-' &&
            a[2] == 'e' && a[3] == 'n' && a[4] == 'v' && a[5] == ':') {
            const char *kv = a + 6;
            if (strncmp(kv, name, nl) == 0 && kv[nl] == '=')
                return (char *)(kv + nl + 1);
        }
    }
    return NULL;
}

int system(const char *cmd)
{
    (void)cmd;
    __cervus_errno = ENOSYS;
    return -1;
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

static struct __cervus_FILE __stdin  = { 0, 0, 0, 0, NULL, 0, 0 };
static struct __cervus_FILE __stdout = { 1, 0, 0, 0, NULL, 0, 0 };
static struct __cervus_FILE __stderr = { 2, 0, 0, 0, NULL, 0, 0 };

FILE *stdin  = &__stdin;
FILE *stdout = &__stdout;
FILE *stderr = &__stderr;

int fileno(FILE *s) { return s ? s->fd : -1; }
int feof(FILE *s)   { return s ? s->eof : 1; }
int ferror(FILE *s) { return s ? s->err : 1; }
void clearerr(FILE *s) { if (s) { s->eof = 0; s->err = 0; } }

FILE *fopen(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;
    int flags = 0;
    int has_plus = 0;
    for (const char *m = mode + 1; *m; m++) if (*m == '+') has_plus = 1;
    switch (mode[0]) {
        case 'r': flags = has_plus ? O_RDWR : O_RDONLY; break;
        case 'w': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
        case 'a': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
        default: return NULL;
    }
    int fd = open(path, flags, 0644);
    if (fd < 0) return NULL;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }
    f->fd = fd;
    f->eof = 0;
    f->err = 0;
    f->flags = 1;
    f->buf = NULL;
    f->buf_size = 0;
    f->buf_pos = 0;
    return f;
}
int fclose(FILE *s)
{
    if (!s) return EOF;
    int fd = s->fd;
    int owned = s->flags & 1;
    close(fd);
    if (owned) {
        free(s);
    }
    return 0;
}
int fflush(FILE *s) { (void)s; return 0; }

size_t fread(void *buf, size_t size, size_t nmemb, FILE *s)
{
    if (!s || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t got = 0;
    while (got < total) {
        ssize_t r = read(s->fd, (char *)buf + got, total - got);
        if (r < 0) { s->err = 1; break; }
        if (r == 0) { s->eof = 1; break; }
        got += (size_t)r;
    }
    return got / size;
}
size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *s)
{
    if (!s || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t sent = 0;
    while (sent < total) {
        ssize_t w = write(s->fd, (const char *)buf + sent, total - sent);
        if (w < 0) { s->err = 1; break; }
        if (w == 0) break;
        sent += (size_t)w;
    }
    return sent / size;
}
int fseek(FILE *s, long off, int whence)
{
    if (!s) return -1;
    off_t r = lseek(s->fd, (off_t)off, whence);
    if (r == (off_t)-1) { s->err = 1; return -1; }
    s->eof = 0;
    return 0;
}
long ftell(FILE *s)
{
    if (!s) return -1;
    return (long)lseek(s->fd, 0, SEEK_CUR);
}

int fputc(int c, FILE *s)
{
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, s) != 1) return EOF;
    return (int)ch;
}
int fgetc(FILE *s)
{
    unsigned char ch;
    size_t got = fread(&ch, 1, 1, s);
    if (got != 1) return EOF;
    return (int)ch;
}
int fputs(const char *str, FILE *s)
{
    if (!str) return EOF;
    size_t n = strlen(str);
    if (fwrite(str, 1, n, s) != n) return EOF;
    return 0;
}
char *fgets(char *str, int n, FILE *s)
{
    if (!str || n <= 0 || !s) return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(s);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        str[i++] = (char)c;
        if (c == '\n') break;
    }
    str[i] = '\0';
    return str;
}
int putchar(int c) { return fputc(c, stdout); }
int getchar(void)  { return fgetc(stdin); }
int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    if (fputc('\n', stdout) == EOF) return EOF;
    return 1;
}

static void __u64_to_str(uint64_t v, char *out, int base, int upper)
{
    char tmp[32];
    int i = 0;
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = digs[v % (uint64_t)base]; v /= (uint64_t)base; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

int vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap)
{
    size_t pos = 0;
#define __PUT(s, n) do { \
    size_t __n = (n); const char *__s = (s); \
    for (size_t __i = 0; __i < __n; __i++) { \
        if (pos + 1 < sz) buf[pos] = __s[__i]; \
        pos++; \
    } \
} while (0)

    while (*fmt) {
        if (*fmt != '%') { __PUT(fmt, 1); fmt++; continue; }
        fmt++;
        int pad_zero = 0, left_align = 0, plus_flag = 0;
        while (*fmt == '0' || *fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#') {
            if (*fmt == '0') pad_zero = 1;
            else if (*fmt == '-') left_align = 1;
            else if (*fmt == '+') plus_flag = 1;
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }
        int is_long = 0, is_size_t = 0;
        while (*fmt == 'l') { is_long++; fmt++; }
        if (*fmt == 'z') { is_size_t = 1; fmt++; }
        if (*fmt == 'h') { fmt++; }

        char nb[40];
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                size_t l = strlen(s);
                if (prec >= 0 && (size_t)prec < l) l = (size_t)prec;
                int pad = (int)(width > (int)l ? width - (int)l : 0);
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                __PUT(s, l);
                if (left_align)  for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'd': case 'i': {
                int64_t v;
                if (is_long >= 2)   v = va_arg(ap, long long);
                else if (is_long)   v = va_arg(ap, long);
                else if (is_size_t) v = (int64_t)va_arg(ap, size_t);
                else                v = va_arg(ap, int);
                int neg = v < 0;
                uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
                __u64_to_str(u, nb, 10, 0);
                int numlen = (int)strlen(nb) + (neg || plus_flag ? 1 : 0);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align && !pad_zero) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                if (neg)        __PUT("-", 1);
                else if (plus_flag) __PUT("+", 1);
                if (!left_align && pad_zero) for (int i = 0; i < pad; i++) __PUT("0", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'u': {
                uint64_t v;
                if (is_long >= 2)   v = va_arg(ap, unsigned long long);
                else if (is_long)   v = va_arg(ap, unsigned long);
                else if (is_size_t) v = va_arg(ap, size_t);
                else                v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 10, 0);
                int numlen = (int)strlen(nb);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(pad_zero ? "0" : " ", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'x': case 'X': {
                uint64_t v;
                if (is_long >= 2)   v = va_arg(ap, unsigned long long);
                else if (is_long)   v = va_arg(ap, unsigned long);
                else if (is_size_t) v = va_arg(ap, size_t);
                else                v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 16, *fmt == 'X');
                int numlen = (int)strlen(nb);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(pad_zero ? "0" : " ", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'o': {
                uint64_t v;
                if (is_long >= 2) v = va_arg(ap, unsigned long long);
                else if (is_long) v = va_arg(ap, unsigned long);
                else              v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 8, 0);
                __PUT(nb, strlen(nb));
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
                __PUT("0x", 2);
                __u64_to_str(v, nb, 16, 0);
                __PUT(nb, strlen(nb));
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                __PUT(&c, 1);
                break;
            }
            case '%': __PUT("%", 1); break;
            default: {
                char c = *fmt;
                __PUT("%", 1);
                __PUT(&c, 1);
                break;
            }
        }
        fmt++;
    }
    if (sz > 0) buf[pos < sz ? pos : sz - 1] = '\0';
#undef __PUT
    return (int)pos;
}

int snprintf(char *buf, size_t sz, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sz, fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(FILE *s, const char *fmt, va_list ap)
{
    char small[512];
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(small, sizeof(small), fmt, ap2);
    va_end(ap2);
    if (needed < (int)sizeof(small)) {
        fwrite(small, 1, (size_t)needed, s);
        return needed;
    }
    char *big = (char *)malloc((size_t)needed + 1);
    if (!big) {
        fwrite(small, 1, sizeof(small) - 1, s);
        return (int)sizeof(small) - 1;
    }
    vsnprintf(big, (size_t)needed + 1, fmt, ap);
    fwrite(big, 1, (size_t)needed, s);
    return needed;
}

int fprintf(FILE *s, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(s, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

void perror(const char *msg)
{
    if (msg && *msg) { fputs(msg, stderr); fputs(": ", stderr); }
    fputs(strerror(__cervus_errno), stderr);
    fputc('\n', stderr);
}

int remove(const char *path) { return unlink(path); }
int rename(const char *oldp, const char *newp)
{
    return (int)__sys_ret(syscall2(SYS_RENAME, oldp, newp));
}

typedef struct {
    uint64_t d_ino;
    uint8_t  d_type;
    char     d_name[256];
} __kernel_dirent_t;

struct __cervus_DIR {
    int fd;
    struct dirent buf;
};

DIR *opendir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return NULL;
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) { close(fd); return NULL; }
    d->fd = fd;
    return d;
}
struct dirent *readdir(DIR *dirp)
{
    if (!dirp) return NULL;
    __kernel_dirent_t kde;
    int r = (int)syscall2(SYS_READDIR, dirp->fd, &kde);
    if (r != 0) return NULL;
    dirp->buf.d_ino  = kde.d_ino;
    dirp->buf.d_type = kde.d_type;
    size_t nl = strlen(kde.d_name);
    if (nl >= sizeof(dirp->buf.d_name)) nl = sizeof(dirp->buf.d_name) - 1;
    memcpy(dirp->buf.d_name, kde.d_name, nl);
    dirp->buf.d_name[nl] = '\0';
    return &dirp->buf;
}
int closedir(DIR *dirp)
{
    if (!dirp) return -1;
    int fd = dirp->fd;
    return close(fd);
}
void rewinddir(DIR *dirp)
{
    if (!dirp) return;
    lseek(dirp->fd, 0, SEEK_SET);
}
int dirfd(DIR *dirp) { return dirp ? dirp->fd : -1; }

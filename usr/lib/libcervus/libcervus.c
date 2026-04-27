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
void *memmem(const void *haystack, size_t hlen,
             const void *needle, size_t nlen)
{
    if (nlen == 0) return (void *)haystack;
    if (hlen < nlen) return NULL;
    const unsigned char *h = (const unsigned char *)haystack;
    const unsigned char *n = (const unsigned char *)needle;
    size_t last = hlen - nlen;
    for (size_t i = 0; i <= last; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, nlen) == 0)
            return (void *)(h + i);
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

typedef struct __mblock {
    size_t size;
    size_t prev_size;
} __mblock_t;

#define MB_HDR_SZ        (sizeof(__mblock_t))
#define MB_ALIGN         16
#define MB_MIN_TOTAL     32
#define MB_FREE_BIT      ((size_t)1)
#define MB_SIZE(b)       ((b)->size & ~MB_FREE_BIT)
#define MB_IS_FREE(b)    (((b)->size & MB_FREE_BIT) != 0)
#define MB_USER(b)       ((void *)((char *)(b) + MB_HDR_SZ))
#define MB_FROM_USER(p)  ((__mblock_t *)((char *)(p) - MB_HDR_SZ))

static __mblock_t *__heap_start = NULL;
static __mblock_t *__heap_end   = NULL;

static inline size_t __align_up(size_t n, size_t a) {
    return (n + a - 1) & ~(a - 1);
}

static inline __mblock_t *__mb_next(__mblock_t *b) {
    return (__mblock_t *)((char *)b + MB_SIZE(b));
}

static inline __mblock_t *__mb_prev(__mblock_t *b) {
    if (b->prev_size == 0) return NULL;
    return (__mblock_t *)((char *)b - b->prev_size);
}

static __mblock_t *__heap_grow(size_t need) {
    size_t chunk = __align_up(need + MB_HDR_SZ, 65536);

    if (!__heap_start) {
        void *base = sbrk((intptr_t)chunk);
        if (base == (void *)-1) return NULL;

        uintptr_t addr = (uintptr_t)base;
        uintptr_t aligned = (addr + MB_ALIGN - 1) & ~(uintptr_t)(MB_ALIGN - 1);
        size_t lost = aligned - addr;
        if (lost >= chunk - MB_MIN_TOTAL - MB_HDR_SZ) {
            __cervus_errno = ENOMEM;
            return NULL;
        }

        __heap_start = (__mblock_t *)aligned;
        size_t usable = chunk - lost;

        __mblock_t *first = __heap_start;
        first->size      = (usable - MB_HDR_SZ) | MB_FREE_BIT;
        first->prev_size = 0;

        __heap_end = (__mblock_t *)((char *)first + MB_SIZE(first));
        __heap_end->size      = 0;
        __heap_end->prev_size = MB_SIZE(first);

        return first;
    }

    void *p = sbrk((intptr_t)chunk);
    if (p == (void *)-1) return NULL;
    if ((uintptr_t)p != (uintptr_t)__heap_end) {
        __cervus_errno = ENOMEM;
        return NULL;
    }

    __mblock_t *new_block = __heap_end;
    new_block->size = (chunk - MB_HDR_SZ) | MB_FREE_BIT;
    __mblock_t *new_end = (__mblock_t *)((char *)new_block + MB_SIZE(new_block));
    new_end->size      = 0;
    new_end->prev_size = MB_SIZE(new_block);
    __heap_end = new_end;

    __mblock_t *prev = __mb_prev(new_block);
    if (prev && MB_IS_FREE(prev)) {
        size_t merged_sz = MB_SIZE(prev) + MB_SIZE(new_block);
        prev->size = merged_sz | MB_FREE_BIT;
        __heap_end->prev_size = merged_sz;
        return prev;
    }
    return new_block;
}

static void __mb_split(__mblock_t *b, size_t need) {
    size_t cur = MB_SIZE(b);
    if (cur < need + MB_MIN_TOTAL) {
        b->size = cur;
        return;
    }
    b->size = need;

    __mblock_t *rest = (__mblock_t *)((char *)b + need);
    rest->size      = (cur - need) | MB_FREE_BIT;
    rest->prev_size = need;

    __mblock_t *after = __mb_next(rest);
    if (after) after->prev_size = MB_SIZE(rest);
}

void *malloc(size_t n)
{
    if (n == 0) n = 1;
    size_t need = __align_up(n + MB_HDR_SZ, MB_ALIGN);
    if (need < MB_MIN_TOTAL) need = MB_MIN_TOTAL;

    for (__mblock_t *b = __heap_start; b && b != __heap_end; b = __mb_next(b)) {
        if (MB_IS_FREE(b) && MB_SIZE(b) >= need) {
            __mb_split(b, need);
            return MB_USER(b);
        }
    }

    __mblock_t *grown = __heap_grow(need);
    if (!grown) return NULL;
    if (MB_SIZE(grown) < need) {
        __cervus_errno = ENOMEM;
        return NULL;
    }
    __mb_split(grown, need);
    return MB_USER(grown);
}

void *calloc(size_t nm, size_t sz)
{
    size_t t = nm * sz;
    if (nm && t / nm != sz) { __cervus_errno = ENOMEM; return NULL; }
    void *p = malloc(t);
    if (p) memset(p, 0, t);
    return p;
}

void free(void *p)
{
    if (!p) return;
    __mblock_t *b = MB_FROM_USER(p);
    b->size = MB_SIZE(b) | MB_FREE_BIT;

    __mblock_t *next = __mb_next(b);
    if (next != __heap_end && MB_IS_FREE(next)) {
        size_t merged = MB_SIZE(b) + MB_SIZE(next);
        b->size = merged | MB_FREE_BIT;
        __mblock_t *after = __mb_next(b);
        if (after) after->prev_size = merged;
    }
    __mblock_t *prev = __mb_prev(b);
    if (prev && MB_IS_FREE(prev)) {
        size_t merged = MB_SIZE(prev) + MB_SIZE(b);
        prev->size = merged | MB_FREE_BIT;
        __mblock_t *after = __mb_next(prev);
        if (after) after->prev_size = merged;
    }
}

void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (n == 0) { free(p); return NULL; }

    __mblock_t *b = MB_FROM_USER(p);
    size_t cur_total = MB_SIZE(b);
    size_t cur_user  = cur_total - MB_HDR_SZ;
    size_t need      = __align_up(n + MB_HDR_SZ, MB_ALIGN);
    if (need < MB_MIN_TOTAL) need = MB_MIN_TOTAL;

    if (need <= cur_total) {
        if (cur_total >= need + MB_MIN_TOTAL) {
            b->size = need;
            __mblock_t *rest = (__mblock_t *)((char *)b + need);
            rest->size      = (cur_total - need) | MB_FREE_BIT;
            rest->prev_size = need;
            __mblock_t *after = __mb_next(rest);
            if (after) after->prev_size = MB_SIZE(rest);
            if (after != __heap_end && MB_IS_FREE(after)) {
                size_t merged = MB_SIZE(rest) + MB_SIZE(after);
                rest->size = merged | MB_FREE_BIT;
                __mblock_t *aft2 = __mb_next(rest);
                if (aft2) aft2->prev_size = merged;
            }
        }
        return p;
    }

    __mblock_t *next = __mb_next(b);
    if (next != __heap_end && MB_IS_FREE(next) &&
        cur_total + MB_SIZE(next) >= need)
    {
        size_t combined = cur_total + MB_SIZE(next);
        b->size = combined;
        __mblock_t *after = __mb_next(b);
        if (after) after->prev_size = combined;
        if (combined >= need + MB_MIN_TOTAL) {
            b->size = need;
            __mblock_t *rest = (__mblock_t *)((char *)b + need);
            rest->size      = (combined - need) | MB_FREE_BIT;
            rest->prev_size = need;
            __mblock_t *aft = __mb_next(rest);
            if (aft) aft->prev_size = MB_SIZE(rest);
        }
        return p;
    }

    void *np = malloc(n);
    if (!np) return NULL;
    memcpy(np, p, cur_user);
    free(p);
    return np;
}

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

uint64_t __cervus_strtod_bits(const char *s, char **endptr)
{
    if (!s) {
        if (endptr) *endptr = (char *)s;
        return 0;
    }
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' ||
           *p == '\r' || *p == '\f' || *p == '\v') p++;

    int sign = 0;
    if (*p == '+') p++;
    else if (*p == '-') { sign = 1; p++; }

    if ((p[0] == 'i' || p[0] == 'I') &&
        (p[1] == 'n' || p[1] == 'N') &&
        (p[2] == 'f' || p[2] == 'F')) {
        p += 3;
        if ((p[0] == 'i' || p[0] == 'I') &&
            (p[1] == 'n' || p[1] == 'N') &&
            (p[2] == 'i' || p[2] == 'I') &&
            (p[3] == 't' || p[3] == 'T') &&
            (p[4] == 'y' || p[4] == 'Y')) p += 5;
        if (endptr) *endptr = (char *)p;
        return ((uint64_t)sign << 63) | 0x7FF0000000000000ULL;
    }
    if ((p[0] == 'n' || p[0] == 'N') &&
        (p[1] == 'a' || p[1] == 'A') &&
        (p[2] == 'n' || p[2] == 'N')) {
        p += 3;
        if (endptr) *endptr = (char *)p;
        return 0x7FF8000000000000ULL;
    }

    uint64_t mant   = 0;
    int      dec_exp = 0;
    int      seen_digit = 0;
    int      seen_dot   = 0;

    while (*p >= '0' && *p <= '9') {
        seen_digit = 1;
        if (mant <= (UINT64_MAX - 9) / 10) {
            mant = mant * 10 + (uint64_t)(*p - '0');
        } else {
            dec_exp++;
        }
        p++;
    }
    if (*p == '.') {
        seen_dot = 1;
        p++;
        while (*p >= '0' && *p <= '9') {
            seen_digit = 1;
            if (mant <= (UINT64_MAX - 9) / 10) {
                mant = mant * 10 + (uint64_t)(*p - '0');
                dec_exp--;
            }
            p++;
        }
    }
    if (!seen_digit) {
        if (endptr) *endptr = (char *)s;
        return 0;
    }

    if (*p == 'e' || *p == 'E') {
        const char *ep = p + 1;
        int esign = 0;
        if (*ep == '+') ep++;
        else if (*ep == '-') { esign = 1; ep++; }
        if (*ep >= '0' && *ep <= '9') {
            int eval = 0;
            while (*ep >= '0' && *ep <= '9') {
                if (eval < 10000) eval = eval * 10 + (*ep - '0');
                ep++;
            }
            dec_exp += esign ? -eval : eval;
            p = ep;
        }
    }
    (void)seen_dot;
    if (endptr) *endptr = (char *)p;

    if (mant == 0) {
        return (uint64_t)sign << 63;
    }
    int bin_exp = 0;

    while ((mant >> 63) == 0) {
        mant <<= 1;
        bin_exp--;
    }
    while (dec_exp > 0) {
        uint64_t hi = (mant >> 32) * 10;
        uint64_t lo = (mant & 0xFFFFFFFFULL) * 10;
        uint64_t carry = (lo >> 32);
        mant = (hi + carry) << 32 | (lo & 0xFFFFFFFFULL);
        uint64_t check_hi = hi + carry;
        if (check_hi >> 32) {
            mant >>= 4;
            mant |= (check_hi & 0xFULL) << 60;
            bin_exp += 4;
        }
        while ((mant >> 63) == 0) {
            mant <<= 1;
            bin_exp--;
        }
        dec_exp--;
    }
    while (dec_exp < 0) {
        uint64_t r = mant / 10;
        while ((r >> 63) == 0 && bin_exp > -16384) {
            r <<= 1;
            bin_exp--;
        }
        mant = r;
        dec_exp++;
    }
    int ieee_exp = bin_exp + 63 + 1023;

    uint64_t frac;
    uint64_t no_implicit = mant & 0x7FFFFFFFFFFFFFFFULL;
    uint64_t round_bit = (no_implicit >> 10) & 1;
    uint64_t sticky    = (no_implicit & 0x3FFULL) ? 1 : 0;
    frac = no_implicit >> 11;
    if (round_bit && (sticky || (frac & 1))) {
        frac++;
        if (frac == (1ULL << 52)) {
            frac = 0;
            ieee_exp++;
        }
    }

    if (ieee_exp >= 0x7FF) {
        return ((uint64_t)sign << 63) | 0x7FF0000000000000ULL;
    }
    if (ieee_exp <= 0) {
        return (uint64_t)sign << 63;
    }
    return ((uint64_t)sign << 63) |
           ((uint64_t)ieee_exp << 52) |
           (frac & 0xFFFFFFFFFFFFFULL);
}


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

int mkstemp(char *template)
{
    if (!template) { __cervus_errno = EINVAL; return -1; }
    size_t len = strlen(template);
    if (len < 6) { __cervus_errno = EINVAL; return -1; }
    char *suf = template + len - 6;
    for (int i = 0; i < 6; i++) {
        if (suf[i] != 'X') { __cervus_errno = EINVAL; return -1; }
    }
    static uint64_t __mkstemp_seq = 0;
    uint64_t pid = (uint64_t)getpid();
    for (int attempt = 0; attempt < 100; attempt++) {
        uint64_t seed = (cervus_uptime_ns() ^ (pid << 32)) + (__mkstemp_seq++);
        const char *alpha = "0123456789abcdefghijklmnopqrstuvwxyz";
        for (int i = 0; i < 6; i++) {
            suf[i] = alpha[seed % 36];
            seed /= 36;
        }
        struct stat st;
        if (stat(template, &st) == 0) continue;
        int fd = open(template, O_RDWR | O_CREAT, 0600);
        if (fd >= 0) return fd;
    }
    __cervus_errno = EEXIST;
    return -1;
}

FILE *tmpfile(void)
{
    char tmpl[64];
    strcpy(tmpl, "/mnt/tmp/tmpXXXXXX");
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        strcpy(tmpl, "/tmp/tmpXXXXXX");
        fd = mkstemp(tmpl);
        if (fd < 0) return NULL;
    }
    unlink(tmpl);
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

#include <termios.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

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

sighandler_t signal(int signum, sighandler_t handler)
{
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

int raise(int sig)
{
    (void)sig;
    return 0;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact)
{
    (void)sig; (void)act;
    if (oldact) {
        oldact->sa_handler = SIG_DFL;
        memset(&oldact->sa_mask, 0, sizeof(oldact->sa_mask));
        oldact->sa_flags = 0;
    }
    return 0;
}

int sigemptyset(sigset_t *set)             { if (set) memset(set, 0, sizeof(*set)); return 0; }
int sigfillset(sigset_t *set)              { if (set) memset(set, 0xFF, sizeof(*set)); return 0; }
int sigaddset(sigset_t *set, int sig)      { (void)set; (void)sig; return 0; }
int sigdelset(sigset_t *set, int sig)      { (void)set; (void)sig; return 0; }
int sigismember(const sigset_t *set, int sig) { (void)set; (void)sig; return 0; }
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how; (void)set;
    if (oldset) memset(oldset, 0, sizeof(*oldset));
    return 0;
}

typedef struct { int64_t tv_sec; int64_t tv_nsec; } __cervus_ts_raw_t;

time_t time(time_t *t)
{
    __cervus_ts_raw_t ts = {0, 0};
    syscall2(SYS_CLOCK_GET, 0, &ts);
    time_t v = (time_t)ts.tv_sec;
    if (t) *t = v;
    return v;
}

int clock_gettime(int clk, struct timespec *tp)
{
    if (!tp) { __cervus_errno = EINVAL; return -1; }
    __cervus_ts_raw_t ts = {0, 0};
    long r = syscall2(SYS_CLOCK_GET, clk, &ts);
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return -1; }
    tp->tv_sec  = (time_t)ts.tv_sec;
    tp->tv_nsec = (long)ts.tv_nsec;
    return 0;
}

clock_t clock(void)
{
    uint64_t up_ns = (uint64_t)syscall0(SYS_UPTIME);
    return (clock_t)(up_ns / 1000ULL);
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    (void)tz;
    if (!tv) { __cervus_errno = EINVAL; return -1; }
    __cervus_ts_raw_t ts = {0, 0};
    syscall2(SYS_CLOCK_GET, 0, &ts);
    tv->tv_sec  = (time_t)ts.tv_sec;
    tv->tv_usec = (long)(ts.tv_nsec / 1000);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!req) { __cervus_errno = EINVAL; return -1; }
    uint64_t ns = (uint64_t)req->tv_sec * 1000000000ULL + (uint64_t)req->tv_nsec;
    long r = syscall1(SYS_SLEEP_NS, ns);
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return -1; }
    return 0;
}

static int __is_leap(int y) {
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static const int __days_in_mon[2][12] = {
    {31,28,31,30,31,30,31,31,30,31,30,31},
    {31,29,31,30,31,30,31,31,30,31,30,31},
};

static struct tm __tm_buf;

struct tm *gmtime(const time_t *t)
{
    if (!t) return NULL;
    long long sec = (long long)*t;
    long days = (long)(sec / 86400);
    long rem  = (long)(sec % 86400);
    if (rem < 0) { rem += 86400; days--; }

    __tm_buf.tm_hour = rem / 3600;
    rem -= __tm_buf.tm_hour * 3600;
    __tm_buf.tm_min  = rem / 60;
    __tm_buf.tm_sec  = rem - __tm_buf.tm_min * 60;

    __tm_buf.tm_wday = (int)((days + 4) % 7);
    if (__tm_buf.tm_wday < 0) __tm_buf.tm_wday += 7;

    int year = 1970;
    while (1) {
        int ly = __is_leap(year);
        int dy = ly ? 366 : 365;
        if (days >= dy) { days -= dy; year++; }
        else if (days < 0) { year--; days += __is_leap(year) ? 366 : 365; }
        else break;
    }
    __tm_buf.tm_year = year - 1900;
    __tm_buf.tm_yday = (int)days;
    int ly = __is_leap(year);
    int m = 0;
    while (m < 12 && days >= __days_in_mon[ly][m]) {
        days -= __days_in_mon[ly][m];
        m++;
    }
    __tm_buf.tm_mon  = m;
    __tm_buf.tm_mday = (int)days + 1;
    __tm_buf.tm_isdst = 0;
    return &__tm_buf;
}

struct tm *localtime(const time_t *t) { return gmtime(t); }

time_t mktime(struct tm *tm)
{
    if (!tm) return (time_t)-1;
    int year = tm->tm_year + 1900;
    int mon  = tm->tm_mon;
    long days = 0;
    for (int y = 1970; y < year; y++) days += __is_leap(y) ? 366 : 365;
    int ly = __is_leap(year);
    for (int m = 0; m < mon; m++) days += __days_in_mon[ly][m];
    days += tm->tm_mday - 1;
    long long sec = (long long)days * 86400LL
                  + (long long)tm->tm_hour * 3600LL
                  + (long long)tm->tm_min * 60LL
                  + (long long)tm->tm_sec;
    return (time_t)sec;
}

static const char *__wday_name[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *__mon_name[]  = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};

static char __asctime_buf[32];
char *asctime(const struct tm *tm)
{
    if (!tm) return NULL;
    int wday = tm->tm_wday; if (wday < 0 || wday > 6) wday = 0;
    int mon  = tm->tm_mon;  if (mon  < 0 || mon  > 11) mon  = 0;
    int y = tm->tm_year + 1900;
    int pos = 0;
    const char *w = __wday_name[wday];
    const char *m = __mon_name[mon];
    __asctime_buf[pos++] = w[0]; __asctime_buf[pos++] = w[1]; __asctime_buf[pos++] = w[2];
    __asctime_buf[pos++] = ' ';
    __asctime_buf[pos++] = m[0]; __asctime_buf[pos++] = m[1]; __asctime_buf[pos++] = m[2];
    __asctime_buf[pos++] = ' ';
    int md = tm->tm_mday;
    __asctime_buf[pos++] = (char)('0' + (md/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (md % 10));
    __asctime_buf[pos++] = ' ';
    int hh = tm->tm_hour, mm = tm->tm_min, ss = tm->tm_sec;
    __asctime_buf[pos++] = (char)('0' + (hh/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (hh % 10));
    __asctime_buf[pos++] = ':';
    __asctime_buf[pos++] = (char)('0' + (mm/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (mm % 10));
    __asctime_buf[pos++] = ':';
    __asctime_buf[pos++] = (char)('0' + (ss/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (ss % 10));
    __asctime_buf[pos++] = ' ';
    __asctime_buf[pos++] = (char)('0' + (y/1000 % 10));
    __asctime_buf[pos++] = (char)('0' + (y/100 % 10));
    __asctime_buf[pos++] = (char)('0' + (y/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (y % 10));
    __asctime_buf[pos++] = '\n';
    __asctime_buf[pos]   = '\0';
    return __asctime_buf;
}

char *ctime(const time_t *t) { return asctime(gmtime(t)); }

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    if (!s || !fmt || !tm || max == 0) return 0;
    size_t i = 0;
    while (*fmt && i + 1 < max) {
        if (*fmt != '%') { s[i++] = *fmt++; continue; }
        fmt++;
        char tmp[16];
        int n = 0;
        switch (*fmt) {
            case 'Y': {
                int y = tm->tm_year + 1900;
                n = 4;
                tmp[0] = '0' + (y/1000)%10;
                tmp[1] = '0' + (y/100)%10;
                tmp[2] = '0' + (y/10)%10;
                tmp[3] = '0' + y%10;
                break;
            }
            case 'm': { int v=tm->tm_mon+1; tmp[0]='0'+v/10; tmp[1]='0'+v%10; n=2; break; }
            case 'd': { int v=tm->tm_mday;  tmp[0]='0'+v/10; tmp[1]='0'+v%10; n=2; break; }
            case 'H': { int v=tm->tm_hour;  tmp[0]='0'+v/10; tmp[1]='0'+v%10; n=2; break; }
            case 'M': { int v=tm->tm_min;   tmp[0]='0'+v/10; tmp[1]='0'+v%10; n=2; break; }
            case 'S': { int v=tm->tm_sec;   tmp[0]='0'+v/10; tmp[1]='0'+v%10; n=2; break; }
            case '%': tmp[0]='%'; n=1; break;
            default:  tmp[0]='%'; tmp[1]=*fmt; n = (*fmt ? 2 : 1); break;
        }
        for (int k = 0; k < n && i + 1 < max; k++) s[i++] = tmp[k];
        if (*fmt) fmt++;
    }
    s[i] = '\0';
    return i;
}

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
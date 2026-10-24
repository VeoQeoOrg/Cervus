#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/file.h>
#include "darwin.h"

#define DO_NONBLOCK  0x4
#define DO_APPEND    0x8
#define DO_SYNC      0x80
#define DO_NOFOLLOW  0x100
#define DO_CREAT     0x200
#define DO_TRUNC     0x400
#define DO_EXCL      0x800
#define DO_NOCTTY    0x20000
#define DO_DIRECTORY 0x100000
#define DO_DSYNC     0x400000
#define DO_CLOEXEC   0x1000000

#define DAT_FDCWD           (-2)
#define DAT_EACCESS         0x10
#define DAT_SYMLINK_NOFOLLOW 0x20
#define DAT_SYMLINK_FOLLOW  0x40
#define DAT_REMOVEDIR       0x80

struct dstat {
    int32_t st_dev;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint64_t st_ino;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t st_rdev;
    struct timespec st_atimespec;
    struct timespec st_mtimespec;
    struct timespec st_ctimespec;
    struct timespec st_birthtimespec;
    int64_t st_size;
    int64_t st_blocks;
    int32_t st_blksize;
    uint32_t st_flags;
    uint32_t st_gen;
    int32_t st_lspare;
    int64_t st_qspare[2];
};
_Static_assert(sizeof(struct dstat) == 144, "Darwin stat64 is 144 bytes");

static int oflags(int f)
{
    int o = f & 3;
    if (f & DO_NONBLOCK) o |= O_NONBLOCK;
    if (f & DO_APPEND) o |= O_APPEND;
    if (f & DO_SYNC) o |= O_SYNC;
    if (f & DO_NOFOLLOW) o |= O_NOFOLLOW;
    if (f & DO_CREAT) o |= O_CREAT;
    if (f & DO_TRUNC) o |= O_TRUNC;
    if (f & DO_EXCL) o |= O_EXCL;
    if (f & DO_NOCTTY) o |= O_NOCTTY;
    if (f & DO_DIRECTORY) o |= O_DIRECTORY;
    if (f & DO_DSYNC) o |= O_DSYNC;
    if (f & DO_CLOEXEC) o |= O_CLOEXEC;
    return o;
}

int d_open_flags(int f)
{
    return oflags(f);
}

static int oflags_back(int o)
{
    int f = o & 3;
    if (o & O_NONBLOCK) f |= DO_NONBLOCK;
    if (o & O_APPEND) f |= DO_APPEND;
    if ((o & O_SYNC) == O_SYNC) f |= DO_SYNC;
    else if (o & O_DSYNC) f |= DO_DSYNC;
    return f;
}

static int atfd(int fd)
{
    return fd == DAT_FDCWD ? AT_FDCWD : fd;
}

static int atflags(int f)
{
    int o = 0;
    if (f & DAT_EACCESS) o |= AT_EACCESS;
    if (f & DAT_SYMLINK_NOFOLLOW) o |= AT_SYMLINK_NOFOLLOW;
    if (f & DAT_SYMLINK_FOLLOW) o |= AT_SYMLINK_FOLLOW;
    if (f & DAT_REMOVEDIR) o |= AT_REMOVEDIR;
    return o;
}

static void to_dstat(const struct stat *s, struct dstat *d)
{
    memset(d, 0, sizeof *d);
    d->st_dev = (int32_t)s->st_dev;
    d->st_mode = (uint16_t)s->st_mode;
    d->st_nlink = (uint16_t)(s->st_nlink > 0xffff ? 0xffff : s->st_nlink);
    d->st_ino = s->st_ino;
    d->st_uid = s->st_uid;
    d->st_gid = s->st_gid;
    d->st_rdev = (int32_t)s->st_rdev;
    d->st_atimespec = s->st_atim;
    d->st_mtimespec = s->st_mtim;
    d->st_ctimespec = s->st_ctim;
    d->st_birthtimespec = s->st_ctim;
    d->st_size = s->st_size;
    d->st_blocks = (int64_t)s->st_blocks;
    d->st_blksize = (int32_t)(s->st_blksize ? s->st_blksize : 4096);
}

static int d_stat(const char *p, struct dstat *d)
{
    struct stat s;
    if (stat(p, &s) < 0)
        return -1;
    to_dstat(&s, d);
    return 0;
}

static int d_lstat(const char *p, struct dstat *d)
{
    struct stat s;
    if (lstat(p, &s) < 0)
        return -1;
    to_dstat(&s, d);
    return 0;
}

static int d_fstat(int fd, struct dstat *d)
{
    struct stat s;
    if (fstat(fd, &s) < 0)
        return -1;
    to_dstat(&s, d);
    return 0;
}

static int d_fstatat(int dfd, const char *p, struct dstat *d, int fl)
{
    struct stat s;
    if (fstatat(atfd(dfd), p, &s, atflags(fl)) < 0)
        return -1;
    to_dstat(&s, d);
    return 0;
}

DSYM("_stat$INODE64", d_stat);
DSYM("_lstat$INODE64", d_lstat);
DSYM("_fstat$INODE64", d_fstat);
DSYM("_fstatat$INODE64", d_fstatat);
DSYM("_stat64", d_stat);
DSYM("_lstat64", d_lstat);
DSYM("_fstat64", d_fstat);
DSYM("_fstatat64", d_fstatat);

struct ddirent {
    uint64_t d_ino;
    uint64_t d_seekoff;
    uint16_t d_reclen;
    uint16_t d_namlen;
    uint8_t d_type;
    char d_name[1024];
};

struct ddir {
    int fd;
    DIR *real;
    struct ddirent ent;
};

static struct ddir *dwrap(DIR *real)
{
    if (!real)
        return NULL;
    struct ddir *d = calloc(1, sizeof *d);
    if (!d) {
        closedir(real);
        errno = ENOMEM;
        return NULL;
    }
    d->real = real;
    d->fd = dirfd(real);
    return d;
}

static struct ddir *d_opendir(const char *p) { return dwrap(opendir(p)); }
static struct ddir *d_fdopendir(int fd) { return dwrap(fdopendir(fd)); }

static int d_closedir(struct ddir *d)
{
    int r = closedir(d->real);
    free(d);
    return r;
}

static void fill_dirent(struct ddirent *e, const struct dirent *s, long off)
{
    size_t n = strnlen(s->d_name, sizeof e->d_name - 1);
    e->d_ino = s->d_ino;
    e->d_seekoff = (uint64_t)off;
    e->d_namlen = (uint16_t)n;
    e->d_type = s->d_type;
    memcpy(e->d_name, s->d_name, n);
    e->d_name[n] = 0;
    e->d_reclen = (uint16_t)((offsetof(struct ddirent, d_name) + n + 1 + 7) & ~7u);
}

static struct ddirent *d_readdir(struct ddir *d)
{
    struct dirent *s = readdir(d->real);
    if (!s)
        return NULL;
    fill_dirent(&d->ent, s, telldir(d->real));
    return &d->ent;
}

static int d_readdir_r(struct ddir *d, struct ddirent *e, struct ddirent **res)
{
    int saved = errno;
    errno = 0;
    struct dirent *s = readdir(d->real);
    if (!s) {
        int e2 = errno;
        errno = saved;
        *res = NULL;
        return e2 ? d_errno_lx2d(e2) : 0;
    }
    errno = saved;
    fill_dirent(e, s, telldir(d->real));
    *res = e;
    return 0;
}

static void d_rewinddir(struct ddir *d) { rewinddir(d->real); }
static long d_telldir(struct ddir *d) { return telldir(d->real); }
static void d_seekdir(struct ddir *d, long l) { seekdir(d->real, l); }
static int d_dirfd(struct ddir *d) { return d->fd; }

static int d_alphasort(const struct ddirent **a, const struct ddirent **b)
{
    return strcoll((*a)->d_name, (*b)->d_name);
}

static int d_scandir(const char *p, struct ddirent ***out,
                     int (*sel)(const struct ddirent *),
                     int (*cmp)(const struct ddirent **, const struct ddirent **))
{
    struct ddir *d = d_opendir(p);
    if (!d)
        return -1;
    struct ddirent **v = NULL, *e;
    size_t n = 0, cap = 0;
    while ((e = d_readdir(d))) {
        if (sel && !sel(e))
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            struct ddirent **nv = realloc(v, cap * sizeof *v);
            if (!nv)
                goto fail;
            v = nv;
        }
        v[n] = malloc(e->d_reclen);
        if (!v[n])
            goto fail;
        memcpy(v[n], e, e->d_reclen);
        n++;
    }
    d_closedir(d);
    if (cmp && n)
        qsort(v, n, sizeof *v, (int (*)(const void *, const void *))cmp);
    *out = v;
    return (int)n;
fail:
    while (n)
        free(v[--n]);
    free(v);
    d_closedir(d);
    errno = ENOMEM;
    return -1;
}

DSYM("_opendir$INODE64", d_opendir);
DSYM("_fdopendir$INODE64", d_fdopendir);
DSYM("_closedir", d_closedir);
DSYM("_readdir$INODE64", d_readdir);
DSYM("_readdir_r$INODE64", d_readdir_r);
DSYM("_rewinddir$INODE64", d_rewinddir);
DSYM("_telldir$INODE64", d_telldir);
DSYM("_seekdir$INODE64", d_seekdir);
DSYM("_dirfd", d_dirfd);
DSYM("_alphasort$INODE64", d_alphasort);
DSYM("_scandir$INODE64", d_scandir);

static int d_open(const char *p, int f, ...)
{
    va_list ap;
    va_start(ap, f);
    int mode = (f & DO_CREAT) ? va_arg(ap, int) : 0;
    va_end(ap);
    return open(p, oflags(f), mode);
}

static int d_openat(int dfd, const char *p, int f, ...)
{
    va_list ap;
    va_start(ap, f);
    int mode = (f & DO_CREAT) ? va_arg(ap, int) : 0;
    va_end(ap);
    return openat(atfd(dfd), p, oflags(f), mode);
}

static int d_creat(const char *p, mode_t m) { return open(p, O_WRONLY | O_CREAT | O_TRUNC, m); }

struct dflock {
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
    int16_t l_type;
    int16_t l_whence;
};

static int lock_type(int t, int back)
{
    static const int d[] = { 0, F_RDLCK, F_UNLCK, F_WRLCK };
    if (!back)
        return t >= 1 && t <= 3 ? d[t] : -1;
    return t == F_RDLCK ? 1 : t == F_WRLCK ? 3 : 2;
}

static int d_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);
    switch (cmd) {
    case 0: return fcntl(fd, F_DUPFD, (int)arg);
    case 1: return fcntl(fd, F_GETFD);
    case 2: return fcntl(fd, F_SETFD, (int)arg);
    case 3: {
        int r = fcntl(fd, F_GETFL);
        return r < 0 ? r : oflags_back(r);
    }
    case 4: return fcntl(fd, F_SETFL, oflags((int)arg & ~3));
    case 7: case 8: case 9: {
        struct dflock *dl = (struct dflock *)arg;
        struct flock fl = { 0 };
        fl.l_type = (short)lock_type(dl->l_type, 0);
        fl.l_whence = dl->l_whence;
        fl.l_start = dl->l_start;
        fl.l_len = dl->l_len;
        int r = fcntl(fd, cmd == 7 ? F_GETLK : cmd == 8 ? F_SETLK : F_SETLKW, &fl);
        if (r == 0 && cmd == 7) {
            dl->l_type = (int16_t)lock_type(fl.l_type, 1);
            dl->l_whence = fl.l_whence;
            dl->l_start = fl.l_start;
            dl->l_len = fl.l_len;
            dl->l_pid = fl.l_pid;
        }
        return r;
    }
    case 48: case 51: case 85: case 73: case 74:
        return cmd == 51 || cmd == 85 ? fsync(fd) : 0;
    case 50: {
        char link[64];
        snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
        ssize_t n = readlink(link, (char *)arg, PATH_MAX - 1);
        if (n < 0)
            return -1;
        ((char *)arg)[n] = 0;
        return 0;
    }
    case 67: return fcntl(fd, F_DUPFD_CLOEXEC, (int)arg);
    }
    errno = EINVAL;
    return -1;
}

static int d_ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    switch (req) {
    case 0x40087468: return ioctl(fd, TIOCGWINSZ, arg);
    case 0x80087467: return ioctl(fd, TIOCSWINSZ, arg);
    case 0x4004667f: return ioctl(fd, FIONREAD, arg);
    case 0x8004667e: return ioctl(fd, FIONBIO, arg);
    case 0x40047477: return ioctl(fd, 0x540F, arg);
    case 0x80047476: return ioctl(fd, 0x5410, arg);
    case 0x20007461: return ioctl(fd, 0x540E, arg);
    case 0x20006601: return fcntl(fd, F_SETFD, FD_CLOEXEC);
    case 0x20006602: return fcntl(fd, F_SETFD, 0);
    }
    int handled, r = d_term_ioctl(fd, req, arg, &handled);
    if (handled)
        return r;
    errno = ENOTTY;
    return -1;
}

static int d_faccessat(int dfd, const char *p, int m, int f) { return faccessat(atfd(dfd), p, m, atflags(f)); }
static int d_fchmodat(int dfd, const char *p, mode_t m, int f) { return fchmodat(atfd(dfd), p, m, atflags(f)); }
static int d_fchownat(int dfd, const char *p, uid_t u, gid_t g, int f) { return fchownat(atfd(dfd), p, u, g, atflags(f)); }
static int d_unlinkat(int dfd, const char *p, int f) { return unlinkat(atfd(dfd), p, atflags(f)); }
static int d_mkdirat(int dfd, const char *p, mode_t m) { return mkdirat(atfd(dfd), p, m); }
static ssize_t d_readlinkat(int dfd, const char *p, char *b, size_t n) { return readlinkat(atfd(dfd), p, b, n); }
static int d_symlinkat(const char *t, int dfd, const char *p) { return symlinkat(t, atfd(dfd), p); }
static int d_renameat(int a, const char *pa, int b, const char *pb) { return renameat(atfd(a), pa, atfd(b), pb); }
static int d_linkat(int a, const char *pa, int b, const char *pb, int f) { return linkat(atfd(a), pa, atfd(b), pb, atflags(f)); }

DSYM("_open", d_open);
DSYM("_openat", d_openat);
DSYM("_creat", d_creat);
DSYM("_fcntl", d_fcntl);
DSYM("_ioctl", d_ioctl);
DSYM("_faccessat", d_faccessat);
DSYM("_fchmodat", d_fchmodat);
DSYM("_fchownat", d_fchownat);
DSYM("_unlinkat", d_unlinkat);
DSYM("_mkdirat", d_mkdirat);
DSYM("_readlinkat", d_readlinkat);
DSYM("_symlinkat", d_symlinkat);
DSYM("_renameat", d_renameat);
DSYM("_linkat", d_linkat);

struct dtimeval {
    long tv_sec;
    int tv_usec;
};

static int utimes_common(int fd, const char *p, const struct dtimeval *tv, int fl)
{
    struct timespec ts[2], *tp = NULL;
    if (tv) {
        for (int i = 0; i < 2; i++) {
            ts[i].tv_sec = tv[i].tv_sec;
            ts[i].tv_nsec = (long)tv[i].tv_usec * 1000;
        }
        tp = ts;
    }
    if (p)
        return utimensat(AT_FDCWD, p, tp, fl);
    return futimens(fd, tp);
}

static int d_utimes(const char *p, const struct dtimeval *tv) { return utimes_common(-1, p, tv, 0); }
static int d_lutimes(const char *p, const struct dtimeval *tv) { return utimes_common(-1, p, tv, AT_SYMLINK_NOFOLLOW); }
static int d_futimes(int fd, const struct dtimeval *tv) { return utimes_common(fd, NULL, tv, 0); }

static void conv_ts(const struct timespec *in, struct timespec *out)
{
    for (int i = 0; i < 2; i++) {
        out[i] = in[i];
        if (in[i].tv_nsec == -1)
            out[i].tv_nsec = UTIME_NOW;
        else if (in[i].tv_nsec == -2)
            out[i].tv_nsec = UTIME_OMIT;
    }
}

static int d_utimensat(int dfd, const char *p, const struct timespec *t, int f)
{
    struct timespec ts[2];
    if (t)
        conv_ts(t, ts);
    return utimensat(atfd(dfd), p, t ? ts : NULL, atflags(f));
}

static int d_futimens(int fd, const struct timespec *t)
{
    struct timespec ts[2];
    if (t)
        conv_ts(t, ts);
    return futimens(fd, t ? ts : NULL);
}

DSYM("_utimes", d_utimes);
DSYM("_lutimes", d_lutimes);
DSYM("_futimes", d_futimes);
DSYM("_utimensat", d_utimensat);
DSYM("_futimens", d_futimens);

static void *d_mmap(void *a, size_t len, int prot, int fl, int fd, off_t off)
{
    int f = fl & (MAP_SHARED | MAP_PRIVATE | MAP_FIXED);
    if (fl & 0x1000)
        f |= MAP_ANONYMOUS;
    if (fl & 0x40)
        f |= MAP_NORESERVE;
    return mmap(a, len, prot, f, (fl & 0x1000) ? -1 : fd, off);
}
DSYM("_mmap", d_mmap);

DPASS(munmap);
DPASS(mprotect);
DPASS(msync);
DPASS(madvise);
DPASS(read);
DPASS(write);
DPASS(pread);
DPASS(pwrite);
DPASS(readv);
DPASS(writev);
DPASS(close);
DPASS(lseek);
DPASS(dup);
DPASS(dup2);
DPASS(pipe);
DPASS(fsync);
DPASS(ftruncate);
DPASS(truncate);
DPASS(access);
DPASS(chdir);
DPASS(fchdir);
DPASS(getcwd);
DPASS(mkdir);
DPASS(rmdir);
DPASS(unlink);
DPASS(link);
DPASS(symlink);
DPASS(readlink);
DPASS(realpath);
DPASS(chmod);
DPASS(fchmod);
DPASS(chown);
DPASS(fchown);
DPASS(lchown);
DPASS(umask);
DPASS(mkfifo);
DPASS(mknod);
DPASS(isatty);
DPASS(ttyname);
DPASS(mkstemp);
DPASS(mkstemps);
DPASS(mkdtemp);
DPASS(mktemp);
DPASS(flock);

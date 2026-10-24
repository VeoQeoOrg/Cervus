#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/futex.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include "darwin.h"

#define ULF_WAKE_ALL  0x100
#define ULF_NO_ERRNO  0x1000000

static int ulock_ret(int e, uint32_t op)
{
    if (e == EAGAIN || e == EWOULDBLOCK)
        return 0;
    if (op & ULF_NO_ERRNO)
        return -d_errno_lx2d(e);
    errno = e;
    return -1;
}

static int ulock_wait(uint32_t op, void *addr, uint64_t value, uint64_t ns)
{
    uint32_t cmd = op & 0xff;
    uint32_t expect = (uint32_t)value;
    if (cmd == 5 || cmd == 6) {
        uint64_t cur;
        memcpy(&cur, addr, 8);
        if (cur != value)
            return 0;
        memcpy(&expect, addr, 4);
    } else if (cmd == 2 || cmd == 4) {
        uint32_t cur = __atomic_load_n((uint32_t *)addr, __ATOMIC_ACQUIRE);
        if (cur != expect)
            return 0;
    }
    if (futex_wait((volatile uint32_t *)addr, expect, ns) < 0)
        return ulock_ret(errno, op);
    return 0;
}

static int d_ulock_wait(uint32_t op, void *addr, uint64_t value, uint32_t us)
{
    return ulock_wait(op, addr, value, (uint64_t)us * 1000);
}

static int d_ulock_wait2(uint32_t op, void *addr, uint64_t value, uint64_t ns, uint64_t value2)
{
    (void)value2;
    return ulock_wait(op, addr, value, ns);
}

static int d_ulock_wake(uint32_t op, void *addr, uint64_t wake)
{
    (void)wake;
    futex_wake((volatile uint32_t *)addr, (op & (ULF_WAKE_ALL | 0x200)) ? INT_MAX : 1);
    return 0;
}

DSYM("___ulock_wait", d_ulock_wait);
DSYM("___ulock_wait2", d_ulock_wait2);
DSYM("___ulock_wake", d_ulock_wake);

struct kdirent {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[256];
};

static uint8_t kdt(uint8_t t)
{
    static const uint8_t m[] = { DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_LNK, DT_FIFO, DT_SOCK };
    return t < sizeof m ? m[t] : DT_UNKNOWN;
}

static ssize_t d_getdirentries64(int fd, char *buf, size_t n, int64_t *basep)
{
    size_t used = 0;
    const size_t maxrec = (21 + 256 + 1 + 7) & ~7ul;
    if (basep)
        *basep = lseek(fd, 0, SEEK_CUR);
    while (n - used >= maxrec) {
        struct kdirent k;
        long r = syscall2(SYS_READDIR, fd, &k);
        if (r < 0) {
            if (!used) {
                errno = (int)-r;
                return -1;
            }
            break;
        }
        if (r != 0)
            break;
        size_t nl = strnlen(k.d_name, 255);
        size_t rec = (21 + nl + 1 + 7) & ~7ul;
        char *e = buf + used;
        uint64_t seek = (uint64_t)lseek(fd, 0, SEEK_CUR);
        uint16_t reclen = (uint16_t)rec, namlen = (uint16_t)nl;
        memset(e, 0, rec);
        memcpy(e, &k.d_ino, 8);
        memcpy(e + 8, &seek, 8);
        memcpy(e + 16, &reclen, 2);
        memcpy(e + 18, &namlen, 2);
        e[20] = (char)kdt(k.d_type);
        memcpy(e + 21, k.d_name, nl);
        used += rec;
    }
    return (ssize_t)used;
}
DSYM("___getdirentries64", d_getdirentries64);

static int copy_data(int from, int to)
{
    char *b = malloc(1 << 16);
    if (!b) {
        errno = ENOMEM;
        return -1;
    }
    off_t off = 0;
    for (;;) {
        ssize_t r = pread(from, b, 1 << 16, off);
        if (r < 0) {
            free(b);
            return -1;
        }
        if (!r)
            break;
        for (ssize_t w = 0; w < r;) {
            ssize_t k = write(to, b + w, (size_t)(r - w));
            if (k < 0) {
                free(b);
                return -1;
            }
            w += k;
        }
        off += r;
    }
    free(b);
    return 0;
}

#define COPYFILE_ACL   (1 << 0)
#define COPYFILE_STAT  (1 << 1)
#define COPYFILE_XATTR (1 << 2)
#define COPYFILE_DATA  (1 << 3)

static int d_fcopyfile(int from, int to, void *state, uint32_t flags)
{
    (void)state;
    if ((flags & COPYFILE_DATA) && copy_data(from, to) < 0)
        return -1;
    if (flags & COPYFILE_STAT) {
        struct stat st;
        if (fstat(from, &st) == 0)
            fchmod(to, st.st_mode & 07777);
    }
    return 0;
}

static int d_copyfile(const char *from, const char *to, void *state, uint32_t flags)
{
    int a = open(from, O_RDONLY | O_CLOEXEC);
    if (a < 0)
        return -1;
    struct stat st;
    fstat(a, &st);
    int b = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st.st_mode & 07777);
    if (b < 0) {
        close(a);
        return -1;
    }
    int r = d_fcopyfile(a, b, state, flags ? flags : COPYFILE_DATA | COPYFILE_STAT);
    close(a);
    close(b);
    return r;
}

static void *d_copyfile_state_alloc(void) { return calloc(1, 16); }
static int d_copyfile_state_free(void *s) { free(s); return 0; }

DSYM("_fcopyfile", d_fcopyfile);
DSYM("_copyfile", d_copyfile);
DSYM("_copyfile_state_alloc", d_copyfile_state_alloc);
DSYM("_copyfile_state_free", d_copyfile_state_free);

struct dsf_hdtr {
    struct iovec *headers;
    int hdr_cnt;
    struct iovec *trailers;
    int trl_cnt;
};

static int d_sendfile(int fd, int s, off_t off, off_t *len, struct dsf_hdtr *hd, int flags)
{
    (void)flags;
    off_t want = len ? *len : 0, sent = 0;
    char *b = malloc(1 << 16);
    if (!b) {
        errno = ENOMEM;
        return -1;
    }
    if (hd && hd->hdr_cnt > 0) {
        ssize_t w = writev(s, hd->headers, hd->hdr_cnt);
        if (w < 0)
            goto fail;
        sent += w;
    }
    while (!want || sent < want) {
        size_t chunk = 1 << 16;
        if (want && (off_t)chunk > want - sent)
            chunk = (size_t)(want - sent);
        ssize_t r = pread(fd, b, chunk, off);
        if (r < 0)
            goto fail;
        if (!r)
            break;
        ssize_t w = write(s, b, (size_t)r);
        if (w < 0)
            goto fail;
        sent += w;
        off += w;
        if (w < r)
            break;
    }
    if (hd && hd->trl_cnt > 0) {
        ssize_t w = writev(s, hd->trailers, hd->trl_cnt);
        if (w > 0)
            sent += w;
    }
    free(b);
    if (len)
        *len = sent;
    return 0;
fail:
    free(b);
    if (len)
        *len = sent;
    return -1;
}
DSYM("_sendfile", d_sendfile);

static unsigned __int128 udivmod128(unsigned __int128 n, unsigned __int128 d, unsigned __int128 *rem)
{
    unsigned __int128 q = 0, r = 0;
    if (!d) {
        if (rem)
            *rem = 0;
        return 0;
    }
    if ((uint64_t)(d >> 64) == 0 && (uint64_t)(n >> 64) == 0) {
        uint64_t a = (uint64_t)n, b = (uint64_t)d;
        if (rem)
            *rem = a % b;
        return a / b;
    }
    for (int i = 127; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (unsigned __int128)1 << i;
        }
    }
    if (rem)
        *rem = r;
    return q;
}

static unsigned __int128 d_udivti3(unsigned __int128 a, unsigned __int128 b) { return udivmod128(a, b, NULL); }

static unsigned __int128 d_umodti3(unsigned __int128 a, unsigned __int128 b)
{
    unsigned __int128 r;
    udivmod128(a, b, &r);
    return r;
}

static unsigned __int128 d_udivmodti4(unsigned __int128 a, unsigned __int128 b, unsigned __int128 *r)
{
    return udivmod128(a, b, r);
}

static __int128 d_divti3(__int128 a, __int128 b)
{
    int neg = (a < 0) != (b < 0);
    unsigned __int128 ua = a < 0 ? -(unsigned __int128)a : (unsigned __int128)a;
    unsigned __int128 ub = b < 0 ? -(unsigned __int128)b : (unsigned __int128)b;
    unsigned __int128 q = udivmod128(ua, ub, NULL);
    return neg ? -(__int128)q : (__int128)q;
}

static __int128 d_modti3(__int128 a, __int128 b)
{
    unsigned __int128 ua = a < 0 ? -(unsigned __int128)a : (unsigned __int128)a;
    unsigned __int128 ub = b < 0 ? -(unsigned __int128)b : (unsigned __int128)b;
    unsigned __int128 r;
    udivmod128(ua, ub, &r);
    return a < 0 ? -(__int128)r : (__int128)r;
}

DSYM("___udivti3", d_udivti3);
DSYM("___umodti3", d_umodti3);
DSYM("___udivmodti4", d_udivmodti4);
DSYM("___divti3", d_divti3);
DSYM("___modti3", d_modti3);

static int d_issetugid(void) { return 0; }
DSYM("_issetugid", d_issetugid);
DPASS(setreuid);
DPASS(setregid);

static int rlim_res(int r)
{
    static const int m[] = { RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE,
                             RLIMIT_AS, RLIMIT_MEMLOCK, RLIMIT_NPROC, RLIMIT_NOFILE };
    return r >= 0 && r < (int)(sizeof m / sizeof m[0]) ? m[r] : -1;
}

static int d_getrlimit(int r, struct rlimit *rl)
{
    int l = rlim_res(r);
    if (l < 0) {
        errno = EINVAL;
        return -1;
    }
    if (getrlimit(l, rl) < 0)
        return -1;
    if (rl->rlim_cur == RLIM_INFINITY)
        rl->rlim_cur = 0x7fffffffffffffffull;
    if (rl->rlim_max == RLIM_INFINITY)
        rl->rlim_max = 0x7fffffffffffffffull;
    return 0;
}

static int d_setrlimit(int r, const struct rlimit *rl)
{
    int l = rlim_res(r);
    if (l < 0) {
        errno = EINVAL;
        return -1;
    }
    struct rlimit v = *rl;
    if (v.rlim_cur >= 0x7fffffffffffffffull)
        v.rlim_cur = RLIM_INFINITY;
    if (v.rlim_max >= 0x7fffffffffffffffull)
        v.rlim_max = RLIM_INFINITY;
    return setrlimit(l, &v);
}

DSYM("_getrlimit", d_getrlimit);
DSYM("_setrlimit", d_setrlimit);

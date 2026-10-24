#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/sched/sched.h"
#include "../../../include/sched/ptrace.h"
#include "../../../include/signal/signal.h"
#include "../../../include/drivers/random.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/memory/mm.h"
#include "../../../include/smp/smp.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/io/serial.h"
#include "../../../include/net/netdev.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

typedef int64_t (*fb_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

#define FB_NR_MAX 608

#define FB_AT_FDCWD            (-100)
#define FB_AT_EACCESS          0x0100
#define FB_AT_SYMLINK_NOFOLLOW 0x0200
#define FB_AT_SYMLINK_FOLLOW   0x0400
#define FB_AT_REMOVEDIR        0x0800
#define FB_AT_EMPTY_PATH       0x4000

#define LX_AT_SYMLINK_NOFOLLOW 0x100
#define LX_AT_REMOVEDIR        0x200
#define LX_AT_SYMLINK_FOLLOW   0x400
#define LX_AT_EACCESS          0x200
#define LX_AT_EMPTY_PATH       0x1000

#define FB_SIGCODE_VA 0x00007FFFFFFFE000ULL
#define FB_OSRELDATE  1501000

extern int64_t sys_exit_group(uint64_t);
extern int64_t sys_getpid(void);
extern int64_t sys_getppid(void);
extern int64_t sys_getuid(void);
extern int64_t sys_getgid(void);
extern int64_t sys_setuid(uint64_t);
extern int64_t sys_setgid(uint64_t);
extern int64_t sys_fork(void);
extern int64_t sys_execve(uint64_t, uint64_t, uint64_t);
extern int64_t sys_execveat(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_yield(void);
extern int64_t sys_read(uint64_t, uint64_t, uint64_t);
extern int64_t sys_write(uint64_t, uint64_t, uint64_t);
extern int64_t sys_close(uint64_t);
extern int64_t sys_close_range(uint64_t, uint64_t, uint64_t);
extern int64_t sys_seek(uint64_t, uint64_t, uint64_t);
extern int64_t sys_ioctl(uint64_t, uint64_t, uint64_t);
extern int64_t sys_dup(uint64_t);
extern int64_t sys_dup2(uint64_t, uint64_t);
extern int64_t sys_pipe(uint64_t);
extern int64_t sys_fcntl(uint64_t, uint64_t, uint64_t);
extern int64_t sys_brk(uint64_t);
extern int64_t sys_mmap(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_munmap(uint64_t, uint64_t);
extern int64_t sys_mprotect(uint64_t, uint64_t, uint64_t);
extern int64_t sys_madvise(uint64_t, uint64_t, uint64_t);
extern int64_t sys_msync(uint64_t, uint64_t, uint64_t);
extern int64_t sys_mincore(uint64_t, uint64_t, uint64_t);
extern int64_t sys_mlock(uint64_t, uint64_t);
extern int64_t sys_munlock(uint64_t, uint64_t);
extern int64_t sys_mlockall(uint64_t);
extern int64_t sys_munlockall(void);
extern int64_t sys_clock_get(uint64_t, uint64_t);
extern int64_t sys_sleep_ns(uint64_t);
extern int64_t sys_unlink(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_rmdir(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_mkdir(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_rename(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_sync(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_chdir(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_getcwd(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_truncate(uint64_t, uint64_t);
extern int64_t sys_ftruncate(uint64_t, uint64_t);
extern int64_t sys_fsync(uint64_t);
extern int64_t sys_fdatasync(uint64_t);
extern int64_t sys_getpgid(uint64_t);
extern int64_t sys_setpgid(uint64_t, uint64_t);
extern int64_t sys_getsid(uint64_t);
extern int64_t sys_setsid(void);
extern int64_t sys_symlink(uint64_t, uint64_t);
extern int64_t sys_chmod(uint64_t, uint64_t);
extern int64_t sys_chown(uint64_t, uint64_t, uint64_t);
extern int64_t sys_fchmod(uint64_t, uint64_t);
extern int64_t sys_fchown(uint64_t, uint64_t, uint64_t);
extern int64_t sys_lchown(uint64_t, uint64_t, uint64_t);
extern int64_t sys_umask(uint64_t);
extern int64_t sys_pread(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_pwrite(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_readv(uint64_t, uint64_t, uint64_t);
extern int64_t sys_writev(uint64_t, uint64_t, uint64_t);
extern int64_t sys_preadv(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_pwritev(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_readlink(uint64_t, uint64_t, uint64_t);
extern int64_t sys_link(uint64_t, uint64_t);
extern int64_t sys_openat(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_mkdirat(uint64_t, uint64_t, uint64_t);
extern int64_t sys_mknodat(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_unlinkat(uint64_t, uint64_t, uint64_t);
extern int64_t sys_renameat(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_linkat(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_symlinkat(uint64_t, uint64_t, uint64_t);
extern int64_t sys_readlinkat(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_fchmodat(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_fchownat(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_fchdir(uint64_t);
extern int64_t sys_chroot(uint64_t);
extern int64_t sys_kill(uint64_t, uint64_t);
extern int64_t sys_tkill(uint64_t, uint64_t);
extern int64_t sys_tgkill(uint64_t, uint64_t, uint64_t);
extern int64_t sys_rt_sigaction(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_rt_sigprocmask(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_rt_sigpending(uint64_t, uint64_t);
extern int64_t sys_rt_sigsuspend(uint64_t, uint64_t);
extern int64_t sys_sigaltstack(uint64_t, uint64_t);
extern int64_t sys_sigqueue(uint64_t, uint64_t, uint64_t);
extern int64_t sys_sigtimedwait(uint64_t, uint64_t, uint64_t);
extern int64_t sys_wait4(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_waitid(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_getrusage(uint64_t, uint64_t);
extern int64_t sys_getpriority(uint64_t, uint64_t);
extern int64_t sys_setpriority(uint64_t, uint64_t, uint64_t);
extern int64_t sys_prlimit(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_gettid(void);
extern int64_t sys_setitimer(uint64_t, uint64_t, uint64_t);
extern int64_t sys_getitimer(uint64_t, uint64_t);
extern int64_t sys_socket(uint64_t, uint64_t, uint64_t);
extern int64_t sys_bind(uint64_t, uint64_t, uint64_t);
extern int64_t sys_connect(uint64_t, uint64_t, uint64_t);
extern int64_t sys_listen(uint64_t, uint64_t);
extern int64_t sys_accept4(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_sendto_flags(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_recvfrom_flags(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_sock_shutdown(uint64_t, uint64_t);
extern int64_t sys_sendmsg(uint64_t, uint64_t, uint64_t);
extern int64_t sys_recvmsg(uint64_t, uint64_t, uint64_t);
extern int64_t sys_getsockname(uint64_t, uint64_t, uint64_t);
extern int64_t sys_getpeername(uint64_t, uint64_t, uint64_t);
extern int64_t sys_socketpair(uint64_t, uint64_t, uint64_t);
extern int64_t sys_setsockopt(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_getsockopt(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_poll(uint64_t, uint64_t, uint64_t);
extern int64_t sys_select(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_ppoll(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_pselect(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_eventfd(uint64_t, uint64_t, uint64_t);
extern int64_t sys_arch_prctl(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t clock_realtime_offset_ns(void);
extern int syscall_resolve_at(task_t *t, int64_t dirfd, uint64_t upath, char *out, size_t cap);
extern task_t *fork_process(task_t *parent, int share_vm, uintptr_t user_sp, int64_t *err);
extern int64_t freebsd_sigreturn(uint64_t ucp);
extern int64_t freebsd_getcontext(uint64_t ucp);
extern void task_start_clone(task_t *t);
extern task_t *task_spawn_clone(task_t *parent, uintptr_t stack_top, uint64_t fs_base);
extern int64_t sys_thread_exit(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_futex(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t futex_wait_deadline(uint64_t uaddr, uint32_t expected, uint64_t deadline);
extern int futex_user_load(uint64_t uaddr, uint32_t *out);
extern int futex_user_cas(uint64_t uaddr, uint32_t *expected, uint32_t desired);

#define A6 uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6
#define UNUSED6 (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6

#define FB0(name, expr) static int64_t name(A6) { UNUSED6; return (expr); }
#define I(x) ((uint64_t)(int64_t)(int32_t)(x))

static const uint8_t g_errno_l2b[134] = {
    [1] = 1, [2] = 2, [3] = 3, [4] = 4, [5] = 5, [6] = 6, [7] = 7, [8] = 8, [9] = 9, [10] = 10,
    [11] = 35, [12] = 12, [13] = 13, [14] = 14, [15] = 15, [16] = 16, [17] = 17, [18] = 18,
    [19] = 19, [20] = 20, [21] = 21, [22] = 22, [23] = 23, [24] = 24, [25] = 25, [26] = 26,
    [27] = 27, [28] = 28, [29] = 29, [30] = 30, [31] = 31, [32] = 32, [33] = 33, [34] = 34,
    [35] = 11, [36] = 63, [37] = 77, [38] = 78, [39] = 66, [40] = 62, [42] = 83, [43] = 82,
    [61] = 87, [62] = 60, [66] = 71, [67] = 91, [71] = 92, [72] = 90, [74] = 89, [75] = 84,
    [84] = 86, [87] = 68, [88] = 38, [89] = 39, [90] = 40, [91] = 41, [92] = 42, [93] = 43,
    [94] = 44, [95] = 45, [96] = 46, [97] = 47, [98] = 48, [99] = 49, [100] = 50, [101] = 51,
    [102] = 52, [103] = 53, [104] = 54, [105] = 55, [106] = 56, [107] = 57, [108] = 58,
    [109] = 59, [110] = 60, [111] = 61, [112] = 64, [113] = 65, [114] = 37, [115] = 36,
    [116] = 70, [122] = 69, [125] = 85, [130] = 96, [131] = 95,
};

int64_t fbsd_errno_from_linux(int64_t e)
{
    if (e > 0 && e < (int64_t)sizeof g_errno_l2b && g_errno_l2b[e]) return g_errno_l2b[e];
    return 22;
}

static const uint8_t g_sig_l2b[64] = {
    0, 1, 2, 3, 4, 5, 6, 10, 8, 9, 30, 11, 31, 13, 14, 15,
    7, 20, 19, 17, 18, 21, 22, 16, 24, 25, 26, 27, 28, 23, 29, 12,
    32, 33, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78,
    79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 0,
};

int fbsd_sig_from_linux(int sig)
{
    if (sig <= 0 || sig >= 64) return sig;
    return g_sig_l2b[sig];
}

static int fb_sig_to_linux(int bsig)
{
    if (bsig == 0) return 0;
    for (int i = 1; i < 64; i++)
        if (g_sig_l2b[i] == bsig) return i;
    return -1;
}

void fbsd_sigset_from_linux(uint64_t mask, uint32_t out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    for (int s = 1; s < 64; s++) {
        if (!(mask & (1ULL << s))) continue;
        int b = g_sig_l2b[s];
        if (b > 0 && b <= 128) out[(b - 1) / 32] |= 1u << ((b - 1) % 32);
    }
}

uint64_t fbsd_sigset_to_linux(const uint32_t in[4])
{
    uint64_t m = 0;
    for (int b = 1; b <= 128; b++) {
        if (!(in[(b - 1) / 32] & (1u << ((b - 1) % 32)))) continue;
        int s = fb_sig_to_linux(b);
        if (s > 0) m |= 1ULL << s;
    }
    return m;
}

void fbsd_fill_siginfo(uint8_t out[80], int bsig, const ksiginfo_t *si)
{
    memset(out, 0, 80);
    int32_t v[6] = { bsig, 0, si->code, (int32_t)si->pid, (int32_t)si->uid, si->status };
    if (si->code == SI_USER_CODE) v[2] = 0x10001;
    else if (si->code == SI_QUEUE_CODE) v[2] = 0x10002;
    else if (si->code == SI_KERNEL_CODE) v[2] = 0x10006;
    memcpy(out, v, sizeof v);
    memcpy(out + 24, &si->addr, 8);
    memcpy(out + 32, &si->value, 8);
    int32_t trapno = si->trapno;
    memcpy(out + 40, &trapno, 4);
}

int64_t fbsd_syscall_return(task_t *t, int64_t ret)
{
    if (ret < 0 && ret >= -4095) {
        t->user_saved_r11 |= 1;
        return fbsd_errno_from_linux(-ret);
    }
    t->user_saved_r11 &= ~1ULL;
    return ret;
}

bool fbsd_syscall_is_sigreturn(uint64_t nr)
{
    return nr == 417 || nr == 422;
}

bool fbsd_syscall_restartable(uint64_t nr)
{
    switch (nr) {
    case 3: case 4: case 5: case 7: case 27: case 28: case 29: case 30: case 54: case 92:
    case 98: case 120: case 121: case 133: case 475: case 476: case 289: case 290:
    case 499: case 541: case 532:
        return true;
    }
    return false;
}

int fbsd_map_sigcode(mm_t *mm)
{
    static const uint8_t code[] = {
        0xff, 0x14, 0x24,
        0x48, 0x8d, 0x7c, 0x24, 0x10,
        0x6a, 0x00,
        0x48, 0xc7, 0xc0, 0xa1, 0x01, 0x00, 0x00,
        0x0f, 0x05,
        0xf4,
        0xeb, 0xfd,
    };
    int64_t r = mm_map_anon(mm, FB_SIGCODE_VA, 4096, VM_READ | VM_WRITE, true);
    if (r < 0) return (int)r;
    if (mm_copy_to(mm, FB_SIGCODE_VA, code, sizeof code) < 0) return -EFAULT;
    return mm_protect(mm, FB_SIGCODE_VA, 4096, VM_READ | VM_EXEC);
}

static uint64_t fb_scratch(task_t *t, size_t len)
{
    return (t->user_rsp - 512 - len) & ~(uint64_t)63;
}

static int64_t fb_atflags(uint64_t f)
{
    int64_t o = 0;
    if (f & FB_AT_SYMLINK_NOFOLLOW) o |= LX_AT_SYMLINK_NOFOLLOW;
    if (f & FB_AT_SYMLINK_FOLLOW)   o |= LX_AT_SYMLINK_FOLLOW;
    if (f & FB_AT_REMOVEDIR)        o |= LX_AT_REMOVEDIR;
    if (f & FB_AT_EMPTY_PATH)       o |= LX_AT_EMPTY_PATH;
    return o;
}

static uint64_t fb_oflags(uint64_t f)
{
    uint64_t o = f & 3;
    if (f & 0x0004)     o |= O_NONBLOCK;
    if (f & 0x0008)     o |= O_APPEND;
    if (f & 0x0040)     o |= 0x2000;
    if (f & 0x0080)     o |= O_SYNC;
    if (f & 0x0100)     o |= O_NOFOLLOW;
    if (f & 0x0200)     o |= O_CREAT;
    if (f & 0x0400)     o |= O_TRUNC;
    if (f & 0x0800)     o |= O_EXCL;
    if (f & 0x8000)     o |= 0x100;
    if (f & 0x10000)    o |= 0x4000;
    if (f & 0x20000)    o |= O_DIRECTORY;
    if (f & 0x100000)   o |= O_CLOEXEC;
    if (f & 0x400000)   o |= 0x200000;
    if (f & 0x1000000)  o |= O_DSYNC;
    return o;
}

static uint64_t fb_oflags_out(uint64_t o)
{
    uint64_t f = o & 3;
    if (o & O_NONBLOCK) f |= 0x0004;
    if (o & O_APPEND)   f |= 0x0008;
    if (o & 0x2000)     f |= 0x0040;
    if ((o & O_SYNC) == O_SYNC) f |= 0x0080;
    else if (o & O_DSYNC) f |= 0x1000000;
    if (o & 0x4000)     f |= 0x10000;
    return f;
}

static int fb_fd_stat(uint64_t fd, vfs_stat_t *st)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    int r = vfs_fstat(f, st);
    fd_put(f);
    return r;
}

static int fb_path_stat(int64_t dirfd, uint64_t path, int nofollow, int empty_ok, vfs_stat_t *st)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (empty_ok) {
        char first = 1;
        if (!path || (syscall_copy_from_user(&first, (const void *)path, 1) == 0 && first == 0)) {
            if ((int)dirfd != FB_AT_FDCWD) return fb_fd_stat((uint64_t)dirfd, st);
            return vfs_stat(t->cwd[0] ? t->cwd : "/", st);
        }
    }
    char kp[VFS_MAX_PATH];
    int r = syscall_resolve_at(t, dirfd, path, kp, sizeof kp);
    if (r < 0) return r;
    return nofollow ? vfs_lstat(kp, st) : vfs_stat(kp, st);
}

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint16_t st_mode;
    int16_t  st_bsdflags;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t  st_padding1;
    uint64_t st_rdev;
    int64_t  st_atim[2];
    int64_t  st_mtim[2];
    int64_t  st_ctim[2];
    int64_t  st_birthtim[2];
    int64_t  st_size;
    int64_t  st_blocks;
    int32_t  st_blksize;
    uint32_t st_flags;
    uint64_t st_gen;
    uint64_t st_filerev;
    uint64_t st_spare[9];
} fb_stat_t;

_Static_assert(sizeof(fb_stat_t) == 224, "FreeBSD struct stat is 224 bytes");

static uint32_t fb_type_bits(vnode_type_t t)
{
    switch (t) {
    case VFS_NODE_DIR:     return 0040000;
    case VFS_NODE_CHARDEV: return 0020000;
    case VFS_NODE_BLKDEV:  return 0060000;
    case VFS_NODE_SYMLINK: return 0120000;
    case VFS_NODE_PIPE:    return 0010000;
    case VFS_NODE_SOCKET:  return 0140000;
    default:               return 0100000;
    }
}

static uint8_t fb_dtype(uint8_t t)
{
    switch (t) {
    case VFS_NODE_DIR:     return 4;
    case VFS_NODE_CHARDEV: return 2;
    case VFS_NODE_BLKDEV:  return 6;
    case VFS_NODE_SYMLINK: return 10;
    case VFS_NODE_PIPE:    return 1;
    case VFS_NODE_SOCKET:  return 12;
    default:               return 8;
    }
}

static int64_t fb_put_stat(uint64_t uptr, const vfs_stat_t *s)
{
    fb_stat_t o;
    memset(&o, 0, sizeof o);
    o.st_dev     = s->st_dev;
    o.st_ino     = s->st_ino;
    o.st_nlink   = s->st_nlink ? s->st_nlink : 1;
    o.st_mode    = (uint16_t)((s->st_mode & 07777) | fb_type_bits(s->st_type));
    o.st_uid     = s->st_uid;
    o.st_gid     = s->st_gid;
    o.st_rdev    = s->st_rdev;
    o.st_atim[0] = s->st_atime;
    o.st_mtim[0] = s->st_mtime;
    o.st_ctim[0] = s->st_ctime;
    o.st_birthtim[0] = s->st_ctime;
    o.st_size    = (int64_t)s->st_size;
    o.st_blocks  = (int64_t)s->st_blocks;
    o.st_blksize = s->st_blksize ? (int32_t)s->st_blksize : 4096;
    return syscall_copy_to_user((void *)uptr, &o, sizeof o);
}

static int64_t fb_fstat(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = fb_fd_stat(a1, &st);
    if (r < 0) return r;
    return fb_put_stat(a2, &st);
}

static int64_t fb_fstatat(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = fb_path_stat((int64_t)(int32_t)a1, a2, (a4 & FB_AT_SYMLINK_NOFOLLOW) != 0, (a4 & FB_AT_EMPTY_PATH) != 0, &st);
    if (r < 0) return r;
    return fb_put_stat(a3, &st);
}

static int64_t fb_faccessat(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = fb_path_stat((int64_t)(int32_t)a1, a2, (a4 & FB_AT_SYMLINK_NOFOLLOW) != 0, (a4 & FB_AT_EMPTY_PATH) != 0, &st);
    if (r < 0) return r;
    uint64_t mode = a3 & 7;
    if (!mode) return 0;
    task_t *t = syscall_cur_task();
    uint32_t uid = t ? t->uid : 0;
    if (uid == 0) {
        if ((mode & 1) && st.st_type != VFS_NODE_DIR && !(st.st_mode & 0111)) return -EACCES;
        return 0;
    }
    uint32_t bits;
    if (uid == st.st_uid) bits = (st.st_mode >> 6) & 7;
    else if (t && t->gid == st.st_gid) bits = (st.st_mode >> 3) & 7;
    else bits = st.st_mode & 7;
    return (bits & mode) == mode ? 0 : -EACCES;
}

FB0(fb_access, fb_faccessat((uint64_t)(int64_t)FB_AT_FDCWD, a1, a2, 0, 0, 0))

static int64_t fb_openat(A6)
{
    UNUSED6;
    return sys_openat(I(a1), a2, fb_oflags(a3), a4);
}

FB0(fb_open, fb_openat((uint64_t)(int64_t)FB_AT_FDCWD, a1, a2, a3, 0, 0))

static int64_t fb_getdirentries(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (!syscall_uptr_validate((void *)a2, a3)) return -EFAULT;
    vfs_file_t *f = fd_get(t->fd_table, (int)a1);
    if (!f) return -EBADF;
    if (a4) {
        int64_t base = (int64_t)f->offset;
        if (syscall_copy_to_user((void *)a4, &base, sizeof base) < 0) { fd_put(f); return -EFAULT; }
    }
    uint8_t *out = (uint8_t *)a2;
    size_t cap = a3, used = 0;
    int64_t rc = 0;
    while (cap - used >= 24 + VFS_MAX_NAME) {
        vfs_dirent_t kd;
        int r = vfs_readdir(f, &kd);
        if (r < 0) { if (r == -ENOTDIR) rc = r; break; }
        size_t nl = strnlen(kd.d_name, VFS_MAX_NAME - 1);
        size_t reclen = (24 + nl + 1 + 7) & ~(size_t)7;
        uint8_t rec[24 + VFS_MAX_NAME + 8];
        memset(rec, 0, reclen);
        uint64_t ino = kd.d_ino ? kd.d_ino : 1;
        int64_t off = (int64_t)f->offset;
        uint16_t rl = (uint16_t)reclen, namlen = (uint16_t)nl;
        memcpy(rec, &ino, 8);
        memcpy(rec + 8, &off, 8);
        memcpy(rec + 16, &rl, 2);
        rec[18] = fb_dtype(kd.d_type);
        memcpy(rec + 20, &namlen, 2);
        memcpy(rec + 24, kd.d_name, nl);
        if (syscall_copy_to_user(out + used, rec, reclen) < 0) { rc = -EFAULT; break; }
        used += reclen;
    }
    fd_put(f);
    if (used) return (int64_t)used;
    return rc;
}

typedef struct {
    uint32_t f_version;
    uint32_t f_type;
    uint64_t f_flags;
    uint64_t f_bsize;
    uint64_t f_iosize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    int64_t  f_bavail;
    uint64_t f_files;
    int64_t  f_ffree;
    uint64_t f_syncwrites;
    uint64_t f_asyncwrites;
    uint64_t f_syncreads;
    uint64_t f_asyncreads;
    uint32_t f_nvnodelistsize;
    uint32_t f_spare0;
    uint64_t f_spare[9];
    uint32_t f_namemax;
    uint32_t f_owner;
    uint64_t f_fsid;
    char     f_charspare[80];
    char     f_fstypename[16];
    char     f_mntfromname[1024];
    char     f_mntonname[1024];
} fb_statfs_t;

_Static_assert(sizeof(fb_statfs_t) == 2344, "FreeBSD struct statfs is 2344 bytes");

static const char *const g_fb_vfs[] = {
    "ufs", "tmpfs", "devfs", "procfs", "cd9660", "msdosfs", "ext2fs", "udf", "nfs", "nullfs", "fdescfs",
};

static int fb_vfs_typenum(const char *name)
{
    for (size_t i = 0; i < sizeof g_fb_vfs / sizeof g_fb_vfs[0]; i++)
        if (strcmp(g_fb_vfs[i], name) == 0) return (int)i + 1;
    return 0;
}

static const char *fb_fstype_name(const char *cervus)
{
    if (!strcmp(cervus, "ext2") || !strcmp(cervus, "ext3") || !strcmp(cervus, "ext4")) return "ext2fs";
    if (!strcmp(cervus, "ramfs") || !strcmp(cervus, "tmpfs") || !strcmp(cervus, "initramfs")) return "tmpfs";
    if (!strcmp(cervus, "iso9660")) return "cd9660";
    if (!strcmp(cervus, "fat") || !strcmp(cervus, "fat32") || !strcmp(cervus, "vfat") || !strcmp(cervus, "msdos")) return "msdosfs";
    if (!strcmp(cervus, "devfs") || !strcmp(cervus, "procfs") || !strcmp(cervus, "udf")) return cervus;
    return "ufs";
}

#define FB_MAX_MOUNTS 32

static int fb_mounts(vfs_mount_info_t **out)
{
    vfs_mount_info_t *m = malloc(FB_MAX_MOUNTS * sizeof *m);
    if (!m) return -ENOMEM;
    int n = vfs_list_mounts(m, FB_MAX_MOUNTS);
    if (n < 0) n = 0;
    *out = m;
    return n;
}

static void fb_statfs_fill(fb_statfs_t *o, const vfs_statvfs_t *sv, const vfs_mount_info_t *mi)
{
    memset(o, 0, sizeof *o);
    const char *ty = fb_fstype_name(mi ? mi->fstype : "");
    o->f_version = 0x20140518;
    o->f_type    = (uint32_t)fb_vfs_typenum(ty);
    o->f_flags   = 0x1000;
    o->f_bsize   = sv->f_bsize ? sv->f_bsize : 4096;
    o->f_iosize  = 65536;
    o->f_blocks  = sv->f_blocks;
    o->f_bfree   = sv->f_bfree;
    o->f_bavail  = (int64_t)sv->f_bavail;
    o->f_files   = sv->f_files;
    o->f_ffree   = (int64_t)sv->f_ffree;
    o->f_namemax = sv->f_namemax ? sv->f_namemax : 255;
    o->f_fsid    = (uint64_t)o->f_type << 32;
    strncpy(o->f_fstypename, ty, sizeof o->f_fstypename - 1);
    strncpy(o->f_mntfromname, mi && mi->device[0] ? mi->device : ty, sizeof o->f_mntfromname - 1);
    strncpy(o->f_mntonname, mi ? mi->path : "/", sizeof o->f_mntonname - 1);
}

static int64_t fb_statfs_out(const char *kp, uint64_t uptr)
{
    vfs_statvfs_t sv;
    int r = vfs_statvfs(kp, &sv);
    if (r < 0) return r;
    vfs_mount_info_t *m;
    int n = fb_mounts(&m);
    if (n < 0) return n;
    const vfs_mount_info_t *best = NULL;
    size_t bl = 0;
    for (int i = 0; i < n; i++) {
        size_t l = strlen(m[i].path);
        bool match = (l == 1 && m[i].path[0] == '/') ||
                     (strncmp(kp, m[i].path, l) == 0 && (kp[l] == '/' || kp[l] == 0));
        if (match && l >= bl) { best = &m[i]; bl = l; }
    }
    fb_statfs_t *o = malloc(sizeof *o);
    if (!o) { free(m); return -ENOMEM; }
    fb_statfs_fill(o, &sv, best);
    int64_t rc = syscall_copy_to_user((void *)uptr, o, sizeof *o);
    free(o);
    free(m);
    return rc;
}

static int64_t fb_statfs(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    char kp[VFS_MAX_PATH];
    int r = syscall_resolve_at(t, FB_AT_FDCWD, a1, kp, sizeof kp);
    if (r < 0) return r;
    return fb_statfs_out(kp, a2);
}

static int64_t fb_fstatfs(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)a1);
    if (!f) return -EBADF;
    char kp[VFS_MAX_PATH];
    strncpy(kp, f->path ? f->path : "/", sizeof kp - 1);
    kp[sizeof kp - 1] = 0;
    fd_put(f);
    return fb_statfs_out(kp, a2);
}

static int64_t fb_getfsstat(A6)
{
    UNUSED6;
    vfs_mount_info_t *m;
    int n = fb_mounts(&m);
    if (n < 0) return n;
    if (!a1) { free(m); return n; }
    int cap = (int)(a2 / sizeof(fb_statfs_t)), done = 0;
    fb_statfs_t *o = malloc(sizeof *o);
    if (!o) { free(m); return -ENOMEM; }
    for (int i = 0; i < n && done < cap; i++) {
        vfs_statvfs_t sv;
        if (vfs_statvfs(m[i].path, &sv) < 0) memset(&sv, 0, sizeof sv);
        fb_statfs_fill(o, &sv, &m[i]);
        if (syscall_copy_to_user((void *)(a1 + (uint64_t)done * sizeof *o), o, sizeof *o) < 0) { done = -EFAULT; break; }
        done++;
    }
    free(o);
    free(m);
    return done;
}

static int64_t fb_unlinkat(A6)
{
    UNUSED6;
    return sys_unlinkat(I(a1), a2, (a3 & FB_AT_REMOVEDIR) ? LX_AT_REMOVEDIR : 0);
}

FB0(fb_fchmodat,  sys_fchmodat(I(a1), a2, a3, (uint64_t)fb_atflags(a4)))
FB0(fb_fchownat,  sys_fchownat(I(a1), a2, I(a3), I(a4), (uint64_t)fb_atflags(a5)))
FB0(fb_linkat,    sys_linkat(I(a1), a2, I(a3), a4, (uint64_t)fb_atflags(a5)))
FB0(fb_mknodat,   sys_mknodat(I(a1), a2, a3, a4))
FB0(fb_mkfifo,    sys_mknodat((uint64_t)(int64_t)FB_AT_FDCWD, a1, (a2 & 07777) | 0010000, 0))
FB0(fb_mkfifoat,  sys_mknodat(I(a1), a2, (a3 & 07777) | 0010000, 0))

extern int64_t kern_utimens(int64_t dirfd, uint64_t upath, int64_t at, int64_t mt);

static int64_t fb_utime_pick(const int64_t ts[2])
{
    if (ts[1] == -1) return -1;
    if (ts[1] == -2) return -2;
    return ts[0] < 0 ? 0 : ts[0];
}

static int64_t fb_utimensat_impl(uint64_t dirfd, uint64_t path, uint64_t times)
{
    int64_t at = -1, mt = -1;
    if (times) {
        int64_t ts[4];
        if (syscall_copy_from_user(ts, (const void *)times, sizeof ts) < 0) return -EFAULT;
        at = fb_utime_pick(ts);
        mt = fb_utime_pick(ts + 2);
    }
    return kern_utimens((int64_t)dirfd, path, at, mt);
}

FB0(fb_utimensat, fb_utimensat_impl(I(a1), a2, a3))
FB0(fb_futimens,  fb_utimensat_impl(I(a1), 0, a2))

static int64_t fb_utimes_impl(uint64_t dirfd, uint64_t path, uint64_t tv)
{
    int64_t at = -1, mt = -1;
    if (tv) {
        int64_t v[4];
        if (syscall_copy_from_user(v, (const void *)tv, sizeof v) < 0) return -EFAULT;
        at = v[0] < 0 ? 0 : v[0];
        mt = v[2] < 0 ? 0 : v[2];
    }
    return kern_utimens((int64_t)dirfd, path, at, mt);
}

FB0(fb_utimes,   fb_utimes_impl((uint64_t)(int64_t)FB_AT_FDCWD, a1, a2))
FB0(fb_lutimes,  fb_utimes_impl((uint64_t)(int64_t)FB_AT_FDCWD, a1, a2))
FB0(fb_futimes,  fb_utimes_impl(I(a1), 0, a2))
FB0(fb_futimesat, fb_utimes_impl(I(a1), a2, a3))

static int64_t fb_chflags(A6)
{
    UNUSED6;
    return (a2 & 0xffffffff) ? -EOPNOTSUPP : 0;
}

static int64_t fb_chflagsat(A6)
{
    UNUSED6;
    return (a3 & 0xffffffff) ? -EOPNOTSUPP : 0;
}

static int64_t fb_pipe2(A6)
{
    UNUSED6;
    int fds[2];
    int64_t r = sys_pipe(a1);
    if (r < 0 || !a2) return r;
    if (syscall_copy_from_user(fds, (const void *)a1, sizeof fds) < 0) return -EFAULT;
    for (int i = 0; i < 2; i++) {
        if (a2 & 0x100000) sys_fcntl((uint64_t)fds[i], 2, 1);
        if (a2 & 0x0004) sys_fcntl((uint64_t)fds[i], 4, O_NONBLOCK);
    }
    return 0;
}

typedef struct {
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
    int16_t l_type;
    int16_t l_whence;
    int32_t l_sysid;
    int32_t pad;
} fb_flock_t;

typedef struct {
    int16_t l_type;
    int16_t l_whence;
    int32_t pad;
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
    int32_t pad2;
} lx_flock_t;

static int64_t fb_fcntl_lock(uint64_t fd, int lcmd, uint64_t arg)
{
    task_t *t = syscall_cur_task();
    fb_flock_t b;
    if (syscall_copy_from_user(&b, (const void *)arg, sizeof b) < 0) return -EFAULT;
    lx_flock_t l = { 0, b.l_whence, 0, b.l_start, b.l_len, b.l_pid, 0 };
    l.l_type = b.l_type == 1 ? 0 : b.l_type == 3 ? 1 : 2;
    uint64_t s = fb_scratch(t, sizeof l);
    if (syscall_copy_to_user((void *)s, &l, sizeof l) < 0) return -EFAULT;
    int64_t r = sys_fcntl(fd, (uint64_t)lcmd, s);
    if (r < 0 || lcmd != 5) return r;
    if (syscall_copy_from_user(&l, (const void *)s, sizeof l) < 0) return -EFAULT;
    b.l_type = l.l_type == 0 ? 1 : l.l_type == 1 ? 3 : 2;
    b.l_whence = l.l_whence;
    b.l_start = l.l_start;
    b.l_len = l.l_len;
    b.l_pid = l.l_pid;
    return syscall_copy_to_user((void *)arg, &b, sizeof b);
}

static int64_t fb_fcntl(A6)
{
    UNUSED6;
    switch (a2) {
    case 0: case 1: case 2:
        return sys_fcntl(a1, a2, a3);
    case 3: {
        int64_t r = sys_fcntl(a1, 3, 0);
        return r < 0 ? r : (int64_t)fb_oflags_out((uint64_t)r);
    }
    case 4:
        return sys_fcntl(a1, 4, fb_oflags(a3) & ~(uint64_t)3);
    case 5:  return sys_fcntl(a1, 9, 0);
    case 6:  return sys_fcntl(a1, 8, a3);
    case 7: case 11: return fb_fcntl_lock(a1, 5, a3);
    case 8: case 12: return fb_fcntl_lock(a1, 6, a3);
    case 9: case 13: return fb_fcntl_lock(a1, 7, a3);
    case 10: return sys_dup2(a1, a3);
    case 17: return sys_fcntl(a1, 1030, a3);
    case 18: {
        if (a1 == a3) return (int64_t)a3;
        int64_t r = sys_dup2(a1, a3);
        if (r >= 0) sys_fcntl((uint64_t)r, 2, 1);
        return r;
    }
    case 19: return sys_fcntl(a1, 1033, a3);
    case 20: return sys_fcntl(a1, 1034, a3);
    case 15: case 16: case 21: return 0;
    }
    return -EINVAL;
}

static uint64_t fb_mmap_flags(uint64_t f)
{
    uint64_t o = f & 3;
    if (!o) o = 2;
    if (f & 0x0010)  o |= 0x10;
    if (f & 0x1400)  o |= 0x20;
    if (f & 0x0400)  o |= 0x20000;
    if (f & 0x80000) o |= 0x40;
    if ((f & 0x4000) && (f & 0x0010)) o |= 0x100000;
    return o;
}

static int64_t fb_mmap(A6)
{
    UNUSED6;
    uint64_t addr = a1, len = a2, prot = a3, flags = a4, fd = a5, off = a6;
    if (flags & 0x2000) {
        if (prot || (int32_t)fd != -1 || off) return -EINVAL;
        flags = (flags & ~(uint64_t)0x2000) | 0x1000 | 0x2;
    }
    uint64_t lflags = fb_mmap_flags(flags);
    if (flags & 0x1400) fd = (uint64_t)-1;
    unsigned al = (unsigned)((flags >> 24) & 0xff);
    uint64_t align = al == 1 ? (2ULL << 20) : (al >= 12 && al < 48) ? (1ULL << al) : 0;
    if (!align || (flags & 0x10) || len == 0)
        return sys_mmap(addr, len, prot, lflags, fd, off);
    uint64_t span = len + align;
    int64_t r = sys_mmap(addr, span, prot, lflags, fd, off);
    if (r < 0) return r;
    uint64_t base = (uint64_t)r;
    uint64_t start = (base + align - 1) & ~(align - 1);
    if (start != base && !(flags & 0x1400)) {
        sys_munmap(base, span);
        r = sys_mmap(start, len, prot, lflags | 0x10, fd, off);
        return r;
    }
    if (start > base) sys_munmap(base, start - base);
    uint64_t end = start + ((len + 4095) & ~4095ULL);
    if (base + span > end) sys_munmap(end, base + span - end);
    return (int64_t)start;
}

static int64_t fb_madvise(A6)
{
    UNUSED6;
    switch (a3) {
    case 0: case 1: case 2: case 3: case 4:
        return sys_madvise(a1, a2, a3);
    case 5:
        return sys_madvise(a1, a2, 8);
    }
    return 0;
}

static int64_t fb_msync(A6)
{
    UNUSED6;
    uint64_t f = 0;
    if (a3 & 1) f |= 1;
    else f |= 4;
    if (a3 & 2) f |= 2;
    return sys_msync(a1, a2, f);
}

static int64_t fb_break(A6)
{
    UNUSED6;
    int64_t r = sys_brk(a1);
    if (r < 0) return r;
    return (uint64_t)r >= a1 ? 0 : -ENOMEM;
}

static int64_t fb_getcwd(A6)
{
    UNUSED6;
    int64_t r = sys_getcwd(a1, a2, 0, 0, 0, 0);
    return r < 0 ? r : 0;
}

static int64_t fb_issetugid(A6) { UNUSED6; return 0; }
FB0(fb_zero,    0)
FB0(fb_enosys,  -ENOSYS)
FB0(fb_einval,  -EINVAL)
FB0(fb_getpgrp, sys_getpgid(0))
FB0(fb_exit,    sys_exit_group(a1))

static int64_t fb_getlogin(A6)
{
    UNUSED6;
    static const char name[] = "root";
    if (a2 < sizeof name) return -ERANGE;
    return syscall_copy_to_user((void *)a1, name, sizeof name);
}

static int64_t fb_getgroups(A6)
{
    UNUSED6;
    if (a1 == 0) return 1;
    task_t *t = syscall_cur_task();
    uint32_t g = t ? t->gid : 0;
    if (syscall_copy_to_user((void *)a2, &g, sizeof g) < 0) return -EFAULT;
    return 1;
}

static int64_t fb_getresid(uint64_t a, uint64_t b, uint64_t c, uint32_t v)
{
    if ((a && syscall_copy_to_user((void *)a, &v, 4) < 0) || (b && syscall_copy_to_user((void *)b, &v, 4) < 0) ||
        (c && syscall_copy_to_user((void *)c, &v, 4) < 0)) return -EFAULT;
    return 0;
}

FB0(fb_getresuid, fb_getresid(a1, a2, a3, (uint32_t)sys_getuid()))
FB0(fb_getresgid, fb_getresid(a1, a2, a3, (uint32_t)sys_getgid()))

static int64_t fb_getpriority(A6)
{
    UNUSED6;
    int64_t r = sys_getpriority(a1, I(a2));
    return r < 0 ? r : 20 - r;
}

static int fb_rlimit_res(uint64_t r)
{
    static const int8_t map[15] = { 0, 1, 2, 3, 4, 5, 8, 6, 7, -1, 9, -1, -1, -1, -1 };
    return r < 15 ? map[r] : -2;
}

static int64_t fb_getrlimit(A6)
{
    UNUSED6;
    int lr = fb_rlimit_res(a1);
    uint64_t v[2] = { 0x7fffffffffffffffULL, 0x7fffffffffffffffULL };
    if (lr == -2) return -EINVAL;
    if (lr >= 0) {
        task_t *t = syscall_cur_task();
        uint64_t s = fb_scratch(t, sizeof v);
        int64_t r = sys_prlimit(0, (uint64_t)lr, 0, s);
        if (r < 0) return r;
        if (syscall_copy_from_user(v, (const void *)s, sizeof v) < 0) return -EFAULT;
        for (int i = 0; i < 2; i++)
            if (v[i] == ~0ULL || v[i] > 0x7fffffffffffffffULL) v[i] = 0x7fffffffffffffffULL;
    }
    return syscall_copy_to_user((void *)a2, v, sizeof v);
}

static int64_t fb_setrlimit(A6)
{
    UNUSED6;
    int lr = fb_rlimit_res(a1);
    if (lr == -2) return -EINVAL;
    if (lr < 0) return 0;
    uint64_t v[2];
    if (syscall_copy_from_user(v, (const void *)a2, sizeof v) < 0) return -EFAULT;
    for (int i = 0; i < 2; i++)
        if (v[i] == 0x7fffffffffffffffULL) v[i] = ~0ULL;
    task_t *t = syscall_cur_task();
    uint64_t s = fb_scratch(t, sizeof v);
    if (syscall_copy_to_user((void *)s, v, sizeof v) < 0) return -EFAULT;
    return sys_prlimit(0, (uint64_t)lr, s, 0);
}

static int64_t fb_getdtablesize(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    return t ? (int64_t)t->rlim[RLIMIT_NOFILE_K].cur : 1024;
}

static int64_t fb_pathconf_value(uint64_t name)
{
    switch (name) {
    case 1: return 32767;
    case 2: case 3: return 255;
    case 4: return 255;
    case 5: return 1024;
    case 6: return 512;
    case 7: case 8: return 1;
    case 9: return 0xff;
    case 13: return 64;
    }
    return -EINVAL;
}

FB0(fb_pathconf, fb_pathconf_value(a2))

static uint64_t fb_clock(uint64_t c)
{
    switch (c) {
    case 0: case 9: case 10: return 0;
    case 4: case 11: case 12: return 1;
    case 5: case 7: case 8: return 7;
    case 13: return 5;
    case 14: return 3;
    case 1: case 2: case 15: return 2;
    case 16: return 11;
    }
    return c;
}

FB0(fb_clock_gettime, sys_clock_get(fb_clock(a1), a2))

static int64_t fb_clock_getres(A6)
{
    UNUSED6;
    if (!a2) return 0;
    int64_t ts[2] = { 0, a1 == 13 ? 1000000000 : 1 };
    if (a1 == 13) { ts[0] = 1; ts[1] = 0; }
    return syscall_copy_to_user((void *)a2, ts, sizeof ts);
}

static uint64_t fb_clock_ns(uint64_t clk)
{
    uint64_t mono = sched_now_ns();
    uint64_t l = fb_clock(clk);
    if (l == 0 || l == 5 || l == 11) return (uint64_t)((int64_t)mono + clock_realtime_offset_ns());
    return mono;
}

static int fb_get_timespec(uint64_t uptr, uint64_t *ns)
{
    int64_t ts[2];
    if (syscall_copy_from_user(ts, (const void *)uptr, sizeof ts) < 0) return -EFAULT;
    if (ts[0] < 0 || ts[1] < 0 || ts[1] >= 1000000000) return -EINVAL;
    *ns = (uint64_t)ts[0] * 1000000000ULL + (uint64_t)ts[1];
    return 0;
}

static int64_t fb_sleep(uint64_t ns, uint64_t rem_ptr)
{
    uint64_t start = sched_now_ns();
    int64_t r = sys_sleep_ns(ns);
    if (r == -EINTR && rem_ptr) {
        uint64_t done = sched_now_ns() - start;
        uint64_t left = done < ns ? ns - done : 0;
        int64_t rem[2] = { (int64_t)(left / 1000000000ULL), (int64_t)(left % 1000000000ULL) };
        syscall_copy_to_user((void *)rem_ptr, rem, sizeof rem);
    }
    return r < 0 ? r : 0;
}

static int64_t fb_nanosleep(A6)
{
    UNUSED6;
    uint64_t ns;
    int r = fb_get_timespec(a1, &ns);
    if (r < 0) return r;
    return fb_sleep(ns, a2);
}

static int64_t fb_clock_nanosleep(A6)
{
    UNUSED6;
    uint64_t ns;
    int r = fb_get_timespec(a3, &ns);
    if (r < 0) return r;
    int64_t rc;
    if (a2 & 1) {
        uint64_t now = fb_clock_ns(a1);
        rc = ns <= now ? 0 : fb_sleep(ns - now, 0);
    } else {
        rc = fb_sleep(ns, a4);
    }
    return rc < 0 ? fbsd_errno_from_linux(-rc) : 0;
}

static int64_t fb_gettimeofday(A6)
{
    UNUSED6;
    if (a1) {
        uint64_t ns = fb_clock_ns(0);
        int64_t tv[2] = { (int64_t)(ns / 1000000000ULL), (int64_t)((ns % 1000000000ULL) / 1000) };
        if (syscall_copy_to_user((void *)a1, tv, sizeof tv) < 0) return -EFAULT;
    }
    if (a2) {
        int32_t tz[2] = { 0, 0 };
        if (syscall_copy_to_user((void *)a2, tz, sizeof tz) < 0) return -EFAULT;
    }
    return 0;
}

static int64_t fb_getrandom(A6)
{
    UNUSED6;
    size_t len = a2 > 33554432 ? 33554432 : a2;
    uint8_t buf[256];
    size_t done = 0;
    while (done < len) {
        size_t n = len - done < sizeof buf ? len - done : sizeof buf;
        random_bytes(buf, n);
        if (syscall_copy_to_user((uint8_t *)a1 + done, buf, n) < 0) return done ? (int64_t)done : -EFAULT;
        done += n;
    }
    return (int64_t)len;
}

static int64_t fb_sysarch(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    uint64_t v = 0;
    switch (a1) {
    case 129: case 136:
        if (syscall_copy_from_user(&v, (const void *)a2, sizeof v) < 0) return -EFAULT;
        return sys_arch_prctl(0x1002, v, 0, 0, 0, 0);
    case 131:
        if (syscall_copy_from_user(&v, (const void *)a2, sizeof v) < 0) return -EFAULT;
        return sys_arch_prctl(0x1001, v, 0, 0, 0, 0);
    case 128: case 135: {
        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100u));
        v = ((uint64_t)hi << 32) | lo;
        return syscall_copy_to_user((void *)a2, &v, sizeof v);
    }
    case 130:
        v = t ? t->gs_base : 0;
        return syscall_copy_to_user((void *)a2, &v, sizeof v);
    }
    return -EINVAL;
}

static int64_t fb_kill(A6)
{
    UNUSED6;
    int s = fb_sig_to_linux((int)a2);
    if (s < 0) return -EINVAL;
    return sys_kill(I(a1), (uint64_t)s);
}

static int64_t fb_thr_kill(A6)
{
    UNUSED6;
    int s = fb_sig_to_linux((int)a2);
    if (s < 0) return -EINVAL;
    return sys_tkill(a1, (uint64_t)s);
}

static int64_t fb_thr_kill2(A6)
{
    UNUSED6;
    int s = fb_sig_to_linux((int)a3);
    if (s < 0) return -EINVAL;
    return sys_tgkill(I(a1), a2, (uint64_t)s);
}

static int64_t fb_thr_self(A6)
{
    UNUSED6;
    int64_t tid = sys_gettid();
    return syscall_copy_to_user((void *)a1, &tid, sizeof tid);
}

static uint64_t fb_saflags_to_linux(uint32_t f)
{
    uint64_t o = 0;
    if (f & 0x01) o |= SA_ONSTACK;
    if (f & 0x02) o |= SA_RESTART;
    if (f & 0x04) o |= SA_RESETHAND;
    if (f & 0x08) o |= 0x1;
    if (f & 0x10) o |= SA_NODEFER;
    if (f & 0x20) o |= 0x2;
    if (f & 0x40) o |= SA_SIGINFO;
    return o;
}

static uint32_t fb_saflags_from_linux(uint64_t o)
{
    uint32_t f = 0;
    if (o & SA_ONSTACK)   f |= 0x01;
    if (o & SA_RESTART)   f |= 0x02;
    if (o & SA_RESETHAND) f |= 0x04;
    if (o & 0x1)          f |= 0x08;
    if (o & SA_NODEFER)   f |= 0x10;
    if (o & 0x2)          f |= 0x20;
    if (o & SA_SIGINFO)   f |= 0x40;
    return f;
}

typedef struct {
    uint64_t handler;
    uint32_t flags;
    uint32_t mask[4];
    uint32_t pad;
} fb_sigaction_t;

static int64_t fb_sigaction(A6)
{
    UNUSED6;
    int s = fb_sig_to_linux((int)a1);
    if (s <= 0) return -EINVAL;
    task_t *t = syscall_cur_task();
    uint64_t sc = fb_scratch(t, 64);
    uint64_t kact = 0, koact = 0;
    if (a2) {
        fb_sigaction_t b;
        if (syscall_copy_from_user(&b, (const void *)a2, sizeof b) < 0) return -EFAULT;
        uint64_t k[4] = { b.handler, fb_saflags_to_linux(b.flags), 0, fbsd_sigset_to_linux(b.mask) };
        kact = sc;
        if (syscall_copy_to_user((void *)kact, k, sizeof k) < 0) return -EFAULT;
    }
    if (a3) koact = sc + 32;
    int64_t r = sys_rt_sigaction((uint64_t)s, kact, koact, 8);
    if (r < 0 || !a3) return r;
    uint64_t k[4];
    if (syscall_copy_from_user(k, (const void *)koact, sizeof k) < 0) return -EFAULT;
    fb_sigaction_t o;
    memset(&o, 0, sizeof o);
    o.handler = k[0];
    o.flags = fb_saflags_from_linux(k[1]);
    fbsd_sigset_from_linux(k[3], o.mask);
    return syscall_copy_to_user((void *)a3, &o, sizeof o);
}

static int64_t fb_sigprocmask(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    uint64_t sc = fb_scratch(t, 16);
    uint64_t set = 0, oset = 0;
    if (a2) {
        if (a1 < 1 || a1 > 3) return -EINVAL;
        uint32_t b[4];
        if (syscall_copy_from_user(b, (const void *)a2, sizeof b) < 0) return -EFAULT;
        uint64_t m = fbsd_sigset_to_linux(b);
        set = sc;
        if (syscall_copy_to_user((void *)set, &m, sizeof m) < 0) return -EFAULT;
    }
    if (a3) oset = sc + 8;
    int64_t r = sys_rt_sigprocmask(a2 ? a1 - 1 : 0, set, oset, 8);
    if (r < 0 || !a3) return r;
    uint64_t m;
    if (syscall_copy_from_user(&m, (const void *)oset, sizeof m) < 0) return -EFAULT;
    uint32_t b[4];
    fbsd_sigset_from_linux(m, b);
    return syscall_copy_to_user((void *)a3, b, sizeof b);
}

static int64_t fb_mask_in(uint64_t uset, uint64_t *scratch_out)
{
    task_t *t = syscall_cur_task();
    uint32_t b[4];
    if (syscall_copy_from_user(b, (const void *)uset, sizeof b) < 0) return -EFAULT;
    uint64_t m = fbsd_sigset_to_linux(b);
    uint64_t s = fb_scratch(t, 8);
    if (syscall_copy_to_user((void *)s, &m, sizeof m) < 0) return -EFAULT;
    *scratch_out = s;
    return 0;
}

static int64_t fb_sigsuspend(A6)
{
    UNUSED6;
    uint64_t s;
    int64_t r = fb_mask_in(a1, &s);
    return r < 0 ? r : sys_rt_sigsuspend(s, 8);
}

static int64_t fb_sigpending(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    uint64_t s = fb_scratch(t, 8);
    int64_t r = sys_rt_sigpending(s, 8);
    if (r < 0) return r;
    uint64_t m;
    if (syscall_copy_from_user(&m, (const void *)s, sizeof m) < 0) return -EFAULT;
    uint32_t b[4];
    fbsd_sigset_from_linux(m, b);
    return syscall_copy_to_user((void *)a1, b, sizeof b);
}

static int64_t fb_sigtimedwait_impl(uint64_t uset, uint64_t uinfo, uint64_t uts)
{
    task_t *t = syscall_cur_task();
    uint64_t sc = fb_scratch(t, 192);
    uint32_t b[4];
    if (syscall_copy_from_user(b, (const void *)uset, sizeof b) < 0) return -EFAULT;
    uint64_t m = fbsd_sigset_to_linux(b);
    if (syscall_copy_to_user((void *)sc, &m, sizeof m) < 0) return -EFAULT;
    uint64_t li = uinfo ? sc + 64 : 0;
    int64_t r = sys_sigtimedwait(sc, li, uts);
    if (r <= 0) return r;
    int bs = fbsd_sig_from_linux((int)r);
    if (uinfo) {
        uint8_t lin[128];
        if (syscall_copy_from_user(lin, (const void *)li, sizeof lin) < 0) return -EFAULT;
        ksiginfo_t si;
        memset(&si, 0, sizeof si);
        int32_t code, pid, uid, status;
        memcpy(&code, lin + 8, 4);
        memcpy(&pid, lin + 16, 4);
        memcpy(&uid, lin + 20, 4);
        memcpy(&status, lin + 24, 4);
        si.code = code; si.pid = (uint32_t)pid; si.uid = (uint32_t)uid; si.status = status;
        memcpy(&si.value, lin + 24, 8);
        uint8_t out[80];
        fbsd_fill_siginfo(out, bs, &si);
        if (syscall_copy_to_user((void *)uinfo, out, sizeof out) < 0) return -EFAULT;
    }
    return bs;
}

FB0(fb_sigtimedwait, fb_sigtimedwait_impl(a1, a2, a3))
FB0(fb_sigwaitinfo,  fb_sigtimedwait_impl(a1, a2, 0))

static int64_t fb_sigwait(A6)
{
    UNUSED6;
    int64_t r = fb_sigtimedwait_impl(a1, 0, 0);
    if (r < 0) return fbsd_errno_from_linux(-r);
    int32_t s = (int32_t)r;
    if (syscall_copy_to_user((void *)a2, &s, sizeof s) < 0) return 14;
    return 0;
}

static int64_t fb_sigqueue(A6)
{
    UNUSED6;
    int s = fb_sig_to_linux((int)a2);
    if (s < 0) return -EINVAL;
    return sys_sigqueue(I(a1), (uint64_t)s, a3);
}

static int64_t fb_sigaltstack(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    uint64_t sc = fb_scratch(t, 64);
    uint64_t lss = 0, loss = 0;
    if (a1) {
        struct { uint64_t sp; uint64_t size; int32_t flags; int32_t pad; } b;
        if (syscall_copy_from_user(&b, (const void *)a1, sizeof b) < 0) return -EFAULT;
        struct { uint64_t sp; int32_t flags; int32_t pad; uint64_t size; } l = {
            b.sp, (b.flags & 4) ? 2 : (b.flags & 1), 0, b.size };
        lss = sc;
        if (syscall_copy_to_user((void *)lss, &l, sizeof l) < 0) return -EFAULT;
    }
    if (a2) loss = sc + 32;
    int64_t r = sys_sigaltstack(lss, loss);
    if (r < 0 || !a2) return r;
    struct { uint64_t sp; int32_t flags; int32_t pad; uint64_t size; } l;
    if (syscall_copy_from_user(&l, (const void *)loss, sizeof l) < 0) return -EFAULT;
    struct { uint64_t sp; uint64_t size; int32_t flags; int32_t pad; } b = {
        l.sp, l.size, (l.flags & 2) ? 4 : (l.flags & 1), 0 };
    return syscall_copy_to_user((void *)a2, &b, sizeof b);
}

static int32_t fb_wstatus(int32_t st)
{
    if ((st & 0x7f) == 0) return st;
    if (st == 0xffff) return 0x13;
    if ((st & 0xff) == 0x7f) return (fbsd_sig_from_linux((st >> 8) & 0xff) << 8) | 0x7f;
    return (st & ~0x7f) | (fbsd_sig_from_linux(st & 0x7f) & 0x7f);
}

static uint64_t fb_wait_options(uint64_t o)
{
    uint64_t l = o & 3;
    if (o & 4)  l |= 8;
    if (o & 8)  l |= 0x1000000;
    if (o & 16) l |= 4;
    return l;
}

static int64_t fb_wait4(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    uint64_t s = a2 ? fb_scratch(t, 8) : 0;
    int64_t r = sys_wait4(I(a1), s, fb_wait_options(a3), a4);
    if (r > 0 && a2) {
        int32_t st;
        if (syscall_copy_from_user(&st, (const void *)s, sizeof st) < 0) return -EFAULT;
        st = fb_wstatus(st);
        if (syscall_copy_to_user((void *)a2, &st, sizeof st) < 0) return -EFAULT;
    }
    return r;
}

static int64_t fb_wait6(A6)
{
    UNUSED6;
    uint64_t lid;
    switch ((int)a1) {
    case 7: lid = 0; break;
    case 0: lid = 1; break;
    case 2: lid = 2; break;
    default: return -EINVAL;
    }
    uint64_t o = a4;
    uint64_t lo = 0;
    if (o & 1)  lo |= 1;
    if (o & 2)  lo |= 2;
    if (o & 4)  lo |= 8;
    if (o & 8)  lo |= 0x1000000;
    if (o & 16) lo |= 4;
    if (!(lo & (2 | 4 | 8))) lo |= 4;
    task_t *t = syscall_cur_task();
    uint64_t si = fb_scratch(t, 128);
    uint8_t zero[128];
    memset(zero, 0, sizeof zero);
    if (syscall_copy_to_user((void *)si, zero, sizeof zero) < 0) return -EFAULT;
    int64_t r = sys_waitid(lid, I(a2), si, lo);
    if (r < 0) return r;
    uint8_t li[128];
    if (syscall_copy_from_user(li, (const void *)si, sizeof li) < 0) return -EFAULT;
    int32_t code, pid, uid, st;
    memcpy(&code, li + 8, 4);
    memcpy(&pid, li + 16, 4);
    memcpy(&uid, li + 20, 4);
    memcpy(&st, li + 24, 4);
    if (pid == 0) return 0;
    int32_t bst;
    switch (code) {
    case 1: bst = (st & 0xff) << 8; break;
    case 2: bst = fbsd_sig_from_linux(st) & 0x7f; break;
    case 3: bst = (fbsd_sig_from_linux(st) & 0x7f) | 0x80; break;
    case 4: case 5: bst = (fbsd_sig_from_linux(st) << 8) | 0x7f; break;
    case 6: bst = 0x13; break;
    default: bst = 0;
    }
    if (a3 && syscall_copy_to_user((void *)a3, &bst, 4) < 0) return -EFAULT;
    if (a5) {
        uint8_t wr[288];
        memset(wr, 0, sizeof wr);
        if (syscall_copy_to_user((void *)a5, wr, sizeof wr) < 0) return -EFAULT;
    }
    if (a6) {
        ksiginfo_t k;
        memset(&k, 0, sizeof k);
        k.code = code;
        k.pid = (uint32_t)pid;
        k.uid = (uint32_t)uid;
        k.status = (code == 1) ? (st & 0xff) : fbsd_sig_from_linux(st);
        uint8_t out[80];
        fbsd_fill_siginfo(out, 20, &k);
        if (syscall_copy_to_user((void *)a6, out, sizeof out) < 0) return -EFAULT;
    }
    return pid;
}

static int64_t fb_spawn(int share_vm, int wait_vfork)
{
    task_t *me = syscall_cur_task();
    if (!me || !me->is_userspace) return -EPERM;
    int64_t err = 0;
    task_t *child = fork_process(me, share_vm, 0, &err);
    if (!child) return err;
    child->user_saved_r11 &= ~1ULL;
    int32_t pid = (int32_t)child->pid;
    struct vfork_wait *w = NULL;
    if (wait_vfork && task_vfork_attach(child) == 0) w = child->vfork_wait;
    ptrace_note_child(me, child, wait_vfork ? PTRACE_EVENT_VFORK : PTRACE_EVENT_FORK);
    task_start_clone(child);
    if (w) task_vfork_wait(me, w);
    return pid;
}

FB0(fb_fork,  fb_spawn(0, 0))
FB0(fb_vfork, fb_spawn(1, 1))

static int64_t fb_rfork(A6)
{
    UNUSED6;
    if (!(a1 & 0x10)) return -EINVAL;
    if (a1 & 0x20) return -EINVAL;
    return fb_spawn(0, 0);
}

typedef struct {
    uint32_t pid;
    int      daemon;
} fb_procdesc_t;

static int pd_poll(vnode_t *n, int events)
{
    (void)events;
    fb_procdesc_t *pd = n->fs_data;
    task_t *t = task_find_by_pid(pd->pid);
    if (!t || t->state == TASK_ZOMBIE || t->state == TASK_DEAD) return POLLHUP;
    return 0;
}

static void pd_unref(vnode_t *n)
{
    if (--n->refcount > 0) return;
    fb_procdesc_t *pd = n->fs_data;
    if (!pd->daemon) {
        task_t *t = task_find_by_pid(pd->pid);
        if (t && t->state != TASK_ZOMBIE && t->state != TASK_DEAD) signal_send(t, SIGKILL);
    }
    free(pd);
    free(n);
}

static const vnode_ops_t PD_OPS = {
    .poll  = pd_poll,
    .unref = pd_unref,
};

static fb_procdesc_t *pd_get(uint64_t fd, vfs_file_t **fout)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return NULL;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return NULL;
    if (!f->vnode || f->vnode->ops != &PD_OPS) { fd_put(f); return NULL; }
    *fout = f;
    return f->vnode->fs_data;
}

static int64_t fb_pdfork(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    fb_procdesc_t *pd = calloc(1, sizeof *pd);
    vnode_t *vn = calloc(1, sizeof *vn);
    vfs_file_t *file = vfs_file_alloc();
    if (!pd || !vn || !file) {
        free(pd); free(vn);
        if (file) vfs_file_free(file);
        return -ENOMEM;
    }
    int64_t pid = fb_spawn(0, 0);
    if (pid < 0) { free(pd); free(vn); vfs_file_free(file); return pid; }
    pd->pid = (uint32_t)pid;
    pd->daemon = (a2 & 1) != 0;
    vn->type = VFS_NODE_CHARDEV;
    vn->ops = &PD_OPS;
    vn->fs_data = pd;
    vn->refcount = 1;
    file->vnode = vn;
    file->flags = 2;
    int fd = fd_alloc(t->fd_table, file, 0);
    if (fd < 0) { vfs_file_free(file); return pid; }
    if (a2 & 2) fd_set_flags(t->fd_table, fd, FD_CLOEXEC);
    int32_t ufd = fd;
    syscall_copy_to_user((void *)a1, &ufd, sizeof ufd);
    return pid;
}

static int64_t fb_pdgetpid(A6)
{
    UNUSED6;
    vfs_file_t *f;
    fb_procdesc_t *pd = pd_get(a1, &f);
    if (!pd) return -EBADF;
    int32_t pid = (int32_t)pd->pid;
    fd_put(f);
    return syscall_copy_to_user((void *)a2, &pid, sizeof pid);
}

static int64_t fb_pdkill(A6)
{
    UNUSED6;
    vfs_file_t *f;
    fb_procdesc_t *pd = pd_get(a1, &f);
    if (!pd) return -EBADF;
    uint32_t pid = pd->pid;
    fd_put(f);
    int s = fb_sig_to_linux((int)a2);
    if (s < 0) return -EINVAL;
    return sys_kill(pid, (uint64_t)s);
}

static int64_t fb_posix_fadvise(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return 9;
    vfs_file_t *f = fd_get(t->fd_table, (int)a1);
    if (!f) return 9;
    fd_put(f);
    return 0;
}

static int64_t fb_posix_fallocate(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = fb_fd_stat(a1, &st);
    if (r < 0) return fbsd_errno_from_linux(-r);
    if ((int64_t)a2 < 0 || (int64_t)a3 <= 0) return 22;
    uint64_t end = a2 + a3;
    if (end <= st.st_size) return 0;
    int64_t rc = sys_ftruncate(a1, end);
    return rc < 0 ? fbsd_errno_from_linux(-rc) : 0;
}

static int64_t fb_fexecve(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    uint64_t s = fb_scratch(t, 8);
    char z = 0;
    if (syscall_copy_to_user((void *)s, &z, 1) < 0) return -EFAULT;
    return sys_execveat(I(a1), s, a2, a3, LX_AT_EMPTY_PATH);
}

static int64_t fb_cpuset_getaffinity(A6)
{
    UNUSED6;
    size_t sz = a4;
    if (sz == 0 || sz > 1024) return -ERANGE;
    uint8_t mask[1024];
    memset(mask, 0, sz);
    uint32_t n = smp_get_cpu_count();
    for (uint32_t i = 0; i < n && i / 8 < sz; i++) mask[i / 8] |= (uint8_t)(1u << (i % 8));
    return syscall_copy_to_user((void *)a5, mask, sz);
}

static int64_t fb_sched_getcpu(A6)
{
    UNUSED6;
    return (int64_t)smp_cpu_index();
}

static uint64_t fb_domain(uint64_t d)
{
    switch (d) {
    case 1: return 1;
    case 2: return 2;
    case 28: return 10;
    }
    return d | 0x100000000ULL;
}

static uint64_t fb_socktype(uint64_t t)
{
    uint64_t l = t & 0x0fffffff;
    if (t & 0x10000000) l |= O_CLOEXEC;
    if (t & 0x20000000) l |= O_NONBLOCK;
    return l;
}

static int64_t fb_socket(A6)
{
    UNUSED6;
    uint64_t d = fb_domain(a1);
    if (d >> 32) return -EAFNOSUPPORT;
    return sys_socket(d, fb_socktype(a2), a3);
}

static int64_t fb_socketpair(A6)
{
    UNUSED6;
    uint64_t d = fb_domain(a1);
    if (d >> 32) return -EAFNOSUPPORT;
    return sys_socketpair(d, fb_socktype(a2), a4);
}

static int64_t fb_sockaddr_conv(uint64_t uaddr, uint64_t len, uint8_t b[128], uint64_t *outlen)
{
    if (len < 2 || len > 128) return -EINVAL;
    if (syscall_copy_from_user(b, (const void *)uaddr, len) < 0) return -EFAULT;
    uint64_t fam = fb_domain(b[1]);
    if (fam >> 32) return -EAFNOSUPPORT;
    uint16_t lf = (uint16_t)fam;
    memcpy(b, &lf, 2);
    uint64_t l = len;
    if (lf == 1) {
        size_t n = 2;
        while (n < len && b[n]) n++;
        l = n < len ? n + 1 : len;
    }
    *outlen = l;
    return 0;
}

static int64_t fb_sockaddr_in(uint64_t uaddr, uint64_t len, uint64_t *out, uint64_t *outlen)
{
    if (!uaddr) { *out = 0; *outlen = 0; return 0; }
    uint8_t b[128];
    uint64_t l;
    int64_t r = fb_sockaddr_conv(uaddr, len, b, &l);
    if (r < 0) return r;
    task_t *t = syscall_cur_task();
    uint64_t s = fb_scratch(t, 128);
    if (syscall_copy_to_user((void *)s, b, l) < 0) return -EFAULT;
    *out = s;
    *outlen = l;
    return 0;
}

static int64_t fb_sockaddr_out(uint64_t kbuf, uint32_t klen, uint64_t uaddr, uint64_t ulenp)
{
    if (!uaddr || !ulenp) return 0;
    uint32_t cap;
    if (syscall_copy_from_user(&cap, (const void *)ulenp, sizeof cap) < 0) return -EFAULT;
    uint8_t b[128];
    if (klen > sizeof b) klen = sizeof b;
    memset(b, 0, sizeof b);
    if (klen && syscall_copy_from_user(b, (const void *)kbuf, klen) < 0) return -EFAULT;
    uint16_t lf;
    memcpy(&lf, b, 2);
    uint8_t bf = lf == 10 ? 28 : (uint8_t)lf;
    uint32_t blen = klen;
    if (lf == 1 && blen > 106) blen = 106;
    b[0] = (uint8_t)blen;
    b[1] = bf;
    uint32_t n = blen < cap ? blen : cap;
    if (n && syscall_copy_to_user((void *)uaddr, b, n) < 0) return -EFAULT;
    return syscall_copy_to_user((void *)ulenp, &blen, sizeof blen);
}

static int64_t fb_connect(A6)
{
    UNUSED6;
    uint64_t s, l;
    int64_t r = fb_sockaddr_in(a2, a3, &s, &l);
    return r < 0 ? r : sys_connect(a1, s, l);
}

static int64_t fb_bind(A6)
{
    UNUSED6;
    uint64_t s, l;
    int64_t r = fb_sockaddr_in(a2, a3, &s, &l);
    return r < 0 ? r : sys_bind(a1, s, l);
}

static uint64_t fb_msgflags(uint64_t f)
{
    uint64_t l = f & 0x7;
    if (f & 0x8)     l |= 0x80;
    if (f & 0x10)    l |= 0x20;
    if (f & 0x20)    l |= 0x8;
    if (f & 0x40)    l |= 0x100;
    if (f & 0x80)    l |= 0x40;
    if (f & 0x20000) l |= 0x4000;
    if (f & 0x40000) l |= 0x40000000;
    return l;
}

static int64_t fb_sendto(A6)
{
    UNUSED6;
    uint64_t s = 0, l = 0;
    int64_t r = fb_sockaddr_in(a5, a6, &s, &l);
    if (r < 0) return r;
    return sys_sendto_flags(a1, a2, a3, fb_msgflags(a4), s, l);
}

static int64_t fb_recvfrom(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    uint64_t kb = 0, kl = 0;
    if (a5 && a6) {
        kb = fb_scratch(t, 160);
        kl = kb + 128;
        uint32_t cap = 128;
        if (syscall_copy_to_user((void *)kl, &cap, sizeof cap) < 0) return -EFAULT;
    }
    int64_t r = sys_recvfrom_flags(a1, a2, a3, fb_msgflags(a4), kb, kl);
    if (r < 0 || !kb) return r;
    uint32_t got;
    if (syscall_copy_from_user(&got, (const void *)kl, sizeof got) < 0) return -EFAULT;
    int64_t e = fb_sockaddr_out(kb, got, a5, a6);
    return e < 0 ? e : r;
}

static int64_t fb_sockname(uint64_t fd, uint64_t uaddr, uint64_t ulen, int peer, uint64_t flags, int accept)
{
    task_t *t = syscall_cur_task();
    uint64_t kb = 0, kl = 0;
    if (uaddr && ulen) {
        kb = fb_scratch(t, 160);
        kl = kb + 128;
        uint32_t cap = 128;
        if (syscall_copy_to_user((void *)kl, &cap, sizeof cap) < 0) return -EFAULT;
    }
    int64_t r;
    if (accept) r = sys_accept4(fd, kb, kl, flags);
    else r = peer ? sys_getpeername(fd, kb, kl) : sys_getsockname(fd, kb, kl);
    if (r < 0 || !kb) return r;
    uint32_t got;
    if (syscall_copy_from_user(&got, (const void *)kl, sizeof got) < 0) return -EFAULT;
    int64_t e = fb_sockaddr_out(kb, got, uaddr, ulen);
    return e < 0 ? e : r;
}

FB0(fb_getsockname, fb_sockname(a1, a2, a3, 0, 0, 0))
FB0(fb_getpeername, fb_sockname(a1, a2, a3, 1, 0, 0))
FB0(fb_accept,      fb_sockname(a1, a2, a3, 0, 0, 1))
FB0(fb_accept4,     fb_sockname(a1, a2, a3, 0, fb_socktype(a4 & 0x30000000) & (O_CLOEXEC | O_NONBLOCK), 1))

typedef struct {
    uint64_t name;
    uint32_t namelen;
    uint32_t pad0;
    uint64_t iov;
    int32_t  iovlen;
    int32_t  pad1;
    uint64_t control;
    uint32_t controllen;
    int32_t  flags;
} fb_msghdr_t;

typedef struct {
    uint64_t name;
    uint32_t namelen;
    uint32_t pad0;
    uint64_t iov;
    uint64_t iovlen;
    uint64_t control;
    uint64_t controllen;
    int32_t  flags;
    int32_t  pad1;
} lx_msghdr_t;

_Static_assert(sizeof(fb_msghdr_t) == 48, "FreeBSD msghdr is 48 bytes");
_Static_assert(sizeof(lx_msghdr_t) == 56, "Linux msghdr is 56 bytes");

#define FB_CTL_MAX 4096

static int64_t fb_cmsg_to_linux(const uint8_t *in, size_t inlen, uint8_t *out, size_t *outlen)
{
    size_t o = 0, i = 0;
    while (i + 16 <= inlen) {
        uint32_t len;
        int32_t level, type;
        memcpy(&len, in + i, 4);
        memcpy(&level, in + i + 4, 4);
        memcpy(&type, in + i + 8, 4);
        if (len < 16 || i + len > inlen) return -EINVAL;
        size_t dlen = len - 16;
        uint64_t llen = 16 + dlen;
        int32_t llevel = level == 0xffff ? 1 : level;
        int32_t ltype = type;
        if (level == 0xffff && type == 2) ltype = 29;
        memcpy(out + o, &llen, 8);
        memcpy(out + o + 8, &llevel, 4);
        memcpy(out + o + 12, &ltype, 4);
        memcpy(out + o + 16, in + i + 16, dlen);
        o += (llen + 7) & ~(size_t)7;
        i += ((size_t)len + 7) & ~(size_t)7;
    }
    *outlen = o;
    return 0;
}

static size_t fb_cmsg_from_linux(const uint8_t *in, size_t inlen, uint8_t *out, size_t cap)
{
    size_t o = 0, i = 0;
    while (i + 16 <= inlen) {
        uint64_t llen;
        int32_t level, type;
        memcpy(&llen, in + i, 8);
        memcpy(&level, in + i + 8, 4);
        memcpy(&type, in + i + 12, 4);
        if (llen < 16 || i + llen > inlen) break;
        size_t dlen = llen - 16;
        uint32_t len = (uint32_t)(16 + dlen);
        if (o + len > cap) break;
        int32_t blevel = level == 1 ? 0xffff : level;
        int32_t btype = (level == 1 && type == 29) ? 2 : type;
        memset(out + o, 0, 16);
        memcpy(out + o, &len, 4);
        memcpy(out + o + 4, &blevel, 4);
        memcpy(out + o + 8, &btype, 4);
        memcpy(out + o + 16, in + i + 16, dlen);
        o += ((size_t)len + 7) & ~(size_t)7;
        i += ((size_t)llen + 7) & ~(size_t)7;
    }
    return o > cap ? cap : o;
}

static int64_t fb_sendmsg(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    fb_msghdr_t bm;
    if (syscall_copy_from_user(&bm, (const void *)a2, sizeof bm) < 0) return -EFAULT;
    if (bm.iovlen < 0 || bm.controllen > FB_CTL_MAX) return -EINVAL;
    uint64_t base = fb_scratch(t, 192 + FB_CTL_MAX);
    lx_msghdr_t lm;
    memset(&lm, 0, sizeof lm);
    lm.iov = bm.iov;
    lm.iovlen = (uint64_t)bm.iovlen;
    if (bm.name && bm.namelen) {
        uint8_t nb[128];
        uint64_t nl;
        int64_t r = fb_sockaddr_conv(bm.name, bm.namelen, nb, &nl);
        if (r < 0) return r;
        if (syscall_copy_to_user((void *)(base + 64), nb, nl) < 0) return -EFAULT;
        lm.name = base + 64;
        lm.namelen = (uint32_t)nl;
    }
    if (bm.control && bm.controllen) {
        uint8_t *in = malloc(bm.controllen), *out = malloc(FB_CTL_MAX);
        if (!in || !out) { free(in); free(out); return -ENOMEM; }
        size_t ol = 0;
        int64_t r = syscall_copy_from_user(in, (const void *)bm.control, bm.controllen) < 0 ? -EFAULT
                  : fb_cmsg_to_linux(in, bm.controllen, out, &ol);
        if (r == 0 && ol && syscall_copy_to_user((void *)(base + 192), out, ol) < 0) r = -EFAULT;
        free(in);
        free(out);
        if (r < 0) return r;
        lm.control = ol ? base + 192 : 0;
        lm.controllen = ol;
    }
    if (syscall_copy_to_user((void *)base, &lm, sizeof lm) < 0) return -EFAULT;
    return sys_sendmsg(a1, base, fb_msgflags(a3));
}

static int64_t fb_recvmsg(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    fb_msghdr_t bm;
    if (syscall_copy_from_user(&bm, (const void *)a2, sizeof bm) < 0) return -EFAULT;
    if (bm.iovlen < 0) return -EINVAL;
    uint32_t ctlcap = bm.controllen > FB_CTL_MAX ? FB_CTL_MAX : bm.controllen;
    uint64_t base = fb_scratch(t, 192 + FB_CTL_MAX);
    lx_msghdr_t lm;
    memset(&lm, 0, sizeof lm);
    lm.iov = bm.iov;
    lm.iovlen = (uint64_t)bm.iovlen;
    if (bm.name && bm.namelen) {
        lm.name = base + 64;
        lm.namelen = 128;
    }
    if (bm.control && ctlcap) {
        lm.control = base + 192;
        lm.controllen = ctlcap;
    }
    if (syscall_copy_to_user((void *)base, &lm, sizeof lm) < 0) return -EFAULT;
    int64_t r = sys_recvmsg(a1, base, fb_msgflags(a3));
    if (r < 0) return r;
    if (syscall_copy_from_user(&lm, (const void *)base, sizeof lm) < 0) return -EFAULT;
    if (bm.name && bm.namelen) {
        int64_t e = fb_sockaddr_out(base + 64, lm.namelen, bm.name, a2 + 8);
        if (e < 0) return e;
    }
    uint32_t bctl = 0;
    if (bm.control && ctlcap && lm.controllen) {
        size_t ll = lm.controllen > FB_CTL_MAX ? FB_CTL_MAX : lm.controllen;
        uint8_t *in = malloc(ll), *out = malloc(ctlcap);
        if (!in || !out) { free(in); free(out); return -ENOMEM; }
        int64_t e = 0;
        if (syscall_copy_from_user(in, (const void *)(base + 192), ll) < 0) e = -EFAULT;
        else {
            bctl = (uint32_t)fb_cmsg_from_linux(in, ll, out, ctlcap);
            if (bctl && syscall_copy_to_user((void *)bm.control, out, bctl) < 0) e = -EFAULT;
        }
        free(in);
        free(out);
        if (e < 0) return e;
    }
    int32_t bf = 0;
    if (lm.flags & 0x1)  bf |= 0x1;
    if (lm.flags & 0x80) bf |= 0x8;
    if (lm.flags & 0x20) bf |= 0x10;
    if (lm.flags & 0x8)  bf |= 0x20;
    if (syscall_copy_to_user((void *)(a2 + 40), &bctl, 4) < 0 ||
        syscall_copy_to_user((void *)(a2 + 44), &bf, 4) < 0) return -EFAULT;
    return r;
}

static int fb_sockopt(uint64_t level, uint64_t name, uint64_t *llevel, uint64_t *lname)
{
    if (level == 0xffff) {
        *llevel = 1;
        switch (name) {
        case 0x1: *lname = 1; return 0;
        case 0x2: *lname = 30; return 0;
        case 0x4: *lname = 2; return 0;
        case 0x8: *lname = 9; return 0;
        case 0x10: *lname = 5; return 0;
        case 0x20: *lname = 6; return 0;
        case 0x80: *lname = 13; return 0;
        case 0x100: *lname = 10; return 0;
        case 0x200: *lname = 15; return 0;
        case 0x400: *lname = 29; return 0;
        case 0x800: return 1;
        case 0x1001: *lname = 7; return 0;
        case 0x1002: *lname = 8; return 0;
        case 0x1003: *lname = 19; return 0;
        case 0x1004: *lname = 18; return 0;
        case 0x1005: *lname = 21; return 0;
        case 0x1006: *lname = 20; return 0;
        case 0x1007: *lname = 4; return 0;
        case 0x1008: *lname = 3; return 0;
        case 0x1016: *lname = 38; return 0;
        case 0x1019: *lname = 39; return 0;
        }
        return -1;
    }
    *llevel = level;
    *lname = name;
    if (level == 6) {
        if (name == 0x100) *lname = 4;
        else if (name == 0x200) *lname = 5;
        else if (name == 0x400) *lname = 6;
    } else if (level == 0) {
        if (name == 3) *lname = 1;
        else if (name == 4) *lname = 2;
        else if (name == 2) *lname = 3;
    } else if (level == 41) {
        if (name == 27) *lname = 26;
    }
    return 0;
}

static int64_t fb_setsockopt(A6)
{
    UNUSED6;
    uint64_t ll, ln;
    int k = fb_sockopt(a2, a3, &ll, &ln);
    if (k == 1) return 0;
    if (k < 0) return -ENOPROTOOPT;
    int64_t r = sys_setsockopt(a1, ll, ln, a4, a5);
    return r == -ENOPROTOOPT && ll != 1 ? 0 : r;
}

static int64_t fb_getsockopt(A6)
{
    UNUSED6;
    uint64_t ll, ln;
    int k = fb_sockopt(a2, a3, &ll, &ln);
    if (k == 1) {
        int32_t z = 0, sz = 4;
        if (syscall_copy_to_user((void *)a4, &z, 4) < 0 || syscall_copy_to_user((void *)a5, &sz, 4) < 0) return -EFAULT;
        return 0;
    }
    if (k < 0) return -ENOPROTOOPT;
    int64_t r = sys_getsockopt(a1, ll, ln, a4, a5);
    if (r < 0 || ll != 1) return r;
    int32_t v;
    if (ln == 4) {
        if (syscall_copy_from_user(&v, (const void *)a4, 4) < 0) return -EFAULT;
        v = v ? (int32_t)fbsd_errno_from_linux(v) : 0;
        return syscall_copy_to_user((void *)a4, &v, 4);
    }
    if (ln == 39) {
        if (syscall_copy_from_user(&v, (const void *)a4, 4) < 0) return -EFAULT;
        if (v == 10) v = 28;
        return syscall_copy_to_user((void *)a4, &v, 4);
    }
    return r;
}

static int64_t fb_poll(A6)
{
    UNUSED6;
    return sys_poll(a1, a2, I(a3));
}

static int64_t fb_ppoll(A6)
{
    UNUSED6;
    uint64_t s = 0;
    if (a4) {
        int64_t r = fb_mask_in(a4, &s);
        if (r < 0) return r;
    }
    return sys_ppoll(a1, a2, a3, s, 8);
}

static int64_t fb_pselect(A6)
{
    UNUSED6;
    uint64_t s = 0;
    if (a6) {
        int64_t r = fb_mask_in(a6, &s);
        if (r < 0) return r;
    }
    return sys_pselect(a1, a2, a3, a4, a5, s);
}

typedef struct {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t  c_cc[32];
} cervus_termios_t;

typedef struct {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t  c_cc[20];
    uint32_t c_ispeed, c_ospeed;
} fb_termios_t;

_Static_assert(sizeof(fb_termios_t) == 44, "FreeBSD termios is 44 bytes");

static const uint32_t g_tio_iflag[][2] = {
    { 0x1, 0x1 }, { 0x2, 0x2 }, { 0x4, 0x4 }, { 0x8, 0x8 }, { 0x10, 0x10 }, { 0x20, 0x20 },
    { 0x40, 0x40 }, { 0x80, 0x80 }, { 0x100, 0x100 }, { 0x200, 0x400 }, { 0x400, 0x1000 },
    { 0x800, 0x800 }, { 0x2000, 0x2000 }, { 0x4000, 0x4000 },
};
static const uint32_t g_tio_oflag[][2] = {
    { 0x1, 0x1 }, { 0x2, 0x4 }, { 0x4, 0x1800 }, { 0x10, 0x8 }, { 0x20, 0x10 }, { 0x40, 0x20 },
};
static const uint32_t g_tio_cflag[][2] = {
    { 0x400, 0x40 }, { 0x800, 0x80 }, { 0x1000, 0x100 }, { 0x2000, 0x200 }, { 0x4000, 0x400 },
    { 0x8000, 0x800 }, { 0x30000, 0x80000000 },
};
static const uint32_t g_tio_lflag[][2] = {
    { 0x1, 0x800 }, { 0x2, 0x10 }, { 0x4, 0x20 }, { 0x8, 0x8 }, { 0x10, 0x40 }, { 0x20, 0x400 },
    { 0x40, 0x200 }, { 0x80, 0x1 }, { 0x100, 0x2 }, { 0x400, 0x8000 }, { 0x400000, 0x100 },
    { 0x800000, 0x1000 }, { 0x20000000, 0x4000 }, { 0x80000000, 0x80 },
};
static const int8_t g_tio_cc[][2] = {
    { 0, 4 }, { 1, 11 }, { 2, 16 }, { 3, 2 }, { 4, 14 }, { 5, 3 }, { 6, 12 }, { 8, 0 },
    { 9, 1 }, { 10, 10 }, { 12, 8 }, { 13, 9 }, { 14, 15 }, { 15, 13 }, { 16, 6 }, { 17, 5 },
};

static uint32_t tio_map(const uint32_t (*tab)[2], size_t n, uint32_t v, int from, int to)
{
    uint32_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if ((v & tab[i][from]) == tab[i][from]) o |= tab[i][to];
    }
    return o;
}

#define TIO_MAP(tab, v, from, to) tio_map(tab, sizeof tab / sizeof tab[0], (v), (from), (to))

static void fb_termios_from(fb_termios_t *b, const cervus_termios_t *c)
{
    memset(b, 0, sizeof *b);
    b->c_iflag = TIO_MAP(g_tio_iflag, c->c_iflag, 1, 0);
    b->c_oflag = TIO_MAP(g_tio_oflag, c->c_oflag, 1, 0);
    b->c_cflag = TIO_MAP(g_tio_cflag, c->c_cflag, 1, 0) | (((c->c_cflag >> 4) & 3) << 8);
    b->c_lflag = TIO_MAP(g_tio_lflag, c->c_lflag, 1, 0);
    for (size_t i = 0; i < sizeof g_tio_cc / sizeof g_tio_cc[0]; i++)
        b->c_cc[g_tio_cc[i][0]] = c->c_cc[g_tio_cc[i][1]];
    b->c_cc[7] = 0xff;
    b->c_cc[11] = 0xff;
    b->c_cc[18] = 0xff;
    b->c_ispeed = b->c_ospeed = 38400;
}

static void fb_termios_to(cervus_termios_t *c, const fb_termios_t *b)
{
    uint32_t keep_c = c->c_cflag & 0x100f;
    c->c_iflag = TIO_MAP(g_tio_iflag, b->c_iflag, 0, 1);
    c->c_oflag = TIO_MAP(g_tio_oflag, b->c_oflag, 0, 1);
    c->c_cflag = TIO_MAP(g_tio_cflag, b->c_cflag, 0, 1) | (((b->c_cflag >> 8) & 3) << 4) | keep_c;
    c->c_lflag = TIO_MAP(g_tio_lflag, b->c_lflag, 0, 1);
    for (size_t i = 0; i < sizeof g_tio_cc / sizeof g_tio_cc[0]; i++)
        c->c_cc[g_tio_cc[i][1]] = b->c_cc[g_tio_cc[i][0]];
}

static int64_t fb_termios_ioctl(uint64_t fd, uint64_t req, uint64_t arg)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    cervus_termios_t ct;
    memset(&ct, 0, sizeof ct);
    int64_t r = vfs_ioctl(f, 0x5401, &ct);
    if (r >= 0) {
        fb_termios_t bt;
        if (req == 0x402c7413) {
            fb_termios_from(&bt, &ct);
            r = syscall_copy_to_user((void *)arg, &bt, sizeof bt);
        } else if (syscall_copy_from_user(&bt, (const void *)arg, sizeof bt) < 0) {
            r = -EFAULT;
        } else {
            fb_termios_to(&ct, &bt);
            uint64_t lreq = req == 0x802c7414 ? 0x5402 : req == 0x802c7415 ? 0x5403 : 0x5404;
            r = vfs_ioctl(f, lreq, &ct);
        }
    }
    fd_put(f);
    return r;
}

static int64_t fb_ioctl(A6)
{
    UNUSED6;
    uint64_t req = a2 & 0xffffffffULL;
    switch (req) {
    case 0x402c7413: case 0x802c7414: case 0x802c7415: case 0x802c7416:
        return fb_termios_ioctl(a1, req, a3);
    case 0x40087468: return sys_ioctl(a1, 0x5413, a3);
    case 0x80087467: return sys_ioctl(a1, 0x5414, a3);
    case 0x40047477: return sys_ioctl(a1, 0x540F, a3);
    case 0x80047476: return sys_ioctl(a1, 0x5410, a3);
    case 0x4004667f: return sys_ioctl(a1, 0x541B, a3);
    case 0x8004667e: return sys_ioctl(a1, 0x5421, a3);
    case 0x20006601: return sys_ioctl(a1, 0x5451, 0);
    case 0x20006602: return sys_ioctl(a1, 0x5450, 0);
    case 0x8004667d: return sys_ioctl(a1, 0x5452, a3);
    case 0x20007461: return sys_ioctl(a1, 0x540E, 0);
    case 0x20007471: return sys_ioctl(a1, 0x5422, 0);
    case 0x40047463: return sys_ioctl(a1, 0x5429, a3);
    case 0x40047473: return sys_ioctl(a1, 0x5411, a3);
    case 0x80017472: return sys_ioctl(a1, 0x5412, a3);
    case 0x2000740d: return sys_ioctl(a1, 0x540C, 0);
    case 0x2000740e: return sys_ioctl(a1, 0x540D, 0);
    case 0x2000745e: return sys_ioctl(a1, 0x5409, 1);
    case 0x4004740f: return sys_ioctl(a1, 0x80045430, a3);
    case 0x2000741c: return 0;
    case 0x80047410: {
        int32_t which = 0;
        if (a3 && syscall_copy_from_user(&which, (const void *)a3, 4) < 0) return -EFAULT;
        uint64_t q = (which & 3) == 1 ? 0 : (which & 3) == 2 ? 1 : 2;
        return sys_ioctl(a1, 0x540B, q);
    }
    case 0x4004667a: {
        vfs_stat_t st;
        int r = fb_fd_stat(a1, &st);
        if (r < 0) return r;
        int32_t v = st.st_type == VFS_NODE_CHARDEV ? 0x0004 : 0;
        return syscall_copy_to_user((void *)a3, &v, 4);
    }
    }
    return -ENOTTY;
}

static int64_t fb_specialfd(A6)
{
    UNUSED6;
    if (a1 != 1) return -EINVAL;
    struct { uint32_t initval; int32_t flags; } e;
    if (a3 < sizeof e || syscall_copy_from_user(&e, (const void *)a2, sizeof e) < 0) return -EFAULT;
    uint64_t lf = 0;
    if (e.flags & 0x100000) lf |= O_CLOEXEC;
    if (e.flags & 0x4) lf |= O_NONBLOCK;
    if (e.flags & 0x1) lf |= 1;
    return sys_eventfd(e.initval, lf, 0);
}

static int64_t fb_posix_openpt(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    static const char ptmx[] = "/dev/ptmx";
    uint64_t s = fb_scratch(t, sizeof ptmx);
    if (syscall_copy_to_user((void *)s, ptmx, sizeof ptmx) < 0) return -EFAULT;
    return sys_openat((uint64_t)(int64_t)FB_AT_FDCWD, s, fb_oflags(a1), 0);
}

static void fb_hostname(char *out, size_t cap)
{
    strncpy(out, "cervus", cap - 1);
    out[cap - 1] = 0;
    vfs_file_t *f = NULL;
    if (vfs_open("/etc/hostname", 0, 0, &f) < 0 || !f) return;
    char buf[64];
    int64_t n = vfs_read(f, buf, sizeof buf - 1);
    vfs_close(f);
    if (n <= 0) return;
    buf[n] = 0;
    for (int64_t i = 0; i < n; i++) if (buf[i] == '\n' || buf[i] == '\r') { buf[i] = 0; break; }
    if (buf[0]) { strncpy(out, buf, cap - 1); out[cap - 1] = 0; }
}

enum {
    SC_STR, SC_INT, SC_LONG, SC_FN,
};

typedef struct fb_sysctl fb_sysctl_t;
typedef int64_t (*fb_sysctl_fn)(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest,
                                uint8_t *buf, size_t *len);

struct fb_sysctl {
    const char  *name;
    int32_t      mib[4];
    uint8_t      nmib;
    uint8_t      kind;
    const char  *s;
    int64_t      v;
    fb_sysctl_fn fn;
    uint32_t     ctltype;
    const char  *fmt;
};

static void sc_put(uint8_t *buf, size_t *len, size_t at, const void *src, size_t n)
{
    if (at + n <= *len) memcpy(buf + at, src, n);
}

static int64_t sc_hostname(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest;
    char h[256];
    fb_hostname(h, sizeof h);
    size_t n = strlen(h) + 1;
    sc_put(buf, len, 0, h, n);
    *len = n;
    return 0;
}

static int64_t sc_ncpu(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)rest; (void)nrest;
    int32_t v = (int32_t)smp_get_cpu_count() + (int32_t)e->v;
    sc_put(buf, len, 0, &v, 4);
    *len = 4;
    return 0;
}

static int64_t sc_physmem(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)rest; (void)nrest;
    uint64_t total = (uint64_t)pmm_get_usable_pages() * PAGE_SIZE;
    uint64_t used = (uint64_t)pmm_get_used_pages() * PAGE_SIZE;
    uint64_t v = e->v == 1 ? (total > used ? total - used : 0) : e->v == 2 ? total / PAGE_SIZE : total;
    sc_put(buf, len, 0, &v, 8);
    *len = 8;
    return 0;
}

static int64_t sc_boottime(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest;
    int64_t bt = clock_realtime_offset_ns();
    int64_t tv[2] = { bt / 1000000000LL, (bt % 1000000000LL) / 1000 };
    sc_put(buf, len, 0, tv, sizeof tv);
    *len = sizeof tv;
    return 0;
}

static int64_t sc_arandom(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest;
    size_t n = *len > 256 ? 256 : *len;
    random_bytes(buf, n);
    *len = n;
    return 0;
}

static int64_t sc_pagesizes(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest;
    uint64_t ps[3] = { 4096, 2ULL << 20, 1ULL << 30 };
    sc_put(buf, len, 0, ps, sizeof ps);
    *len = sizeof ps;
    return 0;
}

static int64_t sc_model(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest;
    uint32_t r[12];
    for (uint32_t i = 0; i < 3; i++)
        __asm__ volatile("cpuid" : "=a"(r[i * 4]), "=b"(r[i * 4 + 1]), "=c"(r[i * 4 + 2]), "=d"(r[i * 4 + 3])
                         : "a"(0x80000002u + i), "c"(0));
    char str[49];
    memcpy(str, r, 48);
    str[48] = 0;
    char *p = str;
    while (*p == ' ') p++;
    size_t n = strlen(p) + 1;
    sc_put(buf, len, 0, p, n);
    *len = n;
    return 0;
}

static int64_t sc_vfsconf(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest;
    size_t n = sizeof g_fb_vfs / sizeof g_fb_vfs[0];
    for (size_t i = 0; i < n; i++) {
        uint8_t x[48];
        memset(x, 0, sizeof x);
        strncpy((char *)x + 8, g_fb_vfs[i], 15);
        int32_t num = (int32_t)i + 1;
        memcpy(x + 24, &num, 4);
        sc_put(buf, len, i * 48, x, 48);
    }
    *len = n * 48;
    return 0;
}

#define KP_SIZE 1088

static void kp_fill(uint8_t *k, task_t *t)
{
    memset(k, 0, KP_SIZE);
    int32_t i32;
    i32 = KP_SIZE; memcpy(k + 0, &i32, 4);
    i32 = 1; memcpy(k + 4, &i32, 4);
    i32 = (int32_t)task_tgid(t); memcpy(k + 72, &i32, 4);
    i32 = (int32_t)t->ppid; memcpy(k + 76, &i32, 4);
    i32 = (int32_t)t->pgid; memcpy(k + 80, &i32, 4);
    i32 = (int32_t)t->pgid; memcpy(k + 84, &i32, 4);
    i32 = (int32_t)t->sid; memcpy(k + 88, &i32, 4);
    i32 = (int32_t)t->sid; memcpy(k + 92, &i32, 4);
    uint32_t u32 = 0xffffffffu; memcpy(k + 100, &u32, 4);
    u32 = t->uid; memcpy(k + 168, &u32, 4); memcpy(k + 172, &u32, 4); memcpy(k + 176, &u32, 4);
    u32 = t->gid; memcpy(k + 180, &u32, 4); memcpy(k + 184, &u32, 4); memcpy(k + 192, &u32, 4);
    int16_t ng = 1; memcpy(k + 188, &ng, 2);
    uint64_t vsz = t->mm ? mm_vm_bytes(t->mm) : 0;
    memcpy(k + 256, &vsz, 8);
    int64_t rss = t->mm ? (int64_t)mm_rss_pages(t->mm) : 0;
    memcpy(k + 264, &rss, 8);
    uint64_t rt_us = t->total_runtime / 1000ULL;
    memcpy(k + 328, &rt_us, 8);
    int64_t start_ns = clock_realtime_offset_ns() + (int64_t)t->create_time_ns;
    int64_t st[2] = { start_ns / 1000000000LL, (start_ns % 1000000000LL) / 1000 };
    memcpy(k + 336, st, sizeof st);
    int64_t flag = 0x10000000 | 0x4000 | (t->ctty >= 0 ? 0x2 : 0);
    memcpy(k + 368, &flag, 8);
    char stat = t->state == TASK_ZOMBIE || t->state == TASK_DEAD ? 5 :
                t->state == TASK_BLOCKED ? 3 : 2;
    k[388] = (uint8_t)stat;
    k[389] = (uint8_t)(int8_t)t->nice;
    strncpy((char *)k + 394, t->name, 16);
    if (t->state == TASK_BLOCKED) memcpy(k + 411, "wait", 5);
    memcpy(k + 420, t->uid == 0 ? "root" : "user", 5);
    strncpy((char *)k + 447, t->name, 19);
    memcpy(k + 467, t->abi == TASK_ABI_FREEBSD ? "FreeBSD ELF64" : t->abi == TASK_ABI_LINUX ? "Linux ELF64" : "Cervus ELF64", 14);
    uint64_t nodev = ~0ULL; memcpy(k + 560, &nodev, 8);
    i32 = (int32_t)t->cpu_id; memcpy(k + 568, &i32, 4); memcpy(k + 572, &i32, 4);
    int32_t nth = 1;
    memcpy(k + 596, &nth, 4);
    i32 = (int32_t)t->pid; memcpy(k + 600, &i32, 4);
    k[604] = 3; k[605] = 120; k[606] = 120; k[607] = 120;
    int64_t sflag = 1; memcpy(k + 1072, &sflag, 8);
    int64_t tdflags = t->state == TASK_BLOCKED ? 0x8 : 0;
    memcpy(k + 1080, &tdflags, 8);
}

static int64_t sc_proc(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e;
    if (nrest < 1) return -ENOENT;
    int32_t what = rest[0] & ~0x10;
    int32_t arg = nrest > 1 ? rest[1] : 0;
    task_t *me = syscall_cur_task();
    if (what == 12 || what == 7 || what == 40 || what == 39) {
        task_t *t = arg == -1 || (me && arg == (int32_t)task_tgid(me)) ? me : task_find_by_pid((uint32_t)arg);
        if (!t) return -ESRCH;
        if (what == 12) {
            size_t n = strnlen(t->exe, sizeof t->exe);
            sc_put(buf, len, 0, t->exe, n);
            sc_put(buf, len, n, "", 1);
            *len = n + 1;
            return 0;
        }
        if (what == 40) {
            int32_t v = FB_OSRELDATE;
            sc_put(buf, len, 0, &v, 4);
            *len = 4;
            return 0;
        }
        if (what == 39) {
            uint16_t v = (uint16_t)t->umask;
            sc_put(buf, len, 0, &v, 2);
            *len = 2;
            return 0;
        }
        if (!t->mm) { *len = 0; return 0; }
        size_t n = t->mm->arg_end - t->mm->arg_start;
        if (n > 65536) n = 65536;
        if (n <= *len && mm_access_remote(t->mm, t->mm->arg_start, buf, n, 0) < 0) return -EFAULT;
        *len = n;
        return 0;
    }
    if (what != 0 && what != 1 && what != 2 && what != 3 && what != 5 && what != 6 && what != 8 && what != 10 && what != 11)
        return -EINVAL;
    uint32_t *pids = malloc(1024 * sizeof *pids);
    if (!pids) return -ENOMEM;
    int np = task_collect_pids(pids, 1024);
    uint8_t *k = malloc(KP_SIZE);
    if (!k) { free(pids); return -ENOMEM; }
    size_t at = 0;
    for (int i = 0; i < np; i++) {
        task_t *t = task_find_by_pid(pids[i]);
        if (!t || task_leader(t) != t || !t->is_userspace) continue;
        if (what == 1 && (int32_t)task_tgid(t) != arg) continue;
        if (what == 2 && (int32_t)t->pgid != arg) continue;
        if (what == 3 && (int32_t)t->sid != arg) continue;
        if ((what == 5 || what == 6) && (int32_t)t->uid != arg) continue;
        if ((what == 10 || what == 11) && (int32_t)t->gid != arg) continue;
        kp_fill(k, t);
        sc_put(buf, len, at, k, KP_SIZE);
        at += KP_SIZE;
    }
    free(k);
    free(pids);
    if (what == 1 && at == 0) return -ESRCH;
    *len = at;
    return 0;
}

static int64_t sysctl_reply(const void *data, size_t len, uint64_t oldp, uint64_t oldlenp);

static size_t rt_sin(uint8_t *o, uint32_t host_ip)
{
    memset(o, 0, 16);
    o[0] = 16;
    o[1] = 2;
    uint32_t be = __builtin_bswap32(host_ip);
    memcpy(o + 4, &be, 4);
    return 16;
}

static void rt_ifdata(uint8_t *d, const netdev_t *nd)
{
    memset(d, 0, 152);
    d[0] = nd->is_loopback ? 0x18 : 6;
    d[2] = nd->is_loopback ? 0 : 6;
    d[3] = nd->is_loopback ? 0 : 14;
    d[4] = nd->link_up || nd->is_loopback ? 2 : 1;
    uint16_t dl = 152;
    memcpy(d + 6, &dl, 2);
    uint32_t mtu = nd->mtu;
    memcpy(d + 8, &mtu, 4);
    uint64_t c[4] = { nd->rx_packets, 0, nd->tx_packets, 0 };
    memcpy(d + 24, &c[0], 8);
    memcpy(d + 40, &c[2], 8);
    memcpy(d + 64, &nd->rx_bytes, 8);
    memcpy(d + 72, &nd->tx_bytes, 8);
}

static int64_t fb_rt_iflist(int af, int want, int lform, uint8_t *buf, size_t *len)
{
    netdev_t *list[16];
    int nl = 0;
    netdev_t *lo = netdev_loopback();
    if (lo) list[nl++] = lo;
    for (netdev_t *d = netdev_first(); d && nl < 16; d = d->next)
        if (d != lo) list[nl++] = d;
    size_t at = 0;
    for (int i = 0; i < nl; i++) {
        netdev_t *nd = list[i];
        int idx = i + 1;
        if (want && want != idx) continue;
        int32_t flags = 0x1 | 0x40 | 0x8000 | (nd->is_loopback ? 0x8 : 0x2 | 0x800);
        uint8_t m[256];
        memset(m, 0, sizeof m);
        size_t hl = lform ? 176 : 168;
        size_t doff = lform ? 24 : 16;
        uint8_t *sdl = m + hl;
        size_t nlen = strnlen(nd->name, NETDEV_NAME_MAX);
        sdl[0] = 54;
        sdl[1] = 18;
        uint16_t ix = (uint16_t)idx;
        memcpy(sdl + 2, &ix, 2);
        sdl[4] = nd->is_loopback ? 0x18 : 6;
        sdl[5] = (uint8_t)nlen;
        sdl[6] = nd->is_loopback ? 0 : 6;
        memcpy(sdl + 8, nd->name, nlen);
        if (!nd->is_loopback) memcpy(sdl + 8 + nlen, nd->mac, 6);
        uint16_t ml = (uint16_t)(hl + 56);
        memcpy(m, &ml, 2);
        m[2] = 5;
        m[3] = 14;
        int32_t addrs = 0x10;
        memcpy(m + 4, &addrs, 4);
        memcpy(m + 8, &flags, 4);
        memcpy(m + 12, &ix, 2);
        if (lform) {
            uint16_t l16 = (uint16_t)hl, o16 = (uint16_t)doff;
            memcpy(m + 16, &l16, 2);
            memcpy(m + 18, &o16, 2);
        }
        rt_ifdata(m + doff, nd);
        sc_put(buf, len, at, m, ml);
        at += ml;
        if ((af == 0 || af == 2) && (nd->ip || nd->is_loopback)) {
            uint32_t ip = nd->is_loopback && !nd->ip ? 0x7F000001u : nd->ip;
            uint32_t mask = nd->is_loopback && !nd->netmask ? 0xFF000000u : nd->netmask;
            memset(m, 0, sizeof m);
            size_t ahl = lform ? 176 : 20;
            size_t p = ahl;
            int32_t aa = 0x4 | 0x20;
            p += rt_sin(m + p, mask);
            p += rt_sin(m + p, ip);
            if (!nd->is_loopback) {
                aa |= 0x80;
                p += rt_sin(m + p, (ip & mask) | ~mask);
            }
            uint16_t al = (uint16_t)p;
            memcpy(m, &al, 2);
            m[2] = 5;
            m[3] = 12;
            memcpy(m + 4, &aa, 4);
            memcpy(m + 8, &flags, 4);
            memcpy(m + 12, &ix, 2);
            if (lform) {
                uint16_t l16 = (uint16_t)ahl, o16 = 24;
                memcpy(m + 16, &l16, 2);
                memcpy(m + 18, &o16, 2);
                rt_ifdata(m + 24, nd);
            }
            sc_put(buf, len, at, m, al);
            at += al;
        }
    }
    *len = at;
    return 0;
}

static int64_t fb_sysctl_net(const int32_t *mib, uint32_t n, uint64_t oldp, uint64_t oldlenp, uint64_t newp)
{
    if (newp) return -EPERM;
    if (n < 6 || mib[1] != 17 || (mib[4] != 3 && mib[4] != 5)) return -ENOENT;
    size_t bufcap = 65536;
    uint8_t *buf = calloc(1, bufcap);
    if (!buf) return -ENOMEM;
    size_t len = bufcap;
    int64_t r = fb_rt_iflist(mib[3], mib[5], mib[4] == 5, buf, &len);
    if (r == 0) r = sysctl_reply(buf, len > bufcap ? bufcap : len, oldp, oldlenp);
    free(buf);
    return r;
}

static int64_t sc_cptime(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)rest; (void)nrest;
    uint32_t n = smp_get_cpu_count();
    if (!n) n = 1;
    if (n > 256) n = 256;
    uint64_t up = sched_now_ns();
    int64_t sum[5] = { 0, 0, 0, 0, 0 };
    size_t at = 0;
    for (uint32_t c = 0; c < n; c++) {
        uint64_t id = sched_cpu_idle_ns(c);
        if (id > up) id = up;
        int64_t v[5] = { (int64_t)((up - id) / 7812500ULL), 0, 0, 0, (int64_t)(id / 7812500ULL) };
        if (e->v) {
            sc_put(buf, len, at, v, sizeof v);
            at += sizeof v;
        }
        for (int k = 0; k < 5; k++) sum[k] += v[k];
    }
    if (!e->v) {
        sc_put(buf, len, 0, sum, sizeof sum);
        at = sizeof sum;
    }
    *len = at;
    return 0;
}

static int64_t sc_loadavg(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest;
    uint32_t pids[256];
    int n = task_collect_pids(pids, 256);
    uint32_t runnable = 0;
    for (int i = 0; i < n; i++) {
        task_t *t = task_find_by_pid(pids[i]);
        if (t && t->is_userspace && (t->state == TASK_RUNNING || t->state == TASK_READY)) runnable++;
    }
    struct { uint32_t ld[3]; uint32_t pad; int64_t fscale; } la = { { runnable * 2048, runnable * 2048, runnable * 2048 }, 0, 2048 };
    sc_put(buf, len, 0, &la, sizeof la);
    *len = sizeof la;
    return 0;
}

static int64_t sc_vmstat(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)rest; (void)nrest;
    uint64_t total = pmm_get_usable_pages(), used = pmm_get_used_pages();
    if (used > total) used = total;
    uint64_t v = 0;
    switch (e->v) {
    case 1: v = used; break;
    case 5: v = total - used; break;
    case 8: v = total; break;
    }
    if (e->ctltype == 9) {
        sc_put(buf, len, 0, &v, 8);
        *len = 8;
    } else {
        uint32_t w = (uint32_t)v;
        sc_put(buf, len, 0, &w, 4);
        *len = 4;
    }
    return 0;
}

static int64_t sc_lastpid(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest;
    uint32_t pids[1024];
    int n = task_collect_pids(pids, 1024);
    int32_t mx = 0;
    for (int i = 0; i < n; i++) if ((int32_t)pids[i] > mx) mx = (int32_t)pids[i];
    sc_put(buf, len, 0, &mx, 4);
    *len = 4;
    return 0;
}

static int64_t sc_noent(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest; (void)buf; (void)len;
    return -ENOENT;
}

static int64_t sc_clockrate(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    (void)e; (void)rest; (void)nrest;
    int32_t ci[5] = { 1000, 1000, 0, 128, 128 };
    sc_put(buf, len, 0, ci, sizeof ci);
    *len = sizeof ci;
    return 0;
}

#define CT_NODE   1
#define CT_INT    2
#define CT_STRING 3
#define CT_OPAQUE 5
#define CT_LONG   7
#define CT_ULONG  8

static const fb_sysctl_t g_sysctl[] = {
    { "kern.ostype",       { 1, 1 },  2, SC_STR,  "FreeBSD", 0, 0, CT_STRING, "A" },
    { "kern.osrelease",    { 1, 2 },  2, SC_STR,  "15.1-RELEASE", 0, 0, CT_STRING, "A" },
    { "kern.osrevision",   { 1, 3 },  2, SC_INT,  0, 199506, 0, CT_INT, "I" },
    { "kern.version",      { 1, 4 },  2, SC_STR,  "FreeBSD 15.1-RELEASE Cervus 0.0.2\n", 0, 0, CT_STRING, "A" },
    { "kern.maxproc",      { 1, 6 },  2, SC_INT,  0, 32768, 0, CT_INT, "I" },
    { "kern.maxfiles",     { 1, 7 },  2, SC_INT,  0, 65536, 0, CT_INT, "I" },
    { "kern.argmax",       { 1, 8 },  2, SC_INT,  0, 2097152, 0, CT_INT, "I" },
    { "kern.securelevel",  { 1, 9 },  2, SC_INT,  0, -1, 0, CT_INT, "I" },
    { "kern.hostname",     { 1, 10 }, 2, SC_FN,   0, 0, sc_hostname, CT_STRING, "A" },
    { "kern.hostid",       { 1, 11 }, 2, SC_INT,  0, 0, 0, CT_INT, "IU" },
    { "kern.proc",         { 1, 14 }, 2, SC_FN,   0, 0, sc_proc, CT_NODE, "N" },
    { "kern.ngroups",      { 1, 18 }, 2, SC_INT,  0, 1023, 0, CT_INT, "I" },
    { "kern.boottime",     { 1, 21 }, 2, SC_FN,   0, 0, sc_boottime, CT_OPAQUE, "S,timeval" },
    { "kern.domainname",   { 1, 22 }, 2, SC_STR,  "", 0, 0, CT_STRING, "A" },
    { "kern.osreldate",    { 1, 24 }, 2, SC_INT,  0, FB_OSRELDATE, 0, CT_INT, "I" },
    { "kern.usrstack",     { 1, 33 }, 2, SC_LONG, 0, (int64_t)MM_STACK_TOP, 0, CT_ULONG, "LU" },
    { "kern.iov_max",      { 1, 35 }, 2, SC_INT,  0, 1024, 0, CT_INT, "I" },
    { "kern.arandom",      { 1, 37 }, 2, SC_FN,   0, 0, sc_arandom, CT_OPAQUE, "" },
    { "vm.overcommit",     { 2, 12 }, 2, SC_INT,  0, 0, 0, CT_INT, "I" },
    { "hw.machine",        { 6, 1 },  2, SC_STR,  "amd64", 0, 0, CT_STRING, "A" },
    { "hw.model",          { 6, 2 },  2, SC_FN,   0, 0, sc_model, CT_STRING, "A" },
    { "hw.ncpu",           { 6, 3 },  2, SC_FN,   0, 0, sc_ncpu, CT_INT, "I" },
    { "hw.byteorder",      { 6, 4 },  2, SC_INT,  0, 1234, 0, CT_INT, "I" },
    { "hw.physmem",        { 6, 5 },  2, SC_FN,   0, 0, sc_physmem, CT_ULONG, "LU" },
    { "hw.usermem",        { 6, 6 },  2, SC_FN,   0, 1, sc_physmem, CT_ULONG, "LU" },
    { "hw.pagesize",       { 6, 7 },  2, SC_INT,  0, 4096, 0, CT_INT, "I" },
    { "hw.floatingpoint",  { 6, 10 }, 2, SC_INT,  0, 1, 0, CT_INT, "I" },
    { "hw.machine_arch",   { 6, 11 }, 2, SC_STR,  "amd64", 0, 0, CT_STRING, "A" },
    { "hw.realmem",        { 6, 12 }, 2, SC_FN,   0, 0, sc_physmem, CT_ULONG, "LU" },
    { "user.cs_path",      { 8, 1 },  2, SC_STR,  "/usr/bin:/bin:/usr/sbin:/sbin", 0, 0, CT_STRING, "A" },
    { "user.localbase",    { 8, 21 }, 2, SC_STR,  "/usr/local", 0, 0, CT_STRING, "A" },
    { "kern.smp.cpus",     { 0x7fff, 1 }, 2, SC_FN, 0, 0, sc_ncpu, CT_INT, "I" },
    { "kern.smp.maxcpus",  { 0x7fff, 2 }, 2, SC_FN, 0, 0, sc_ncpu, CT_INT, "I" },
    { "hw.pagesizes",      { 0x7fff, 3 }, 2, SC_FN, 0, 0, sc_pagesizes, CT_ULONG, "LU" },
    { "vm.pmap.pg_ps_enabled", { 0x7fff, 4 }, 2, SC_INT, 0, 0, 0, CT_INT, "I" },
    { "hw.availpages",     { 0x7fff, 5 }, 2, SC_FN, 0, 2, sc_physmem, CT_ULONG, "LU" },
    { "kern.ipc.somaxconn", { 0x7fff, 6 }, 2, SC_INT, 0, 128, 0, CT_INT, "I" },
    { "kern.ps_strings",   { 0x7fff, 7 }, 2, SC_LONG, 0, 0, 0, CT_ULONG, "LU" },
    { "security.bsd.unprivileged_proc_debug", { 0x7fff, 8 }, 2, SC_INT, 0, 1, 0, CT_INT, "I" },
    { "kern.features.inet6", { 0x7fff, 9 }, 2, SC_INT, 0, 1, 0, CT_INT, "I" },
    { "kern.features.inet", { 0x7fff, 10 }, 2, SC_INT, 0, 1, 0, CT_INT, "I" },
    { "kern.module_path",  { 0x7fff, 11 }, 2, SC_STR, "/boot/kernel", 0, 0, CT_STRING, "A" },
    { "kern.hostuuid",     { 0x7fff, 12 }, 2, SC_STR, "00000000-0000-0000-0000-000000000000", 0, 0, CT_STRING, "A" },
    { "kern.coredump",     { 0x7fff, 13 }, 2, SC_INT, 0, 0, 0, CT_INT, "I" },
    { "vfs.usermount",     { 0x7fff, 14 }, 2, SC_INT, 0, 0, 0, CT_INT, "I" },
    { "kern.vm_guest",     { 0x7fff, 15 }, 2, SC_STR, "none", 0, 0, CT_STRING, "A" },
    { "vfs.conflist",      { 0x7fff, 16 }, 2, SC_FN, 0, 0, sc_vfsconf, CT_OPAQUE, "S,xvfsconf" },
    { "kern.pid_max",      { 0x7fff, 17 }, 2, SC_INT, 0, 99999, 0, CT_INT, "I" },
    { "kern.fscale",       { 0x7fff, 18 }, 2, SC_INT, 0, 2048, 0, CT_INT, "I" },
    { "kern.ccpu",         { 0x7fff, 19 }, 2, SC_INT, 0, 1948, 0, CT_INT, "I" },
    { "kern.lastpid",      { 0x7fff, 20 }, 2, SC_FN, 0, 0, sc_lastpid, CT_INT, "I" },
    { "vm.stats.vm.v_page_size", { 0x7fff, 21 }, 2, SC_INT, 0, 4096, 0, CT_INT, "IU" },
    { "vm.stats.vm.v_page_count", { 0x7fff, 22 }, 2, SC_FN, 0, 8, sc_vmstat, CT_INT, "IU" },
    { "kern.cp_time",      { 0x7fff, 23 }, 2, SC_FN, 0, 0, sc_cptime, CT_LONG, "L" },
    { "kern.cp_times",     { 0x7fff, 24 }, 2, SC_FN, 0, 1, sc_cptime, CT_LONG, "L" },
    { "vm.loadavg",        { 2, 2 },      2, SC_FN, 0, 0, sc_loadavg, CT_OPAQUE, "S,loadavg" },
    { "kern.smp.active",   { 0x7fff, 25 }, 2, SC_INT, 0, 1, 0, CT_INT, "I" },
    { "vm.nswapdev",       { 0x7fff, 26 }, 2, SC_INT, 0, 0, 0, CT_INT, "I" },
    { "vfs.bufspace",      { 0x7fff, 27 }, 2, SC_LONG, 0, 0, 0, CT_LONG, "L" },
    { "vm.stats.vm.v_active_count", { 0x7fff, 28 }, 2, SC_FN, 0, 1, sc_vmstat, CT_INT, "IU" },
    { "vm.stats.vm.v_inactive_count", { 0x7fff, 29 }, 2, SC_FN, 0, 2, sc_vmstat, CT_INT, "IU" },
    { "vm.stats.vm.v_laundry_count", { 0x7fff, 30 }, 2, SC_FN, 0, 3, sc_vmstat, CT_INT, "IU" },
    { "vm.stats.vm.v_wire_count", { 0x7fff, 31 }, 2, SC_FN, 0, 4, sc_vmstat, CT_INT, "IU" },
    { "vm.stats.vm.v_free_count", { 0x7fff, 32 }, 2, SC_FN, 0, 5, sc_vmstat, CT_INT, "IU" },
    { "vm.stats.vm.v_swappgsin", { 0x7fff, 33 }, 2, SC_FN, 0, 6, sc_vmstat, 9, "QU" },
    { "vm.stats.vm.v_swappgsout", { 0x7fff, 34 }, 2, SC_FN, 0, 7, sc_vmstat, 9, "QU" },
    { "kern.clockrate",    { 1, 12 },     2, SC_FN, 0, 0, sc_clockrate, CT_OPAQUE, "S,clockinfo" },
    { "kern.smp.maxid",    { 0x7fff, 35 }, 2, SC_FN, 0, -1, sc_ncpu, CT_INT, "I" },
    { "vm.dmmax",          { 0x7fff, 36 }, 2, SC_INT, 0, 32, 0, CT_INT, "I" },
    { "vm.swap_info",      { 0x7fff, 37 }, 2, SC_FN, 0, 0, sc_noent, CT_NODE, "N" },
};

#define SYSCTL_N (sizeof g_sysctl / sizeof g_sysctl[0])

static int64_t sysctl_value(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest, uint8_t *buf, size_t *len)
{
    switch (e->kind) {
    case SC_STR: {
        size_t n = strlen(e->s) + 1;
        sc_put(buf, len, 0, e->s, n);
        *len = n;
        return 0;
    }
    case SC_INT: {
        int32_t v = (int32_t)e->v;
        sc_put(buf, len, 0, &v, 4);
        *len = 4;
        return 0;
    }
    case SC_LONG:
        sc_put(buf, len, 0, &e->v, 8);
        *len = 8;
        return 0;
    }
    return e->fn(e, rest, nrest, buf, len);
}

static int64_t sysctl_reply(const void *data, size_t len, uint64_t oldp, uint64_t oldlenp)
{
    size_t cap = 0;
    if (oldlenp && syscall_copy_from_user(&cap, (const void *)oldlenp, sizeof cap) < 0) return -EFAULT;
    int64_t r = 0;
    if (oldp) {
        size_t n = len < cap ? len : cap;
        if (n && syscall_copy_to_user((void *)oldp, data, n) < 0) return -EFAULT;
        if (len > cap) r = -ENOMEM;
    }
    size_t out = oldp && len > cap ? cap : len;
    if (oldlenp && syscall_copy_to_user((void *)oldlenp, &out, sizeof out) < 0) return -EFAULT;
    return r;
}

static int64_t sysctl_io(const fb_sysctl_t *e, const int32_t *rest, uint32_t nrest,
                         uint64_t oldp, uint64_t oldlenp, uint64_t newp)
{
    if (newp) return -EPERM;
    size_t cap = 0;
    if (oldlenp && syscall_copy_from_user(&cap, (const void *)oldlenp, sizeof cap) < 0) return -EFAULT;
    size_t bufcap = e->kind == SC_FN ? 1048576 : 4096;
    if (oldp && cap < bufcap) bufcap = cap < 4096 ? 4096 : cap;
    uint8_t *buf = calloc(1, bufcap);
    if (!buf) return -ENOMEM;
    size_t len = bufcap;
    int64_t r = sysctl_value(e, rest, nrest, buf, &len);
    if (r == 0) {
        if (len <= bufcap) {
            r = sysctl_reply(buf, len, oldp, oldlenp);
        } else if (!oldp) {
            if (oldlenp && syscall_copy_to_user((void *)oldlenp, &len, sizeof len) < 0) r = -EFAULT;
        } else {
            size_t n = cap < bufcap ? cap : bufcap;
            if (n && syscall_copy_to_user((void *)oldp, buf, n) < 0) r = -EFAULT;
            else r = -ENOMEM;
        }
    }
    free(buf);
    return r;
}

static const fb_sysctl_t *sysctl_find_name(const char *name)
{
    for (size_t i = 0; i < SYSCTL_N; i++)
        if (strcmp(g_sysctl[i].name, name) == 0) return &g_sysctl[i];
    return NULL;
}

static const fb_sysctl_t *sysctl_find_mib(const int32_t *mib, uint32_t n, uint32_t *used)
{
    for (size_t i = 0; i < SYSCTL_N; i++) {
        const fb_sysctl_t *e = &g_sysctl[i];
        if (n < e->nmib || memcmp(mib, e->mib, e->nmib * 4) != 0) continue;
        if (n > e->nmib && e->kind != SC_FN) continue;
        *used = e->nmib;
        return e;
    }
    return NULL;
}

static int64_t fb_sysctl_name2oid(uint64_t oldp, uint64_t oldlenp, uint64_t newp, uint64_t newlen)
{
    if (!newp || newlen == 0 || newlen > 255) return -ENOENT;
    char name[256];
    if (syscall_copy_from_user(name, (const void *)newp, newlen) < 0) return -EFAULT;
    name[newlen] = 0;
    const fb_sysctl_t *e = sysctl_find_name(name);
    int32_t oid[6];
    uint32_t n = 0;
    if (e) {
        memcpy(oid, e->mib, e->nmib * 4);
        n = e->nmib;
    } else {
        static const struct { const char *sub; int32_t v; } procsub[] = {
            { "all", 0 }, { "pid", 1 }, { "pgrp", 2 }, { "session", 3 }, { "uid", 5 }, { "ruid", 6 },
            { "args", 7 }, { "proc", 8 }, { "rgid", 10 }, { "gid", 11 }, { "pathname", 12 },
            { "umask", 39 }, { "osrel", 40 },
        };
        if (strncmp(name, "kern.proc.", 10) != 0) return -ENOENT;
        const char *sub = name + 10;
        bool found = false;
        for (size_t i = 0; i < sizeof procsub / sizeof procsub[0]; i++) {
            if (strcmp(sub, procsub[i].sub) == 0) {
                oid[0] = 1; oid[1] = 14; oid[2] = procsub[i].v;
                n = 3;
                found = true;
                break;
            }
        }
        if (!found) return -ENOENT;
    }
    return sysctl_reply(oid, n * 4, oldp, oldlenp);
}

static int64_t fb_sysctl_meta(const int32_t *mib, uint32_t n, uint64_t oldp, uint64_t oldlenp)
{
    uint32_t used = 0;
    const fb_sysctl_t *e = n > 2 ? sysctl_find_mib(mib + 2, n - 2, &used) : NULL;
    if (mib[1] == 1) {
        if (!e) return -ENOENT;
        return sysctl_reply(e->name, strlen(e->name) + 1, oldp, oldlenp);
    }
    if (mib[1] == 4) {
        if (!e) return -ENOENT;
        uint8_t out[64];
        uint32_t kind = e->ctltype | 0x80000000u;
        memcpy(out, &kind, 4);
        size_t fl = strlen(e->fmt) + 1;
        memcpy(out + 4, e->fmt, fl);
        return sysctl_reply(out, 4 + fl, oldp, oldlenp);
    }
    if (mib[1] == 2 || mib[1] == 7) {
        size_t idx = 0;
        if (n > 2) {
            if (!e) {
                for (idx = 0; idx < SYSCTL_N; idx++)
                    if (memcmp(g_sysctl[idx].mib, mib + 2, 4) > 0) break;
            } else {
                idx = (size_t)(e - g_sysctl) + 1;
            }
        }
        while (idx < SYSCTL_N && g_sysctl[idx].ctltype == CT_NODE) idx++;
        if (idx >= SYSCTL_N) return -ENOENT;
        return sysctl_reply(g_sysctl[idx].mib, g_sysctl[idx].nmib * 4, oldp, oldlenp);
    }
    return -ENOENT;
}

static int64_t fb_sysctl(A6)
{
    UNUSED6;
    uint32_t n = (uint32_t)a2;
    if (n < 2 || n > 24) return -EINVAL;
    int32_t mib[24];
    if (syscall_copy_from_user(mib, (const void *)a1, n * 4) < 0) return -EFAULT;
    if (mib[0] == 0 && mib[1] == 3) return fb_sysctl_name2oid(a3, a4, a5, a6);
    if (mib[0] == 0) return fb_sysctl_meta(mib, n, a3, a4);
    if (mib[0] == 4) return fb_sysctl_net(mib, n, a3, a4, a5);
    uint32_t used = 0;
    const fb_sysctl_t *e = sysctl_find_mib(mib, n, &used);
    if (!e) return -ENOENT;
    return sysctl_io(e, mib + used, n - used, a3, a4, a5);
}

static int64_t fb_sysctlbyname(A6)
{
    UNUSED6;
    if (a2 == 0 || a2 > 255) return -ENOENT;
    char name[256];
    if (syscall_copy_from_user(name, (const void *)a1, a2) < 0) return -EFAULT;
    name[a2] = 0;
    const fb_sysctl_t *e = sysctl_find_name(name);
    if (e) return sysctl_io(e, NULL, 0, a3, a4, a5);
    int32_t oid[6];
    uint64_t sc = fb_scratch(syscall_cur_task(), 64);
    size_t olen = sizeof oid;
    if (syscall_copy_to_user((void *)(sc + 32), &olen, sizeof olen) < 0) return -EFAULT;
    int64_t r = fb_sysctl_name2oid(sc, sc + 32, a1, a2);
    if (r < 0) return r;
    if (syscall_copy_from_user(oid, (const void *)sc, 12) < 0) return -EFAULT;
    uint32_t used = 0;
    e = sysctl_find_mib(oid, 3, &used);
    if (!e) return -ENOENT;
    int32_t rest[2] = { oid[2], -1 };
    return sysctl_io(e, rest, 2, a3, a4, a5);
}

FB0(fb_read,      sys_read(a1, a2, a3))
FB0(fb_write,     sys_write(a1, a2, a3))
FB0(fb_close,     sys_close(a1))
FB0(fb_link,      sys_link(a1, a2))
FB0(fb_unlink,    sys_unlink(a1, 0, 0, 0, 0, 0))
FB0(fb_chdir,     sys_chdir(a1, 0, 0, 0, 0, 0))
FB0(fb_fchdir,    sys_fchdir(a1))
FB0(fb_chmod,     sys_chmod(a1, a2))
FB0(fb_chown,     sys_chown(a1, I(a2), I(a3)))
FB0(fb_getpid,    sys_getpid())
FB0(fb_setuid,    sys_setuid(a1))
FB0(fb_getuid,    sys_getuid())
FB0(fb_sync,      sys_sync(0, 0, 0, 0, 0, 0))
FB0(fb_getppid,   sys_getppid())
FB0(fb_dup,       sys_dup(a1))
FB0(fb_getgid,    sys_getgid())
FB0(fb_symlink,   sys_symlink(a1, a2))
FB0(fb_readlink,  sys_readlink(a1, a2, a3))
FB0(fb_execve,    sys_execve(a1, a2, a3))
FB0(fb_umask,     sys_umask(a1))
FB0(fb_chroot,    sys_chroot(a1))
FB0(fb_munmap,    sys_munmap(a1, a2))
FB0(fb_mprotect,  sys_mprotect(a1, a2, a3))
FB0(fb_mincore,   sys_mincore(a1, a2, a3))
FB0(fb_setpgid,   sys_setpgid(I(a1), I(a2)))
FB0(fb_setitimer, sys_setitimer(a1, a2, a3))
FB0(fb_getitimer, sys_getitimer(a1, a2))
FB0(fb_dup2,      sys_dup2(a1, a2))
FB0(fb_select,    sys_select(a1, a2, a3, a4, a5))
FB0(fb_fsync,     sys_fsync(a1))
FB0(fb_setpriority, sys_setpriority(a1, I(a2), I(a3)))
FB0(fb_listen,    sys_listen(a1, a2))
FB0(fb_getrusage, sys_getrusage(a1, a2))
FB0(fb_readv,     sys_readv(a1, a2, a3))
FB0(fb_writev,    sys_writev(a1, a2, a3))
FB0(fb_fchown,    sys_fchown(a1, I(a2), I(a3)))
FB0(fb_fchmod,    sys_fchmod(a1, a2))
FB0(fb_rename,    sys_rename(a1, a2, 0, 0, 0, 0))
FB0(fb_shutdown,  sys_sock_shutdown(a1, a2))
FB0(fb_mkdir,     sys_mkdir(a1, a2, 0, 0, 0, 0))
FB0(fb_rmdir,     sys_rmdir(a1, 0, 0, 0, 0, 0))
FB0(fb_setsid,    sys_setsid())
FB0(fb_setgid,    sys_setgid(a1))
FB0(fb_mlock,     sys_mlock(a1, a2))
FB0(fb_munlock,   sys_munlock(a1, a2))
FB0(fb_getpgid,   sys_getpgid(I(a1)))
FB0(fb_lchown,    sys_lchown(a1, I(a2), I(a3)))
FB0(fb_preadv,    sys_preadv(a1, a2, a3, a4))
FB0(fb_pwritev,   sys_pwritev(a1, a2, a3, a4))
FB0(fb_getsid,    sys_getsid(I(a1)))
FB0(fb_yield,     sys_yield())
FB0(fb_mlockall,  sys_mlockall(a1))
FB0(fb_munlockall, sys_munlockall())
FB0(fb_pread,     sys_pread(a1, a2, a3, a4))
FB0(fb_pwrite,    sys_pwrite(a1, a2, a3, a4))
FB0(fb_lseek,     sys_seek(a1, a2, I(a3)))
FB0(fb_truncate,  sys_truncate(a1, a2))
FB0(fb_ftruncate, sys_ftruncate(a1, a2))
FB0(fb_mkdirat,   sys_mkdirat(I(a1), a2, a3))
FB0(fb_readlinkat, sys_readlinkat(I(a1), a2, a3, a4))
FB0(fb_renameat,  sys_renameat(I(a1), a2, I(a3), a4))
FB0(fb_symlinkat, sys_symlinkat(a1, I(a2), a3))
FB0(fb_fdatasync, sys_fdatasync(a1))
FB0(fb_close_range, sys_close_range(a1, a2, a3))
FB0(fb_sigreturn, freebsd_sigreturn(a1))
FB0(fb_getcontext, freebsd_getcontext(a1))
static int64_t fb_flock(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)a1);
    if (!f) return -EBADF;
    fd_put(f);
    return 0;
}

static int64_t fb_copy_file_range(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (a6) return -EINVAL;
    vfs_file_t *fi = fd_get(t->fd_table, (int)a1);
    if (!fi) return -EBADF;
    vfs_file_t *fo = fd_get(t->fd_table, (int)a3);
    if (!fo) { fd_put(fi); return -EBADF; }
    int64_t rc = 0;
    vfs_stat_t si, so;
    uint8_t *buf = NULL;
    if ((fi->flags & 3) == O_WRONLY || (fo->flags & 3) == O_RDONLY || (fo->flags & O_APPEND)) { rc = -EBADF; goto out; }
    if (vfs_fstat(fi, &si) < 0 || vfs_fstat(fo, &so) < 0) { rc = -EBADF; goto out; }
    if (si.st_type != VFS_NODE_FILE || so.st_type != VFS_NODE_FILE) { rc = -EINVAL; goto out; }
    uint64_t inoff = fi->offset, outoff = fo->offset;
    if (a2 && syscall_copy_from_user(&inoff, (const void *)a2, 8) < 0) { rc = -EFAULT; goto out; }
    if (a4 && syscall_copy_from_user(&outoff, (const void *)a4, 8) < 0) { rc = -EFAULT; goto out; }
    buf = malloc(65536);
    if (!buf) { rc = -ENOMEM; goto out; }
    uint64_t want = a5 > (16ULL << 20) ? (16ULL << 20) : a5;
    uint64_t done = 0;
    while (done < want) {
        size_t chunk = want - done < 65536 ? (size_t)(want - done) : 65536;
        int64_t n = vfs_pread(fi, buf, chunk, inoff);
        if (n < 0) { if (!done) rc = n; break; }
        if (n == 0) break;
        int64_t w = vfs_pwrite(fo, buf, (size_t)n, outoff);
        if (w < 0) { if (!done) rc = w; break; }
        inoff += (uint64_t)w;
        outoff += (uint64_t)w;
        done += (uint64_t)w;
        if (w < n) break;
    }
    if (rc == 0) {
        rc = (int64_t)done;
        if (a2) syscall_copy_to_user((void *)a2, &inoff, 8);
        else fi->offset = inoff;
        if (a4) syscall_copy_to_user((void *)a4, &outoff, 8);
        else fo->offset = outoff;
    }
out:
    free(buf);
    fd_put(fo);
    fd_put(fi);
    return rc;
}

static int64_t fb_cap_getmode(A6)
{
    UNUSED6;
    uint32_t m = 0;
    return syscall_copy_to_user((void *)a1, &m, sizeof m);
}

#define UMUTEX_CONTESTED 0x80000000u
#define URW_WRITE_OWNER  0x80000000u
#define URW_WRITE_WAIT   0x40000000u
#define URW_READ_WAIT    0x20000000u
#define URW_MAX_READERS  0x1fffffffu
#define USEM_WAITERS     0x80000000u

static int64_t umtx_deadline(uint64_t usize, uint64_t utime, uint64_t *deadline)
{
    *deadline = 0;
    if (!utime) return 0;
    int64_t ts[2];
    uint32_t flags = 0, clk = 0;
    if (usize > 16) {
        struct { int64_t sec, nsec; uint32_t flags, clockid; } ut;
        if (syscall_copy_from_user(&ut, (const void *)utime, sizeof ut) < 0) return -EFAULT;
        ts[0] = ut.sec; ts[1] = ut.nsec; flags = ut.flags; clk = ut.clockid;
    } else if (syscall_copy_from_user(ts, (const void *)utime, sizeof ts) < 0) {
        return -EFAULT;
    }
    if (ts[0] < 0 || ts[1] < 0 || ts[1] >= 1000000000) return -EINVAL;
    uint64_t ns = (uint64_t)ts[0] * 1000000000ULL + (uint64_t)ts[1];
    uint64_t mono = sched_now_ns();
    if (flags & 1) {
        uint64_t now = fb_clock_ns(clk);
        if (ns <= now) return -ETIMEDOUT;
        ns -= now;
    }
    *deadline = mono + ns;
    if (!*deadline) *deadline = 1;
    return 0;
}

static int64_t umtx_sleep(uint64_t addr, uint32_t expected, uint64_t deadline)
{
    int64_t r = futex_wait_deadline(addr, expected, deadline);
    if (r == -EAGAIN || r == 0) return 0;
    return r;
}

static int64_t umtx_wake(uint64_t addr, uint64_t n)
{
    int64_t r = sys_futex(addr, 1, n > 0x7fffffff ? 0x7fffffff : n, 0, 0, 0);
    return r < 0 ? 0 : r;
}

static int64_t umutex_wait(uint64_t m)
{
    for (;;) {
        uint32_t owner;
        if (futex_user_load(m, &owner) < 0) return -EFAULT;
        if (owner == 0 || owner == UMUTEX_CONTESTED) return 0;
        if (!(owner & UMUTEX_CONTESTED)) {
            uint32_t exp = owner;
            int c = futex_user_cas(m, &exp, owner | UMUTEX_CONTESTED);
            if (c < 0) return c;
            if (!c) continue;
        }
        return umtx_sleep(m, owner | UMUTEX_CONTESTED, 0);
    }
}

static int64_t umutex_wake2(uint64_t m)
{
    for (;;) {
        uint32_t owner;
        if (futex_user_load(m, &owner) < 0) return -EFAULT;
        if (owner & UMUTEX_CONTESTED) break;
        uint32_t exp = owner;
        int c = futex_user_cas(m, &exp, owner | UMUTEX_CONTESTED);
        if (c < 0) return c;
        if (c) break;
    }
    if (umtx_wake(m, 1) == 0) {
        uint32_t exp = UMUTEX_CONTESTED;
        futex_user_cas(m, &exp, 0);
    }
    return 0;
}

static int64_t umutex_lock(uint64_t m, uint32_t tid, int try, uint64_t deadline)
{
    for (;;) {
        uint32_t owner;
        if (futex_user_load(m, &owner) < 0) return -EFAULT;
        if ((owner & ~UMUTEX_CONTESTED) == 0) {
            uint32_t exp = owner;
            int c = futex_user_cas(m, &exp, tid | owner);
            if (c < 0) return c;
            if (c) return 0;
            continue;
        }
        if ((owner & ~UMUTEX_CONTESTED) == tid) return -EDEADLK;
        if (try) return -EBUSY;
        if (!(owner & UMUTEX_CONTESTED)) {
            uint32_t exp = owner;
            int c = futex_user_cas(m, &exp, owner | UMUTEX_CONTESTED);
            if (c < 0) return c;
            if (!c) continue;
        }
        int64_t r = umtx_sleep(m, owner | UMUTEX_CONTESTED, deadline);
        if (r < 0) return r;
    }
}

static int64_t umutex_unlock(uint64_t m, uint32_t tid)
{
    uint32_t owner;
    if (futex_user_load(m, &owner) < 0) return -EFAULT;
    if ((owner & ~UMUTEX_CONTESTED) != tid) return -EPERM;
    uint32_t exp = owner;
    int c = futex_user_cas(m, &exp, (owner & UMUTEX_CONTESTED) ? UMUTEX_CONTESTED : 0);
    if (c < 0) return c;
    if (!c) return -EPERM;
    if (owner & UMUTEX_CONTESTED) {
        if (umtx_wake(m, 1) == 0) {
            exp = UMUTEX_CONTESTED;
            futex_user_cas(m, &exp, 0);
        }
    }
    return 0;
}

static int64_t urw_rdlock(uint64_t rw, uint64_t deadline)
{
    for (;;) {
        uint32_t st;
        if (futex_user_load(rw, &st) < 0) return -EFAULT;
        if (!(st & (URW_WRITE_OWNER | URW_WRITE_WAIT)) && (st & URW_MAX_READERS) < URW_MAX_READERS) {
            uint32_t exp = st;
            int c = futex_user_cas(rw, &exp, st + 1);
            if (c < 0) return c;
            if (c) return 0;
            continue;
        }
        if (!(st & URW_READ_WAIT)) {
            uint32_t exp = st;
            int c = futex_user_cas(rw, &exp, st | URW_READ_WAIT);
            if (c < 0) return c;
            if (!c) continue;
        }
        int64_t r = umtx_sleep(rw, st | URW_READ_WAIT, deadline);
        if (r < 0) return r;
    }
}

static int64_t urw_wrlock(uint64_t rw, uint64_t deadline)
{
    for (;;) {
        uint32_t st;
        if (futex_user_load(rw, &st) < 0) return -EFAULT;
        if (!(st & URW_WRITE_OWNER) && (st & URW_MAX_READERS) == 0) {
            uint32_t exp = st;
            int c = futex_user_cas(rw, &exp, (st | URW_WRITE_OWNER) & ~URW_WRITE_WAIT);
            if (c < 0) return c;
            if (c) {
                if (st & (URW_WRITE_WAIT | URW_READ_WAIT)) umtx_wake(rw, 0x7fffffff);
                return 0;
            }
            continue;
        }
        if (!(st & URW_WRITE_WAIT)) {
            uint32_t exp = st;
            int c = futex_user_cas(rw, &exp, st | URW_WRITE_WAIT);
            if (c < 0) return c;
            if (!c) continue;
        }
        int64_t r = umtx_sleep(rw, st | URW_WRITE_WAIT, deadline);
        if (r < 0) return r;
    }
}

static int64_t urw_unlock(uint64_t rw)
{
    for (;;) {
        uint32_t st;
        if (futex_user_load(rw, &st) < 0) return -EFAULT;
        uint32_t nv;
        if (st & URW_WRITE_OWNER) nv = st & ~URW_WRITE_OWNER;
        else if (st & URW_MAX_READERS) nv = st - 1;
        else return -EPERM;
        bool wake = (st & (URW_WRITE_WAIT | URW_READ_WAIT)) && !(nv & (URW_WRITE_OWNER | URW_MAX_READERS));
        if (wake) nv &= ~(URW_WRITE_WAIT | URW_READ_WAIT);
        uint32_t exp = st;
        int c = futex_user_cas(rw, &exp, nv);
        if (c < 0) return c;
        if (!c) continue;
        if (wake) umtx_wake(rw, 0x7fffffff);
        return 0;
    }
}

static int64_t fb_umtx_op(A6)
{
    UNUSED6;
    uint64_t obj = a1, val = a3;
    uint64_t deadline = 0;
    int64_t r;
    switch (a2) {
    case 2: {
        r = umtx_deadline(a4, a5, &deadline);
        if (r < 0) return r;
        uint64_t cur;
        if (syscall_copy_from_user(&cur, (const void *)obj, 8) < 0) return -EFAULT;
        if (cur != val) return 0;
        return umtx_sleep(obj, (uint32_t)val, deadline);
    }
    case 11: case 15: {
        r = umtx_deadline(a4, a5, &deadline);
        if (r < 0) return r;
        return umtx_sleep(obj, (uint32_t)val, deadline);
    }
    case 3: case 16:
        umtx_wake(obj, val);
        return 0;
    case 21: {
        for (uint64_t i = 0; i < val && i < 4096; i++) {
            uint64_t p;
            if (syscall_copy_from_user(&p, (const void *)(obj + i * 8), 8) < 0) return -EFAULT;
            umtx_wake(p, 0x7fffffff);
        }
        return 0;
    }
    case 4: case 5: {
        r = umtx_deadline(a4, a5, &deadline);
        if (r < 0) return r;
        return umutex_lock(obj, (uint32_t)sys_gettid(), a2 == 4, deadline);
    }
    case 6:
        return umutex_unlock(obj, (uint32_t)sys_gettid());
    case 7:
        return -EINVAL;
    case 17:
        return umutex_wait(obj);
    case 18: case 22:
        return umutex_wake2(obj);
    case 8: {
        r = umtx_deadline(0, a5, &deadline);
        if (r < 0) return r;
        if ((val & 2) && a5) {
            int64_t ts[2];
            if (syscall_copy_from_user(ts, (const void *)a5, sizeof ts) < 0) return -EFAULT;
            uint32_t clk = 0;
            if (val & 4) futex_user_load(obj + 8, &clk);
            uint64_t ns = (uint64_t)ts[0] * 1000000000ULL + (uint64_t)ts[1], now = fb_clock_ns(clk);
            deadline = ns <= now ? 1 : sched_now_ns() + (ns - now);
        }
        uint32_t seq;
        if (futex_user_load(obj + 12, &seq) < 0) return -EFAULT;
        uint32_t one = 1;
        if (syscall_copy_to_user((void *)obj, &one, 4) < 0) return -EFAULT;
        r = umutex_unlock(a4, (uint32_t)sys_gettid());
        if (r < 0) return r;
        return umtx_sleep(obj + 12, seq, deadline);
    }
    case 9: case 10: {
        for (;;) {
            uint32_t seq;
            if (futex_user_load(obj + 12, &seq) < 0) return -EFAULT;
            uint32_t exp = seq;
            int c = futex_user_cas(obj + 12, &exp, seq + 1);
            if (c < 0) return c;
            if (c) break;
        }
        int64_t n = umtx_wake(obj + 12, a2 == 10 ? 0x7fffffff : 1);
        if (a2 == 10 || n == 0) {
            uint32_t z = 0;
            syscall_copy_to_user((void *)obj, &z, 4);
        }
        return 0;
    }
    case 12: {
        r = umtx_deadline(a4, a5, &deadline);
        if (r < 0) return r;
        return urw_rdlock(obj, deadline);
    }
    case 13: {
        r = umtx_deadline(a4, a5, &deadline);
        if (r < 0) return r;
        return urw_wrlock(obj, deadline);
    }
    case 14:
        return urw_unlock(obj);
    case 23: {
        r = umtx_deadline(a4, a5, &deadline);
        if (r < 0) return r;
        for (;;) {
            uint32_t cnt;
            if (futex_user_load(obj, &cnt) < 0) return -EFAULT;
            if (cnt & ~USEM_WAITERS) return 0;
            if (!(cnt & USEM_WAITERS)) {
                uint32_t exp = cnt;
                int c = futex_user_cas(obj, &exp, cnt | USEM_WAITERS);
                if (c < 0) return c;
                if (!c) continue;
            }
            return umtx_sleep(obj, cnt | USEM_WAITERS, deadline);
        }
    }
    case 24: {
        if (umtx_wake(obj, 1) == 0) {
            for (;;) {
                uint32_t cnt;
                if (futex_user_load(obj, &cnt) < 0) return -EFAULT;
                if (!(cnt & USEM_WAITERS)) break;
                uint32_t exp = cnt;
                int c = futex_user_cas(obj, &exp, cnt & ~USEM_WAITERS);
                if (c != 0) break;
            }
        }
        return 0;
    }
    case 26: case 27: case 28:
        return 0;
    }
    return -EINVAL;
}

typedef struct {
    uint64_t start_func;
    uint64_t arg;
    uint64_t stack_base;
    uint64_t stack_size;
    uint64_t tls_base;
    uint64_t tls_size;
    uint64_t child_tid;
    uint64_t parent_tid;
    int32_t  flags;
    int32_t  pad;
    uint64_t rtp;
    uint64_t spare[3];
} fb_thr_param_t;

_Static_assert(sizeof(fb_thr_param_t) == 104, "FreeBSD struct thr_param is 104 bytes");

static int64_t fb_thr_new(A6)
{
    UNUSED6;
    task_t *me = syscall_cur_task();
    if (!me || !me->is_userspace) return -EPERM;
    fb_thr_param_t p;
    memset(&p, 0, sizeof p);
    size_t sz = a2 < sizeof p ? a2 : sizeof p;
    if (sz < 64 || syscall_copy_from_user(&p, (const void *)a1, sz) < 0) return -EFAULT;
    if (p.tls_base >= 0x0000800000000000ULL || p.start_func >= 0x0000800000000000ULL) return -EINVAL;
    uint64_t sp = ((p.stack_base + p.stack_size) & ~15ULL) - 8;
    task_t *th = task_spawn_clone(me, sp, p.tls_base);
    if (!th) return -ENOMEM;
    th->user_saved_r11 &= ~1ULL;
    th->user_saved_rip = p.start_func;
    th->user_saved_rdi = p.arg;
    th->user_rsp = sp;
    int64_t tid = (int64_t)th->pid;
    if (p.child_tid) syscall_copy_to_user((void *)p.child_tid, &tid, sizeof tid);
    if (p.parent_tid) syscall_copy_to_user((void *)p.parent_tid, &tid, sizeof tid);
    ptrace_note_child(me, th, 3);
    task_start_clone(th);
    return 0;
}

static int64_t fb_thr_exit(A6)
{
    UNUSED6;
    if (a1) {
        int64_t one = 1;
        if (syscall_copy_to_user((void *)a1, &one, sizeof one) == 0) umtx_wake(a1, 0x7fffffff);
    }
    return sys_thread_exit(0, 0, 0, 0, 0, 0);
}

typedef struct epoll_set epoll_set_t;
extern epoll_set_t *epk_create(void);
extern void epk_destroy(epoll_set_t *ep);
extern int64_t epk_ctl(epoll_set_t *ep, vfs_file_t *epf, int op, vfs_file_t *target, int fd, uint32_t events, uint64_t data);
extern int64_t epk_wait(epoll_set_t *ep, void *kout, int maxevents, int64_t timeout_ns, uint64_t extra_deadline, volatile int *kick);
extern int epk_poll(epoll_set_t *ep, int events);
extern void epk_wake(epoll_set_t *ep);
extern void poll_deadline(uint64_t ns);

typedef struct {
    uint32_t events;
    uint64_t data;
} __attribute__((packed)) kq_epev_t;

typedef struct {
    uint64_t ident;
    int16_t  filter;
    uint16_t flags;
    uint32_t fflags;
    int64_t  data;
    uint64_t udata;
    uint64_t ext[4];
} fb_kevent_t;

_Static_assert(sizeof(fb_kevent_t) == 64, "FreeBSD struct kevent is 64 bytes");

#define EVF_READ   (-1)
#define EVF_WRITE  (-2)
#define EVF_VNODE  (-4)
#define EVF_PROC   (-5)
#define EVF_SIGNAL (-6)
#define EVF_TIMER  (-7)
#define EVF_USER   (-11)

#define EV_ADD      0x0001
#define EV_DELETE   0x0002
#define EV_ENABLE   0x0004
#define EV_DISABLE  0x0008
#define EV_ONESHOT  0x0010
#define EV_CLEAR    0x0020
#define EV_RECEIPT  0x0040
#define EV_DISPATCH 0x0080
#define EV_ERROR    0x4000
#define EV_EOF      0x8000

#define KQ_POLL_NS  50000000ULL

typedef struct {
    uint64_t ident;
    int16_t  filter;
    uint16_t flags;
    uint32_t sfflags;
    int64_t  sdata;
    uint64_t udata;
    int      enabled;
    int      active;
    uint32_t ffl;
    uint64_t period;
    uint64_t next_ns;
    uint64_t vsize;
    int64_t  vmtime;
    uint32_t vnlink;
} kq_note_t;

typedef struct {
    epoll_set_t *ep;
    kmutex_t     lock;
    kq_note_t   *notes;
    int          n, cap;
    volatile int kick;
} kq_t;

static const vnode_ops_t KQ_OPS;

static kq_note_t *kq_find(kq_t *kq, uint64_t ident, int16_t filter)
{
    for (int i = 0; i < kq->n; i++)
        if (kq->notes[i].ident == ident && kq->notes[i].filter == filter) return &kq->notes[i];
    return NULL;
}

static void kq_drop(kq_t *kq, kq_note_t *kn)
{
    *kn = kq->notes[--kq->n];
}

static uint64_t kq_timer_ns(uint32_t fflags, int64_t data)
{
    if (data < 0) data = 0;
    uint64_t v = (uint64_t)data;
    if (fflags & 0x1) return v * 1000000000ULL;
    if (fflags & 0x4) return v * 1000ULL;
    if (fflags & 0x8) return v;
    return v * 1000000ULL;
}

static int kq_sync_fd(kq_t *kq, int fd)
{
    task_t *t = syscall_cur_task();
    vfs_file_t *f = t && t->fd_table ? fd_get(t->fd_table, fd) : NULL;
    if (!f) return -EBADF;
    uint32_t ev = 0;
    int all_clear = 1, any = 0;
    for (int i = 0; i < kq->n; i++) {
        kq_note_t *kn = &kq->notes[i];
        if (kn->ident != (uint64_t)fd || !kn->enabled) continue;
        if (kn->filter == EVF_READ) ev |= POLLIN | 0x2000;
        else if (kn->filter == EVF_WRITE) ev |= POLLOUT;
        else continue;
        any = 1;
        if (!(kn->flags & EV_CLEAR)) all_clear = 0;
    }
    int64_t r;
    if (!any) {
        r = epk_ctl(kq->ep, NULL, 2, f, fd, 0, 0);
        if (r == -ENOENT) r = 0;
    } else {
        if (all_clear) ev |= 1u << 31;
        r = epk_ctl(kq->ep, NULL, 3, f, fd, ev, (uint64_t)fd);
        if (r == -ENOENT) r = epk_ctl(kq->ep, NULL, 1, f, fd, ev, (uint64_t)fd);
    }
    fd_put(f);
    return (int)r;
}

static int kq_vnode_snap(kq_note_t *kn)
{
    vfs_stat_t st;
    int r = fb_fd_stat(kn->ident, &st);
    if (r < 0) return r;
    kn->vsize = st.st_size;
    kn->vmtime = st.st_mtime;
    kn->vnlink = st.st_nlink;
    return 0;
}

static int kq_apply(kq_t *kq, const fb_kevent_t *ch)
{
    kq_note_t *kn = kq_find(kq, ch->ident, ch->filter);
    switch (ch->filter) {
    case EVF_READ: case EVF_WRITE: case EVF_VNODE: case EVF_PROC: case EVF_TIMER: case EVF_USER: case EVF_SIGNAL:
        break;
    default:
        return -EINVAL;
    }
    if (ch->flags & EV_DELETE) {
        if (!kn) return -ENOENT;
        int fdf = kn->filter == EVF_READ || kn->filter == EVF_WRITE;
        kq_drop(kq, kn);
        if (fdf) kq_sync_fd(kq, (int)ch->ident);
        return 0;
    }
    if (!kn) {
        if (!(ch->flags & EV_ADD)) return -ENOENT;
        if (kq->n >= kq->cap) {
            int nc = kq->cap ? kq->cap * 2 : 16;
            kq_note_t *nn = realloc(kq->notes, (size_t)nc * sizeof *nn);
            if (!nn) return -ENOMEM;
            kq->notes = nn;
            kq->cap = nc;
        }
        kn = &kq->notes[kq->n++];
        memset(kn, 0, sizeof *kn);
        kn->ident = ch->ident;
        kn->filter = ch->filter;
        kn->enabled = 1;
        if (ch->filter == EVF_PROC && !task_find_by_pid((uint32_t)ch->ident)) {
            kq->n--;
            return -ESRCH;
        }
    }
    if (ch->flags & EV_ADD) {
        kn->udata = ch->udata;
        kn->flags = ch->flags & (EV_ONESHOT | EV_CLEAR | EV_DISPATCH);
    }
    if (ch->flags & EV_ENABLE) kn->enabled = 1;
    if (ch->flags & EV_DISABLE) kn->enabled = 0;
    if (ch->filter == EVF_USER) {
        uint32_t ctl = ch->fflags & 0xc0000000u, v = ch->fflags & 0x00ffffffu;
        if (ctl == 0x40000000u) kn->ffl &= v;
        else if (ctl == 0x80000000u) kn->ffl |= v;
        else if (ctl == 0xc0000000u) kn->ffl = v;
        if (ch->fflags & 0x01000000u) {
            kn->active = 1;
            __atomic_store_n(&kq->kick, 1, __ATOMIC_RELEASE);
            epk_wake(kq->ep);
        }
        return 0;
    }
    if (ch->flags & EV_ADD) {
        kn->sfflags = ch->fflags;
        kn->sdata = ch->data;
    }
    if (ch->filter == EVF_TIMER && (ch->flags & EV_ADD)) {
        uint64_t ns = kq_timer_ns(ch->fflags, ch->data);
        uint64_t now = sched_now_ns();
        if (ch->fflags & 0x10) {
            uint64_t rt = fb_clock_ns(0);
            kn->next_ns = ns > rt ? now + (ns - rt) : now;
            kn->period = 0;
            kn->flags |= EV_ONESHOT;
        } else {
            kn->period = ns ? ns : 1;
            kn->next_ns = now + kn->period;
        }
        kn->active = 0;
        __atomic_store_n(&kq->kick, 1, __ATOMIC_RELEASE);
        epk_wake(kq->ep);
        return 0;
    }
    if (ch->filter == EVF_VNODE) return kq_vnode_snap(kn);
    if (ch->filter == EVF_SIGNAL && (ch->flags & EV_ADD)) {
        int ls = fb_sig_to_linux((int)ch->ident);
        task_t *me = syscall_cur_task();
        if (ls <= 0) { kq_drop(kq, kn); return -EINVAL; }
        if (me) __atomic_fetch_and(&task_leader(me)->sig_posted, ~(1ULL << ls), __ATOMIC_RELAXED);
    }
    if (ch->filter == EVF_READ || ch->filter == EVF_WRITE) {
        int r = kq_sync_fd(kq, (int)ch->ident);
        if (r < 0) { kq_drop(kq, kn); return r; }
    }
    return 0;
}

static int64_t kq_fd_read_avail(int fd)
{
    task_t *t = syscall_cur_task();
    vfs_file_t *f = t && t->fd_table ? fd_get(t->fd_table, fd) : NULL;
    if (!f) return 0;
    int n = 0;
    vfs_stat_t st;
    int64_t v = 1;
    if (vfs_ioctl(f, 0x541B, &n) >= 0) v = n;
    else if (vfs_fstat(f, &st) == 0 && st.st_type == VFS_NODE_FILE) v = (int64_t)st.st_size - (int64_t)f->offset;
    fd_put(f);
    return v;
}

static int kq_emit(kq_t *kq, kq_note_t *kn, fb_kevent_t *out, uint16_t extra, uint32_t fflags, int64_t data)
{
    memset(out, 0, sizeof *out);
    out->ident = kn->ident;
    out->filter = kn->filter;
    out->flags = (uint16_t)(kn->flags | extra);
    out->fflags = fflags;
    out->data = data;
    out->udata = kn->udata;
    int fdf = kn->filter == EVF_READ || kn->filter == EVF_WRITE;
    uint64_t ident = kn->ident;
    if (kn->flags & EV_ONESHOT) {
        kq_drop(kq, kn);
        if (fdf) kq_sync_fd(kq, (int)ident);
        return 1;
    }
    if (kn->flags & EV_DISPATCH) {
        kn->enabled = 0;
        if (fdf) kq_sync_fd(kq, (int)ident);
    }
    return 0;
}

static int kq_collect_soft(kq_t *kq, fb_kevent_t *out, int max, uint64_t *next)
{
    int n = 0;
    uint64_t now = sched_now_ns(), nx = 0;
    for (int i = 0; i < kq->n && n < max; i++) {
        kq_note_t *kn = &kq->notes[i];
        if (!kn->enabled) continue;
        if (kn->filter == EVF_TIMER) {
            if (now >= kn->next_ns) {
                uint64_t cnt = 1;
                if (kn->period) {
                    cnt += (now - kn->next_ns) / kn->period;
                    kn->next_ns += cnt * kn->period;
                }
                if (kq_emit(kq, kn, &out[n++], 0, 0, (int64_t)cnt)) i--;
                continue;
            }
            if (!nx || kn->next_ns < nx) nx = kn->next_ns;
        } else if (kn->filter == EVF_USER) {
            if (kn->active) {
                uint32_t ffl = kn->ffl;
                if (kn->flags & EV_CLEAR) { kn->active = 0; kn->ffl = 0; }
                if (kq_emit(kq, kn, &out[n++], 0, ffl, 0)) i--;
            }
        } else if (kn->filter == EVF_PROC) {
            task_t *t = task_find_by_pid((uint32_t)kn->ident);
            if (!t || t->state == TASK_ZOMBIE || t->state == TASK_DEAD) {
                int64_t st = t ? (int64_t)fb_wstatus((int32_t)((t->exit_code & 0xff) << 8)) : 0;
                kn->flags |= EV_ONESHOT;
                if (kq_emit(kq, kn, &out[n++], EV_EOF, 0x80000000u, st)) i--;
                continue;
            }
            uint64_t d = now + KQ_POLL_NS;
            if (!nx || d < nx) nx = d;
        } else if (kn->filter == EVF_SIGNAL) {
            int ls = fb_sig_to_linux((int)kn->ident);
            task_t *me = syscall_cur_task();
            if (ls > 0 && me) {
                uint64_t bit = 1ULL << ls;
                if (__atomic_fetch_and(&task_leader(me)->sig_posted, ~bit, __ATOMIC_RELAXED) & bit) {
                    if (kq_emit(kq, kn, &out[n++], 0, 0, 1)) i--;
                    continue;
                }
            }
            uint64_t d = now + KQ_POLL_NS;
            if (!nx || d < nx) nx = d;
        } else if (kn->filter == EVF_VNODE) {
            vfs_stat_t st;
            uint32_t ff = 0;
            if (fb_fd_stat(kn->ident, &st) < 0) ff |= 0x40;
            else {
                if (st.st_size > kn->vsize) ff |= 0x4 | 0x2;
                else if (st.st_size != kn->vsize || st.st_mtime != kn->vmtime) ff |= 0x2;
                if (st.st_nlink == 0 && kn->vnlink) ff |= 0x1;
                else if (st.st_nlink != kn->vnlink) ff |= 0x10;
                kn->vsize = st.st_size;
                kn->vmtime = st.st_mtime;
                kn->vnlink = st.st_nlink;
            }
            ff &= kn->sfflags;
            if (ff) {
                if (kq_emit(kq, kn, &out[n++], 0, ff, 0)) i--;
                continue;
            }
            uint64_t d = now + KQ_POLL_NS;
            if (!nx || d < nx) nx = d;
        }
    }
    if (next) *next = nx;
    return n;
}

static int kq_translate(kq_t *kq, const kq_epev_t *ev, int nev, fb_kevent_t *out, int max)
{
    int n = 0;
    for (int e = 0; e < nev && n < max; e++) {
        int fd = (int)ev[e].data;
        uint32_t got = ev[e].events;
        int eof = (got & (POLLHUP | 0x2000)) != 0;
        int err = (got & POLLERR) != 0;
        for (int i = 0; i < kq->n && n < max; i++) {
            kq_note_t *kn = &kq->notes[i];
            if (kn->ident != (uint64_t)fd || !kn->enabled) continue;
            if (kn->filter == EVF_READ && (got & (POLLIN | POLLHUP | POLLERR | 0x2000))) {
                if (kq_emit(kq, kn, &out[n++], eof || err ? EV_EOF : 0, 0, kq_fd_read_avail(fd))) i--;
            } else if (kn->filter == EVF_WRITE && (got & (POLLOUT | POLLHUP | POLLERR))) {
                if (kq_emit(kq, kn, &out[n++], eof || err ? EV_EOF : 0, 0, 65536)) i--;
            }
        }
    }
    return n;
}

static int kq_poll(vnode_t *vn, int events)
{
    kq_t *kq = vn->fs_data;
    if (!(events & POLLIN)) return 0;
    if (!kmutex_trylock(&kq->lock)) return 0;
    uint64_t now = sched_now_ns(), nx = 0;
    int ready = 0;
    for (int i = 0; i < kq->n; i++) {
        kq_note_t *kn = &kq->notes[i];
        if (!kn->enabled) continue;
        if (kn->filter == EVF_USER && kn->active) ready = 1;
        else if (kn->filter == EVF_TIMER) {
            if (now >= kn->next_ns) ready = 1;
            else if (!nx || kn->next_ns < nx) nx = kn->next_ns;
        } else if (kn->filter == EVF_PROC || kn->filter == EVF_VNODE || kn->filter == EVF_SIGNAL) {
            uint64_t d = now + KQ_POLL_NS;
            if (!nx || d < nx) nx = d;
        }
    }
    kmutex_unlock(&kq->lock);
    if (ready) return POLLIN;
    int r = epk_poll(kq->ep, events);
    if (!r && nx) poll_deadline(nx);
    return r;
}

static void kq_unref(vnode_t *vn)
{
    if (--vn->refcount > 0) return;
    kq_t *kq = vn->fs_data;
    epk_destroy(kq->ep);
    free(kq->notes);
    free(kq);
    free(vn);
}

static const vnode_ops_t KQ_OPS = {
    .poll  = kq_poll,
    .unref = kq_unref,
};

static int64_t fb_kqueue_new(int cloexec)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    kq_t *kq = calloc(1, sizeof *kq);
    vnode_t *vn = calloc(1, sizeof *vn);
    vfs_file_t *file = vfs_file_alloc();
    epoll_set_t *ep = epk_create();
    if (!kq || !vn || !file || !ep) {
        free(kq); free(vn);
        if (file) vfs_file_free(file);
        if (ep) epk_destroy(ep);
        return -ENOMEM;
    }
    kq->ep = ep;
    kmutex_init(&kq->lock);
    vn->type = VFS_NODE_CHARDEV;
    vn->ops = &KQ_OPS;
    vn->fs_data = kq;
    vn->refcount = 1;
    file->vnode = vn;
    file->flags = 2;
    int fd = fd_alloc(t->fd_table, file, 0);
    if (fd < 0) { vfs_file_free(file); return -EMFILE; }
    if (cloexec) fd_set_flags(t->fd_table, fd, FD_CLOEXEC);
    return fd;
}

FB0(fb_kqueue,  fb_kqueue_new(0))
FB0(fb_kqueuex, fb_kqueue_new((a1 & 1) != 0))

static int64_t fb_kevent(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int nch = (int)a3, nev = (int)a5;
    if (nch < 0 || nev < 0) return -EINVAL;
    if (nev > 4096) nev = 4096;
    vfs_file_t *f = fd_get(t->fd_table, (int)a1);
    if (!f) return -EBADF;
    if (!f->vnode || f->vnode->ops != &KQ_OPS) { fd_put(f); return -EBADF; }
    kq_t *kq = f->vnode->fs_data;
    int64_t timeout_ns = -1;
    if (a6) {
        int64_t ts[2];
        if (syscall_copy_from_user(ts, (const void *)a6, sizeof ts) < 0) { fd_put(f); return -EFAULT; }
        if (ts[0] < 0 || ts[1] < 0 || ts[1] >= 1000000000) { fd_put(f); return -EINVAL; }
        timeout_ns = ts[0] * 1000000000LL + ts[1];
    }
    fb_kevent_t *out = nev ? malloc((size_t)nev * sizeof *out) : NULL;
    kq_epev_t *ev = nev ? malloc((size_t)nev * sizeof *ev) : NULL;
    if (nev && (!out || !ev)) { free(out); free(ev); fd_put(f); return -ENOMEM; }
    int n = 0;
    int64_t rc = 0;
    kmutex_lock(&kq->lock);
    for (int i = 0; i < nch; i++) {
        fb_kevent_t ch;
        if (syscall_copy_from_user(&ch, (const void *)(a2 + (uint64_t)i * sizeof ch), sizeof ch) < 0) { rc = -EFAULT; break; }
        int r = kq_apply(kq, &ch);
        if (r < 0 || (ch.flags & EV_RECEIPT)) {
            if (n < nev) {
                out[n] = ch;
                out[n].flags |= EV_ERROR;
                out[n].data = r < 0 ? fbsd_errno_from_linux(-r) : 0;
                n++;
            } else if (r < 0) {
                rc = r;
                break;
            }
        }
    }
    kmutex_unlock(&kq->lock);
    uint64_t start = sched_now_ns();
    while (rc == 0 && n == 0 && nev > 0) {
        uint64_t nx = 0;
        kmutex_lock(&kq->lock);
        __atomic_store_n(&kq->kick, 0, __ATOMIC_RELEASE);
        n = kq_collect_soft(kq, out, nev, &nx);
        kmutex_unlock(&kq->lock);
        if (n) break;
        int64_t left = timeout_ns;
        if (timeout_ns > 0) {
            uint64_t el = sched_now_ns() - start;
            left = el >= (uint64_t)timeout_ns ? 0 : timeout_ns - (int64_t)el;
        }
        int64_t got = epk_wait(kq->ep, ev, nev, left, nx, &kq->kick);
        if (got < 0) { rc = got; break; }
        if (got > 0) {
            kmutex_lock(&kq->lock);
            n = kq_translate(kq, ev, (int)got, out, nev);
            kmutex_unlock(&kq->lock);
            if (n) break;
            continue;
        }
        if (left == 0) break;
        if (timeout_ns > 0 && sched_now_ns() - start >= (uint64_t)timeout_ns) break;
    }
    if (rc == 0 && n && syscall_copy_to_user((void *)a4, out, (size_t)n * sizeof *out) < 0) rc = -EFAULT;
    free(out);
    free(ev);
    fd_put(f);
    return rc < 0 ? rc : n;
}

#define FB_SFB_PEND 0x1u

uint64_t fbsd_fastblock_mask(task_t *t, uint64_t deliverable)
{
    uint32_t w;
    if (futex_user_load(t->fb_sigblock_ptr, &w) < 0) {
        t->fb_sigblock_ptr = 0;
        return deliverable;
    }
    if ((w >> 4) == 0) return deliverable;
    uint64_t keep = (1ULL << SIGKILL) | (1ULL << SIGSTOP);
    static const int faults[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP };
    for (size_t i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        int s = faults[i];
        if (t->sig_si[s].code > 0 && t->sig_si[s].code != SI_KERNEL_CODE) keep |= 1ULL << s;
    }
    uint64_t held = deliverable & ~keep;
    if (held) {
        for (;;) {
            uint32_t exp = w;
            if (w & FB_SFB_PEND) break;
            int c = futex_user_cas(t->fb_sigblock_ptr, &exp, w | FB_SFB_PEND);
            if (c != 0) break;
            w = exp;
        }
    }
    return deliverable & keep;
}

static int64_t fb_sigfastblock(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    switch (a1) {
    case 1:
        if (t->fb_sigblock_ptr) return -EBUSY;
        if (!a2 || (a2 & 3)) return -EINVAL;
        t->fb_sigblock_ptr = a2;
        return 0;
    case 2: {
        if (!t->fb_sigblock_ptr) return -EINVAL;
        uint32_t exp = FB_SFB_PEND;
        int c = futex_user_cas(t->fb_sigblock_ptr, &exp, 0);
        if (c < 0) { t->fb_sigblock_ptr = 0; return -EFAULT; }
        return c ? 0 : -EBUSY;
    }
    case 3: {
        if (!t->fb_sigblock_ptr) return -EINVAL;
        uint32_t w;
        if (futex_user_load(t->fb_sigblock_ptr, &w) < 0) return -EFAULT;
        if (w != 0 && w != FB_SFB_PEND) return -EBUSY;
        t->fb_sigblock_ptr = 0;
        return 0;
    }
    }
    return -EINVAL;
}

static int64_t fb_realpathat(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    char kp[VFS_MAX_PATH], out[VFS_MAX_PATH];
    int r = syscall_resolve_at(t, (int64_t)(int32_t)a1, a2, kp, sizeof kp);
    if (r < 0) return r;
    r = vfs_realpath(kp, out, sizeof out);
    if (r < 0) return r;
    size_t n = strlen(out) + 1;
    if (n > a4) return -ENAMETOOLONG;
    return syscall_copy_to_user((void *)a3, out, n);
}

static int64_t fb_swapcontext(A6)
{
    UNUSED6;
    int64_t r = freebsd_getcontext(a1);
    if (r < 0) return r;
    return freebsd_sigreturn(a2);
}

static const fb_fn_t g_fb_table[FB_NR_MAX] = {
    [1]   = fb_exit,        [2]   = fb_fork,        [3]   = fb_read,        [4]   = fb_write,
    [5]   = fb_open,        [6]   = fb_close,       [7]   = fb_wait4,       [9]   = fb_link,
    [10]  = fb_unlink,      [12]  = fb_chdir,       [13]  = fb_fchdir,      [15]  = fb_chmod,
    [16]  = fb_chown,       [17]  = fb_break,       [20]  = fb_getpid,      [23]  = fb_setuid,
    [24]  = fb_getuid,      [25]  = fb_getuid,      [27]  = fb_recvmsg,     [28]  = fb_sendmsg,
    [29]  = fb_recvfrom,    [30]  = fb_accept,
    [31]  = fb_getpeername, [32]  = fb_getsockname, [33]  = fb_access,      [34]  = fb_chflags,
    [35]  = fb_chflags,     [36]  = fb_sync,        [37]  = fb_kill,        [39]  = fb_getppid,
    [41]  = fb_dup,         [43]  = fb_getgid,      [47]  = fb_getgid,      [49]  = fb_getlogin,
    [50]  = fb_zero,        [53]  = fb_sigaltstack, [54]  = fb_ioctl,       [57]  = fb_symlink,
    [58]  = fb_readlink,    [59]  = fb_execve,      [60]  = fb_umask,       [61]  = fb_chroot,
    [65]  = fb_msync,       [66]  = fb_vfork,       [73]  = fb_munmap,      [74]  = fb_mprotect,
    [75]  = fb_madvise,     [78]  = fb_mincore,     [79]  = fb_getgroups,   [80]  = fb_zero,
    [81]  = fb_getpgrp,     [82]  = fb_setpgid,     [83]  = fb_setitimer,   [86]  = fb_getitimer,
    [89]  = fb_getdtablesize, [90] = fb_dup2,       [92]  = fb_fcntl,       [93]  = fb_select,
    [95]  = fb_fsync,       [96]  = fb_setpriority, [97]  = fb_socket,      [98]  = fb_connect,
    [100] = fb_getpriority, [104] = fb_bind,        [105] = fb_setsockopt,  [106] = fb_listen,
    [116] = fb_gettimeofday, [117] = fb_getrusage,  [118] = fb_getsockopt,  [120] = fb_readv,
    [121] = fb_writev,      [123] = fb_fchown,      [124] = fb_fchmod,      [126] = fb_zero,
    [127] = fb_zero,        [128] = fb_rename,      [131] = fb_flock,       [132] = fb_mkfifo,
    [133] = fb_sendto,      [134] = fb_shutdown,    [135] = fb_socketpair,  [136] = fb_mkdir,
    [137] = fb_rmdir,       [138] = fb_utimes,      [147] = fb_setsid,      [165] = fb_sysarch,
    [181] = fb_setgid,      [182] = fb_zero,        [183] = fb_zero,        [191] = fb_pathconf,
    [192] = fb_pathconf,    [194] = fb_getrlimit,   [195] = fb_setrlimit,   [202] = fb_sysctl,
    [203] = fb_mlock,       [204] = fb_munlock,     [206] = fb_futimes,     [207] = fb_getpgid,
    [209] = fb_poll,        [232] = fb_clock_gettime, [234] = fb_clock_getres, [240] = fb_nanosleep,
    [244] = fb_clock_nanosleep, [250] = fb_zero,    [251] = fb_rfork,       [253] = fb_issetugid,
    [254] = fb_lchown,      [276] = fb_lutimes,     [289] = fb_preadv,      [290] = fb_pwritev,
    [310] = fb_getsid,      [311] = fb_zero,        [312] = fb_zero,        [321] = fb_yield,
    [324] = fb_mlockall,    [325] = fb_munlockall,  [326] = fb_getcwd,      [331] = fb_yield,
    [335] = fb_zero,        [340] = fb_sigprocmask, [341] = fb_sigsuspend,  [343] = fb_sigpending,
    [345] = fb_sigtimedwait, [346] = fb_sigwaitinfo, [360] = fb_getresuid,  [361] = fb_getresgid,
    [362] = fb_kqueue,      [376] = fb_access,      [390] = fb_enosys,      [391] = fb_chflags,     [416] = fb_sigaction,
    [417] = fb_sigreturn,   [421] = fb_getcontext,  [422] = fb_sigreturn,   [423] = fb_swapcontext,
    [429] = fb_sigwait,     [431] = fb_thr_exit,    [432] = fb_thr_self,    [454] = fb_umtx_op,
    [455] = fb_thr_new,     [466] = fb_zero,        [166] = fb_zero,    [433] = fb_thr_kill,    [456] = fb_sigqueue,
    [464] = fb_zero,        [475] = fb_pread,       [476] = fb_pwrite,      [477] = fb_mmap,
    [478] = fb_lseek,       [479] = fb_truncate,    [480] = fb_ftruncate,   [481] = fb_thr_kill2,
    [484] = fb_einval,      [485] = fb_zero,        [486] = fb_einval,      [487] = fb_cpuset_getaffinity,
    [488] = fb_zero,        [489] = fb_faccessat,   [490] = fb_fchmodat,    [491] = fb_fchownat,
    [492] = fb_fexecve,     [494] = fb_futimesat,   [495] = fb_linkat,      [496] = fb_mkdirat,
    [497] = fb_mkfifoat,    [499] = fb_openat,      [500] = fb_readlinkat,  [501] = fb_renameat,
    [502] = fb_symlinkat,   [503] = fb_unlinkat,    [504] = fb_posix_openpt, [513] = fb_pathconf,
    [516] = fb_zero,        [517] = fb_cap_getmode, [518] = fb_pdfork,      [519] = fb_pdkill,
    [520] = fb_pdgetpid,    [530] = fb_posix_fallocate, [532] = fb_wait6, [531] = fb_posix_fadvise, [533] = fb_zero,        [534] = fb_zero,
    [535] = fb_zero,        [536] = fb_zero,        [537] = fb_zero,        [569] = fb_copy_file_range,        [522] = fb_pselect,     [540] = fb_chflagsat,
    [541] = fb_accept4,     [542] = fb_pipe2,       [544] = fb_einval,      [545] = fb_ppoll,
    [546] = fb_futimens,    [547] = fb_utimensat,   [550] = fb_fdatasync,   [551] = fb_fstat,
    [552] = fb_fstatat,     [554] = fb_getdirentries, [555] = fb_statfs,    [556] = fb_fstatfs,
    [557] = fb_getfsstat,   [559] = fb_mknodat,     [563] = fb_getrandom,   [570] = fb_sysctlbyname,
    [560] = fb_kevent,      [583] = fb_kqueuex,     [573] = fb_sigfastblock,      [592] = fb_enosys,      [574] = fb_realpathat,      [575] = fb_close_range, [577] = fb_specialfd,
    [581] = fb_sched_getcpu, [595] = fb_getgroups,  [596] = fb_zero,
};

static volatile uint64_t g_fb_reported[FB_NR_MAX / 64 + 1];

int64_t fbsd_syscall(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5, uint64_t a6)
{
    if (nr == 0 || nr == 198) {
        task_t *t = syscall_cur_task();
        uint64_t a7 = 0;
        if (t) syscall_copy_from_user(&a7, (const void *)(t->user_rsp + 8), sizeof a7);
        nr = a1;
        a1 = a2; a2 = a3; a3 = a4; a4 = a5; a5 = a6; a6 = a7;
    }
    task_t *me = syscall_cur_task();
    fb_fn_t fn = nr < FB_NR_MAX ? g_fb_table[nr] : NULL;
    if (me && me->lx_trace) {
        int64_t r = fn ? fn(a1, a2, a3, a4, a5, a6) : -ENOSYS;
        serial_printf("[fbtrace] %u %llu(%llx, %llx, %llx, %llx, %llx) = %lld\n", me->pid,
                      (unsigned long long)nr, (unsigned long long)a1, (unsigned long long)a2,
                      (unsigned long long)a3, (unsigned long long)a4, (unsigned long long)a5, (long long)r);
        return r;
    }
    if (fn) return fn(a1, a2, a3, a4, a5, a6);
    if (nr < FB_NR_MAX) {
        uint64_t bit = 1ULL << (nr & 63);
        if (!(__atomic_fetch_or(&g_fb_reported[nr / 64], bit, __ATOMIC_RELAXED) & bit))
            serial_printf("[freebsd] %s: unimplemented syscall %llu\n", me ? me->name : "?",
                          (unsigned long long)nr);
    }
    return -ENOSYS;
}

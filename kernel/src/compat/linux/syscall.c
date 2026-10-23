#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/sched/sched.h"
#include "../../../include/drivers/random.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/memory/mm.h"
#include "../../../include/smp/smp.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/io/serial.h"
#include <string.h>
#include <stdbool.h>

typedef int64_t (*lx_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

#define LX_NR_MAX 512

#define LX_AT_FDCWD          (-100)
#define LX_AT_SYMLINK_NOFOLLOW 0x100
#define LX_AT_EMPTY_PATH     0x1000

#define LX_TCGETS  0x5401
#define LX_TCSETS  0x5402
#define LX_TCSETSW 0x5403
#define LX_TCSETSF 0x5404

extern int64_t sys_exit(uint64_t);
extern int64_t sys_exit_group(uint64_t);
extern int64_t sys_getpid(void);
extern int64_t sys_getppid(void);
extern int64_t sys_getuid(void);
extern int64_t sys_getgid(void);
extern int64_t sys_setuid(uint64_t);
extern int64_t sys_setgid(uint64_t);
extern int64_t sys_fork(void);
extern int64_t sys_execve(uint64_t, uint64_t, uint64_t);
extern int64_t sys_yield(void);
extern int64_t sys_read(uint64_t, uint64_t, uint64_t);
extern int64_t sys_write(uint64_t, uint64_t, uint64_t);
extern int64_t sys_open(uint64_t, uint64_t, uint64_t);
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
extern int64_t sys_mremap(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_mincore(uint64_t, uint64_t, uint64_t);
extern int64_t sys_mlock(uint64_t, uint64_t);
extern int64_t sys_munlock(uint64_t, uint64_t);
extern int64_t sys_mlockall(uint64_t);
extern int64_t sys_munlockall(void);
extern int64_t sys_clock_get(uint64_t, uint64_t);
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
extern int64_t sys_alarm(uint64_t);
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
extern int64_t sys_utimensat(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_fchdir(uint64_t);
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
extern int64_t sys_sched_setaffinity(uint64_t, uint64_t);
extern int64_t sys_sched_getaffinity(uint64_t);
extern int64_t sys_sched_setscheduler(uint64_t, uint64_t, uint64_t);
extern int64_t sys_sched_getscheduler(uint64_t, uint64_t);
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
extern int64_t sys_sendmsg(uint64_t, uint64_t, uint64_t);
extern int64_t sys_recvmsg(uint64_t, uint64_t, uint64_t);
extern int64_t sys_sock_shutdown(uint64_t, uint64_t);
extern int64_t sys_getsockname(uint64_t, uint64_t, uint64_t);
extern int64_t sys_getpeername(uint64_t, uint64_t, uint64_t);
extern int64_t sys_socketpair(uint64_t, uint64_t, uint64_t);
extern int64_t sys_setsockopt(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_getsockopt(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_poll(uint64_t, uint64_t, uint64_t);
extern int64_t sys_select(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_ppoll(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_pselect(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_epoll_create(uint64_t, uint64_t, uint64_t);
extern int64_t sys_epoll_ctl(uint64_t, uint64_t, uint64_t);
extern int64_t sys_epoll_wait(uint64_t, uint64_t, uint64_t);
extern int64_t sys_eventfd(uint64_t, uint64_t, uint64_t);
extern int64_t sys_timerfd_create(uint64_t, uint64_t, uint64_t);
extern int64_t sys_timerfd_settime2(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_timerfd_gettime(uint64_t, uint64_t);
extern int64_t sys_signalfd(uint64_t, uint64_t, uint64_t);
extern int64_t sys_memfd_create(uint64_t, uint64_t, uint64_t);
extern int64_t sys_futex(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_arch_prctl(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_prctl(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_personality(uint64_t);
extern int64_t sys_ptrace(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_chroot(uint64_t);
extern int64_t sys_execveat(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_timer_create(uint64_t, uint64_t, uint64_t);
extern int64_t sys_timer_settime(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_timer_gettime(uint64_t, uint64_t);
extern int64_t sys_timer_getoverrun(uint64_t);
extern int64_t sys_timer_delete(uint64_t);
extern int64_t sys_shmget(uint64_t, uint64_t, uint64_t);
extern int64_t sys_shmat(uint64_t, uint64_t, uint64_t);
extern int64_t sys_shmdt(uint64_t);
extern int64_t sys_shmctl(uint64_t, uint64_t, uint64_t);
extern int64_t sys_semget(uint64_t, uint64_t, uint64_t);
extern int64_t sys_semop(uint64_t, uint64_t, uint64_t);
extern int64_t sys_semctl(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_semtimedop(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_msgget(uint64_t, uint64_t);
extern int64_t sys_msgsnd(uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_msgrcv(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_msgctl(uint64_t, uint64_t, uint64_t);
extern int64_t clock_realtime_offset_ns(void);
extern int syscall_resolve_at(task_t *t, int64_t dirfd, uint64_t upath, char *out, size_t cap);

#define A6 uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6
#define UNUSED6 (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6

#define LX0(name, expr)  static int64_t name(A6) { UNUSED6; return (expr); }

LX0(lx_read,       sys_read(a1, a2, a3))
LX0(lx_write,      sys_write(a1, a2, a3))
LX0(lx_open,       sys_open(a1, a2, a3))
LX0(lx_close,      sys_close(a1))
LX0(lx_poll,       sys_poll(a1, a2, a3))
LX0(lx_lseek,      sys_seek(a1, a2, a3))
LX0(lx_mmap,       sys_mmap(a1, a2, a3, a4, a5, a6))
LX0(lx_mprotect,   sys_mprotect(a1, a2, a3))
LX0(lx_munmap,     sys_munmap(a1, a2))
LX0(lx_rt_sigaction,   sys_rt_sigaction(a1, a2, a3, a4))
LX0(lx_rt_sigprocmask, sys_rt_sigprocmask(a1, a2, a3, a4))
LX0(lx_pread64,    sys_pread(a1, a2, a3, a4))
LX0(lx_pwrite64,   sys_pwrite(a1, a2, a3, a4))
LX0(lx_readv,      sys_readv(a1, a2, a3))
LX0(lx_writev,     sys_writev(a1, a2, a3))
LX0(lx_pipe,       sys_pipe(a1))
LX0(lx_select,     sys_select(a1, a2, a3, a4, a5))
LX0(lx_sched_yield, sys_yield())
LX0(lx_mremap,     sys_mremap(a1, a2, a3, a4, a5))
LX0(lx_msync,      sys_msync(a1, a2, a3))
LX0(lx_mincore,    sys_mincore(a1, a2, a3))
LX0(lx_madvise,    sys_madvise(a1, a2, a3))
LX0(lx_shmget,     sys_shmget(a1, a2, a3))
LX0(lx_shmat,      sys_shmat(a1, a2, a3))
LX0(lx_shmctl,     sys_shmctl(a1, a2, a3))
LX0(lx_dup,        sys_dup(a1))
LX0(lx_dup2,       sys_dup2(a1, a2))
LX0(lx_getitimer,  sys_getitimer(a1, a2))
LX0(lx_alarm,      sys_alarm(a1))
LX0(lx_setitimer,  sys_setitimer(a1, a2, a3))
LX0(lx_getpid,     sys_getpid())
LX0(lx_socket,     sys_socket(a1, a2, a3))
LX0(lx_connect,    sys_connect(a1, a2, a3))
LX0(lx_accept,     sys_accept4(a1, a2, a3, 0))
LX0(lx_sendto,     sys_sendto_flags(a1, a2, a3, a4, a5, a6))
LX0(lx_recvfrom,   sys_recvfrom_flags(a1, a2, a3, a4, a5, a6))
LX0(lx_sendmsg,    sys_sendmsg(a1, a2, a3))
LX0(lx_recvmsg,    sys_recvmsg(a1, a2, a3))
LX0(lx_shutdown,   sys_sock_shutdown(a1, a2))
LX0(lx_bind,       sys_bind(a1, a2, a3))
LX0(lx_listen,     sys_listen(a1, a2))
LX0(lx_getsockname, sys_getsockname(a1, a2, a3))
LX0(lx_getpeername, sys_getpeername(a1, a2, a3))
LX0(lx_socketpair, sys_socketpair(a1, a2, a3))
LX0(lx_setsockopt, sys_setsockopt(a1, a2, a3, a4, a5))
LX0(lx_getsockopt, sys_getsockopt(a1, a2, a3, a4, a5))
LX0(lx_fork,       sys_fork())
LX0(lx_execve,     sys_execve(a1, a2, a3))
LX0(lx_exit,       sys_exit(a1))
LX0(lx_wait4,      sys_wait4(a1, a2, a3, a4))
LX0(lx_kill,       sys_kill(a1, a2))
LX0(lx_semget,     sys_semget(a1, a2, a3))
LX0(lx_semop,      sys_semop(a1, a2, a3))
LX0(lx_semctl,     sys_semctl(a1, a2, a3, a4))
LX0(lx_shmdt,      sys_shmdt(a1))
LX0(lx_msgget,     sys_msgget(a1, a2))
LX0(lx_msgsnd,     sys_msgsnd(a1, a2, a3, a4))
LX0(lx_msgrcv,     sys_msgrcv(a1, a2, a3, a4, a5))
LX0(lx_msgctl,     sys_msgctl(a1, a2, a3))
LX0(lx_fcntl,      sys_fcntl(a1, a2, a3))
LX0(lx_fsync,      sys_fsync(a1))
LX0(lx_fdatasync,  sys_fdatasync(a1))
LX0(lx_truncate,   sys_truncate(a1, a2))
LX0(lx_ftruncate,  sys_ftruncate(a1, a2))
LX0(lx_getcwd,     sys_getcwd(a1, a2, 0, 0, 0, 0))
LX0(lx_chdir,      sys_chdir(a1, 0, 0, 0, 0, 0))
LX0(lx_fchdir,     sys_fchdir(a1))
LX0(lx_rename,     sys_rename(a1, a2, 0, 0, 0, 0))
LX0(lx_mkdir,      sys_mkdir(a1, a2, 0, 0, 0, 0))
LX0(lx_rmdir,      sys_rmdir(a1, 0, 0, 0, 0, 0))
LX0(lx_creat,      sys_open(a1, 0x241, a2))
LX0(lx_link,       sys_link(a1, a2))
LX0(lx_unlink,     sys_unlink(a1, 0, 0, 0, 0, 0))
LX0(lx_symlink,    sys_symlink(a1, a2))
LX0(lx_readlink,   sys_readlink(a1, a2, a3))
LX0(lx_chmod,      sys_chmod(a1, a2))
LX0(lx_fchmod,     sys_fchmod(a1, a2))
LX0(lx_chown,      sys_chown(a1, a2, a3))
LX0(lx_fchown,     sys_fchown(a1, a2, a3))
LX0(lx_lchown,     sys_lchown(a1, a2, a3))
LX0(lx_umask,      sys_umask(a1))
LX0(lx_getrusage,  sys_getrusage(a1, a2))
LX0(lx_ptrace,     sys_ptrace(a1, a2, a3, a4))
LX0(lx_getuid,     sys_getuid())
LX0(lx_getgid,     sys_getgid())
LX0(lx_setuid,     sys_setuid(a1))
LX0(lx_setgid,     sys_setgid(a1))
LX0(lx_setpgid,    sys_setpgid(a1, a2))
LX0(lx_getppid,    sys_getppid())
LX0(lx_getpgrp,    sys_getpgid(0))
LX0(lx_setsid,     sys_setsid())
LX0(lx_getpgid,    sys_getpgid(a1))
LX0(lx_getsid,     sys_getsid(a1))
LX0(lx_rt_sigpending, sys_rt_sigpending(a1, a2))
LX0(lx_rt_sigtimedwait, sys_sigtimedwait(a1, a2, a3))
LX0(lx_rt_sigqueueinfo, sys_sigqueue(a1, a2, a3))
LX0(lx_rt_sigsuspend, sys_rt_sigsuspend(a1, a2))
LX0(lx_sigaltstack, sys_sigaltstack(a1, a2))
LX0(lx_personality, sys_personality(a1))
LX0(lx_getpriority, sys_getpriority(a1, a2))
LX0(lx_setpriority, sys_setpriority(a1, a2, a3))
LX0(lx_sched_setscheduler, sys_sched_setscheduler(a1, a2, a3))
LX0(lx_sched_getscheduler, sys_sched_getscheduler(a1, 0))
LX0(lx_mlock,      sys_mlock(a1, a2))
LX0(lx_munlock,    sys_munlock(a1, a2))
LX0(lx_mlockall,   sys_mlockall(a1))
LX0(lx_munlockall, sys_munlockall())
LX0(lx_prctl,      sys_prctl(a1, a2, a3, a4, a5))
LX0(lx_arch_prctl, sys_arch_prctl(a1, a2, a3, a4, a5, a6))
LX0(lx_chroot,     sys_chroot(a1))
LX0(lx_sync,       sys_sync(0, 0, 0, 0, 0, 0))
LX0(lx_gettid,     sys_gettid())
LX0(lx_tkill,      sys_tkill(a1, a2))
LX0(lx_futex,      sys_futex(a1, a2, a3, a4, a5, a6))
LX0(lx_timer_create, sys_timer_create(a1, a2, a3))
LX0(lx_timer_settime, sys_timer_settime(a1, a2, a3, a4))
LX0(lx_timer_gettime, sys_timer_gettime(a1, a2))
LX0(lx_timer_getoverrun, sys_timer_getoverrun(a1))
LX0(lx_timer_delete, sys_timer_delete(a1))
LX0(lx_clock_gettime, sys_clock_get(a1, a2))
LX0(lx_exit_group, sys_exit_group(a1))
LX0(lx_epoll_ctl,  sys_epoll_ctl(a1, a2, a3))
LX0(lx_tgkill,     sys_tgkill(a1, a2, a3))
LX0(lx_waitid,     sys_waitid(a1, a2, a3, a4))
LX0(lx_openat,     sys_openat(a1, a2, a3, a4))
LX0(lx_mkdirat,    sys_mkdirat(a1, a2, a3))
LX0(lx_mknodat,    sys_mknodat(a1, a2, a3, a4))
LX0(lx_fchownat,   sys_fchownat(a1, a2, a3, a4, a5))
LX0(lx_unlinkat,   sys_unlinkat(a1, a2, a3))
LX0(lx_renameat,   sys_renameat(a1, a2, a3, a4))
LX0(lx_linkat,     sys_linkat(a1, a2, a3, a4, a5))
LX0(lx_symlinkat,  sys_symlinkat(a1, a2, a3))
LX0(lx_readlinkat, sys_readlinkat(a1, a2, a3, a4))
LX0(lx_fchmodat,   sys_fchmodat(a1, a2, a3, a4))
LX0(lx_pselect6,   sys_pselect(a1, a2, a3, a4, a5, a6))
LX0(lx_ppoll,      sys_ppoll(a1, a2, a3, a4, a5))
LX0(lx_utimensat,  sys_utimensat(a1, a2, a3, a4, a5))
LX0(lx_signalfd4,  sys_signalfd(a1, a2, a4))
LX0(lx_timerfd_create, sys_timerfd_create(a1, a2, 0))
LX0(lx_eventfd2,   sys_eventfd(a1, a2, 0))
LX0(lx_eventfd,    sys_eventfd(a1, 0, 0))
LX0(lx_timerfd_settime, sys_timerfd_settime2(a1, a2, a3, a4))
LX0(lx_timerfd_gettime, sys_timerfd_gettime(a1, a2))
LX0(lx_accept4,    sys_accept4(a1, a2, a3, a4))
LX0(lx_epoll_create1, sys_epoll_create(a1, 0, 0))
LX0(lx_epoll_create, sys_epoll_create(0, 0, 0))
LX0(lx_preadv,     sys_preadv(a1, a2, a3, a4))
LX0(lx_pwritev,    sys_pwritev(a1, a2, a3, a4))
LX0(lx_memfd_create, sys_memfd_create(a1, a2, 0))
LX0(lx_execveat,   sys_execveat(a1, a2, a3, a4, a5))
LX0(lx_semtimedop, sys_semtimedop(a1, a2, a3, a4))
LX0(lx_close_range, sys_close_range(a1, a2, a3))
LX0(lx_geteuid,    sys_getuid())
LX0(lx_getegid,    sys_getgid())
LX0(lx_enosys,     -ENOSYS)

extern int64_t linux_rt_sigreturn(void);
LX0(lx_rt_sigreturn, linux_rt_sigreturn())

static int64_t lx_dup3(A6)
{
    UNUSED6;
    if (a1 == a2) return -EINVAL;
    int64_t r = sys_dup2(a1, a2);
    if (r >= 0 && (a3 & 02000000)) sys_fcntl((uint64_t)r, 2, 1);
    return r;
}

static int64_t lx_pipe2(A6)
{
    UNUSED6;
    int fds[2];
    int64_t r = sys_pipe(a1);
    if (r < 0 || !a2) return r;
    if (syscall_copy_from_user(fds, (const void *)a1, sizeof fds) < 0) return -EFAULT;
    for (int i = 0; i < 2; i++) {
        if (a2 & 02000000) sys_fcntl((uint64_t)fds[i], 2, 1);
        if (a2 & 04000) sys_fcntl((uint64_t)fds[i], 4, 04000);
    }
    return 0;
}

static int64_t lx_brk(A6)
{
    UNUSED6;
    int64_t r = sys_brk(a1);
    if (r < 0) return sys_brk(0);
    return r;
}

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    int64_t  st_atime_nsec;
    int64_t  st_mtime;
    int64_t  st_mtime_nsec;
    int64_t  st_ctime;
    int64_t  st_ctime_nsec;
    int64_t  unused[3];
} lx_stat_t;

_Static_assert(sizeof(lx_stat_t) == 144, "struct stat on x86_64 Linux is 144 bytes");

static uint32_t lx_type_bits(vnode_type_t t)
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

static uint8_t lx_dtype(uint8_t t)
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

static void lx_stat_from(lx_stat_t *o, const vfs_stat_t *s)
{
    memset(o, 0, sizeof *o);
    o->st_dev     = s->st_dev;
    o->st_ino     = s->st_ino;
    o->st_nlink   = s->st_nlink ? s->st_nlink : 1;
    o->st_mode    = (s->st_mode & 07777) | lx_type_bits(s->st_type);
    o->st_uid     = s->st_uid;
    o->st_gid     = s->st_gid;
    o->st_rdev    = s->st_rdev;
    o->st_size    = (int64_t)s->st_size;
    o->st_blksize = s->st_blksize ? (int64_t)s->st_blksize : 4096;
    o->st_blocks  = (int64_t)s->st_blocks;
    o->st_atime   = s->st_atime;
    o->st_mtime   = s->st_mtime;
    o->st_ctime   = s->st_ctime;
}

static int lx_fd_stat(uint64_t fd, vfs_stat_t *st)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    int r = vfs_fstat(f, st);
    fd_put(f);
    return r;
}

static int lx_path_stat(int64_t dirfd, uint64_t path, uint64_t flags, vfs_stat_t *st)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (flags & LX_AT_EMPTY_PATH) {
        char first = 1;
        if (!path || (syscall_copy_from_user(&first, (const void *)path, 1) == 0 && first == 0)) {
            if ((int)dirfd != LX_AT_FDCWD) return lx_fd_stat((uint64_t)dirfd, st);
            return vfs_stat(t->cwd[0] ? t->cwd : "/", st);
        }
    }
    char kp[VFS_MAX_PATH];
    int r = syscall_resolve_at(t, dirfd, path, kp, sizeof kp);
    if (r < 0) return r;
    return (flags & LX_AT_SYMLINK_NOFOLLOW) ? vfs_lstat(kp, st) : vfs_stat(kp, st);
}

static int64_t lx_put_stat(uint64_t uptr, const vfs_stat_t *st)
{
    lx_stat_t o;
    lx_stat_from(&o, st);
    return syscall_copy_to_user((void *)uptr, &o, sizeof o);
}

static int64_t lx_newfstatat(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = lx_path_stat((int64_t)a1, a2, a4, &st);
    if (r < 0) return r;
    return lx_put_stat(a3, &st);
}

static int64_t lx_stat(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = lx_path_stat(LX_AT_FDCWD, a1, 0, &st);
    if (r < 0) return r;
    return lx_put_stat(a2, &st);
}

static int64_t lx_lstat(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = lx_path_stat(LX_AT_FDCWD, a1, LX_AT_SYMLINK_NOFOLLOW, &st);
    if (r < 0) return r;
    return lx_put_stat(a2, &st);
}

static int64_t lx_fstat(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = lx_fd_stat(a1, &st);
    if (r < 0) return r;
    return lx_put_stat(a2, &st);
}

typedef struct {
    int64_t  tv_sec;
    uint32_t tv_nsec;
    int32_t  reserved;
} lx_statx_ts_t;

typedef struct {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t pad0;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    lx_statx_ts_t stx_atime;
    lx_statx_ts_t stx_btime;
    lx_statx_ts_t stx_ctime;
    lx_statx_ts_t stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t spare[14];
} lx_statx_t;

_Static_assert(sizeof(lx_statx_t) == 256, "struct statx is 256 bytes");

static uint32_t dev_major(uint64_t d) { return (uint32_t)(((d >> 32) & 0xfffff000u) | ((d >> 8) & 0xfffu)); }
static uint32_t dev_minor(uint64_t d) { return (uint32_t)(((d >> 12) & 0xffffff00u) | (d & 0xffu)); }

static int64_t lx_statx(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = lx_path_stat((int64_t)a1, a2, a3, &st);
    if (r < 0) return r;
    lx_statx_t x;
    memset(&x, 0, sizeof x);
    x.stx_mask    = 0x7ff;
    x.stx_blksize = st.st_blksize ? (uint32_t)st.st_blksize : 4096;
    x.stx_nlink   = st.st_nlink ? (uint32_t)st.st_nlink : 1;
    x.stx_uid     = st.st_uid;
    x.stx_gid     = st.st_gid;
    x.stx_mode    = (uint16_t)((st.st_mode & 07777) | lx_type_bits(st.st_type));
    x.stx_ino     = st.st_ino;
    x.stx_size    = st.st_size;
    x.stx_blocks  = st.st_blocks;
    x.stx_atime.tv_sec = st.st_atime;
    x.stx_btime.tv_sec = st.st_ctime;
    x.stx_ctime.tv_sec = st.st_ctime;
    x.stx_mtime.tv_sec = st.st_mtime;
    x.stx_rdev_major = dev_major(st.st_rdev);
    x.stx_rdev_minor = dev_minor(st.st_rdev);
    x.stx_dev_major  = dev_major(st.st_dev);
    x.stx_dev_minor  = dev_minor(st.st_dev);
    return syscall_copy_to_user((void *)a5, &x, sizeof x);
}

static int64_t lx_faccessat2(A6)
{
    UNUSED6;
    vfs_stat_t st;
    int r = lx_path_stat((int64_t)a1, a2, a4 & LX_AT_SYMLINK_NOFOLLOW, &st);
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

static int64_t lx_faccessat(A6) { UNUSED6; return lx_faccessat2(a1, a2, a3, 0, 0, 0); }
static int64_t lx_access(A6)    { UNUSED6; return lx_faccessat2((uint64_t)(int64_t)LX_AT_FDCWD, a1, a2, 0, 0, 0); }

static int64_t lx_getdents64(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (!syscall_uptr_validate((void *)a2, a3)) return -EFAULT;
    vfs_file_t *f = fd_get(t->fd_table, (int)a1);
    if (!f) return -EBADF;
    uint8_t *out = (uint8_t *)a2;
    size_t cap = a3, used = 0;
    int64_t rc = 0;
    while (cap - used >= 24 + VFS_MAX_NAME) {
        vfs_dirent_t kd;
        int r = vfs_readdir(f, &kd);
        if (r < 0) { if (r == -ENOTDIR) rc = r; break; }
        size_t nl = strnlen(kd.d_name, VFS_MAX_NAME - 1);
        size_t reclen = (19 + nl + 1 + 7) & ~(size_t)7;
        uint8_t rec[24 + VFS_MAX_NAME];
        memset(rec, 0, reclen);
        uint64_t off = f->offset;
        uint16_t rl = (uint16_t)reclen;
        memcpy(rec, &kd.d_ino, 8);
        memcpy(rec + 8, &off, 8);
        memcpy(rec + 16, &rl, 2);
        rec[18] = lx_dtype(kd.d_type);
        memcpy(rec + 19, kd.d_name, nl);
        if (syscall_copy_to_user(out + used, rec, reclen) < 0) { rc = -EFAULT; break; }
        used += reclen;
    }
    fd_put(f);
    if (used) return (int64_t)used;
    return rc;
}

typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} lx_utsname_t;

static void lx_hostname(char *out, size_t cap)
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
    size_t i = 0;
    while (buf[i] && buf[i] != '\n' && buf[i] != ' ' && i < cap - 1) { out[i] = buf[i]; i++; }
    if (i) out[i] = 0;
}

static int64_t lx_uname(A6)
{
    UNUSED6;
    lx_utsname_t u;
    memset(&u, 0, sizeof u);
    strcpy(u.sysname, "Linux");
    lx_hostname(u.nodename, sizeof u.nodename);
    strcpy(u.release, "6.12.0-cervus");
    strcpy(u.version, "#1 SMP PREEMPT Cervus 0.0.2");
    strcpy(u.machine, "x86_64");
    strcpy(u.domainname, "(none)");
    return syscall_copy_to_user((void *)a1, &u, sizeof u);
}

typedef struct {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t  c_cc[32];
} cervus_termios_t;

typedef struct {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
} lx_termios_t;

_Static_assert(sizeof(cervus_termios_t) == 48, "Cervus termios is 48 bytes");
_Static_assert(sizeof(lx_termios_t) == 36, "Linux kernel termios is 36 bytes");

#define LX_TCGETS2  0x802c542a
#define LX_TCSETS2  0x402c542b
#define LX_TCSETSW2 0x402c542c
#define LX_TCSETSF2 0x402c542d

typedef struct {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
    uint32_t c_ispeed, c_ospeed;
} lx_termios2_t;

_Static_assert(sizeof(lx_termios2_t) == 44, "Linux termios2 is 44 bytes");

static uint64_t lx_termios_native_req(uint64_t req)
{
    switch (req) {
    case LX_TCGETS: case LX_TCGETS2: return LX_TCGETS;
    case LX_TCSETS: case LX_TCSETS2: return LX_TCSETS;
    case LX_TCSETSW: case LX_TCSETSW2: return LX_TCSETSW;
    case LX_TCSETSF: case LX_TCSETSF2: return LX_TCSETSF;
    }
    return 0;
}

static int64_t lx_ioctl(A6)
{
    UNUSED6;
    uint64_t native = lx_termios_native_req(a2);
    if (!native) return sys_ioctl(a1, a2, a3);
    bool v2 = a2 == LX_TCGETS2 || a2 == LX_TCSETS2 || a2 == LX_TCSETSW2 || a2 == LX_TCSETSF2;
    size_t usz = v2 ? sizeof(lx_termios2_t) : sizeof(lx_termios_t);
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)a1);
    if (!f) return -EBADF;
    cervus_termios_t ct;
    memset(&ct, 0, sizeof ct);
    int64_t r = vfs_ioctl(f, LX_TCGETS, &ct);
    if (r >= 0) {
        lx_termios2_t lt;
        memset(&lt, 0, sizeof lt);
        if (native == LX_TCGETS) {
            lt.c_iflag = ct.c_iflag; lt.c_oflag = ct.c_oflag;
            lt.c_cflag = ct.c_cflag; lt.c_lflag = ct.c_lflag;
            memcpy(lt.c_cc, ct.c_cc, sizeof lt.c_cc);
            lt.c_ispeed = lt.c_ospeed = 38400;
            r = syscall_copy_to_user((void *)a3, &lt, usz);
        } else if (syscall_copy_from_user(&lt, (const void *)a3, usz) < 0) {
            r = -EFAULT;
        } else {
            ct.c_iflag = lt.c_iflag; ct.c_oflag = lt.c_oflag;
            ct.c_cflag = lt.c_cflag; ct.c_lflag = lt.c_lflag;
            memcpy(ct.c_cc, lt.c_cc, sizeof lt.c_cc);
            r = vfs_ioctl(f, native, &ct);
        }
    }
    fd_put(f);
    return r;
}

extern int64_t epoll_wait_args(uint64_t epfd, uint64_t events_ptr, int maxevents, int timeout_ms);

static int64_t lx_epoll_wait(A6)
{
    UNUSED6;
    return epoll_wait_args(a1, a2, (int)a3, (int)a4);
}

static int64_t lx_epoll_pwait(A6)
{
    UNUSED6;
    uint64_t old = 0;
    task_t *t = syscall_cur_task();
    if (a5 && t) {
        uint64_t m;
        if (syscall_copy_from_user(&m, (const void *)a5, sizeof m) < 0) return -EFAULT;
        old = t->sig_blocked;
        t->sig_blocked = m & ~((1ULL << 9) | (1ULL << 19));
    }
    int64_t r = lx_epoll_wait(a1, a2, a3, a4, 0, a6);
    if (a5 && t) {
        t->sig_saved_mask = old;
        t->sig_saved_valid = 1;
        if (r != -EINTR) { t->sig_blocked = old; t->sig_saved_valid = 0; }
    }
    return r;
}

typedef struct { int64_t tv_sec; int64_t tv_nsec; } lx_timespec_t;

extern int64_t sys_sleep_ns(uint64_t ns);

static int64_t lx_sleep(uint64_t ns, uint64_t rem_ptr)
{
    uint64_t start = sched_now_ns();
    int64_t r = sys_sleep_ns(ns);
    if (r == -EINTR && rem_ptr) {
        uint64_t done = sched_now_ns() - start;
        uint64_t left = done < ns ? ns - done : 0;
        lx_timespec_t rem = { (int64_t)(left / 1000000000ULL), (int64_t)(left % 1000000000ULL) };
        syscall_copy_to_user((void *)rem_ptr, &rem, sizeof rem);
    }
    return r < 0 ? r : 0;
}

static int lx_get_timespec(uint64_t uptr, uint64_t *ns)
{
    lx_timespec_t ts;
    if (syscall_copy_from_user(&ts, (const void *)uptr, sizeof ts) < 0) return -EFAULT;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000) return -EINVAL;
    *ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    return 0;
}

static int64_t lx_nanosleep(A6)
{
    UNUSED6;
    uint64_t ns;
    int r = lx_get_timespec(a1, &ns);
    if (r < 0) return r;
    return lx_sleep(ns, a2);
}

static uint64_t lx_clock_ns(uint64_t clk)
{
    uint64_t mono = sched_now_ns();
    if (clk == 0 || clk == 5 || clk == 8 || clk == 11) return (uint64_t)((int64_t)mono + clock_realtime_offset_ns());
    return mono;
}

static int64_t lx_clock_nanosleep(A6)
{
    UNUSED6;
    uint64_t ns;
    int r = lx_get_timespec(a3, &ns);
    if (r < 0) return r;
    if (a2 & 1) {
        uint64_t now = lx_clock_ns(a1);
        if (ns <= now) return 0;
        return lx_sleep(ns - now, 0);
    }
    return lx_sleep(ns, a4);
}

static int64_t lx_clock_getres(A6)
{
    UNUSED6;
    if (!a2) return 0;
    lx_timespec_t ts = { 0, 1 };
    return syscall_copy_to_user((void *)a2, &ts, sizeof ts);
}

static int64_t lx_gettimeofday(A6)
{
    UNUSED6;
    if (a1) {
        uint64_t ns = lx_clock_ns(0);
        struct { int64_t sec, usec; } tv = { (int64_t)(ns / 1000000000ULL), (int64_t)((ns % 1000000000ULL) / 1000) };
        if (syscall_copy_to_user((void *)a1, &tv, sizeof tv) < 0) return -EFAULT;
    }
    if (a2) {
        int32_t tz[2] = { 0, 0 };
        if (syscall_copy_to_user((void *)a2, tz, sizeof tz) < 0) return -EFAULT;
    }
    return 0;
}

static int64_t lx_time(A6)
{
    UNUSED6;
    int64_t s = (int64_t)(lx_clock_ns(0) / 1000000000ULL);
    if (a1 && syscall_copy_to_user((void *)a1, &s, sizeof s) < 0) return -EFAULT;
    return s;
}

typedef struct {
    int64_t  uptime;
    uint64_t loads[3];
    uint64_t totalram, freeram, sharedram, bufferram, totalswap, freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t pad2;
    uint64_t totalhigh, freehigh;
    uint32_t mem_unit;
    char     reserved[4];
} lx_sysinfo_t;

static int64_t lx_sysinfo(A6)
{
    UNUSED6;
    lx_sysinfo_t si;
    memset(&si, 0, sizeof si);
    uint64_t usable = (uint64_t)pmm_get_usable_pages() * PAGE_SIZE;
    uint64_t used   = (uint64_t)pmm_get_used_pages() * PAGE_SIZE;
    if (used > usable) used = usable;
    si.uptime   = (int64_t)(sched_now_ns() / 1000000000ULL);
    si.totalram = usable;
    si.freeram  = usable - used;
    si.procs    = 1;
    si.mem_unit = 1;
    return syscall_copy_to_user((void *)a1, &si, sizeof si);
}

typedef struct {
    int64_t f_type, f_bsize;
    uint64_t f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int32_t f_fsid[2];
    int64_t f_namelen, f_frsize, f_flags;
    int64_t f_spare[4];
} lx_statfs_t;

static int64_t lx_statfs_out(const char *kp, uint64_t uptr)
{
    vfs_statvfs_t sv;
    int r = vfs_statvfs(kp, &sv);
    if (r < 0) return r;
    lx_statfs_t o;
    memset(&o, 0, sizeof o);
    o.f_type    = 0xEF53;
    o.f_bsize   = (int64_t)sv.f_bsize;
    o.f_blocks  = sv.f_blocks;
    o.f_bfree   = sv.f_bfree;
    o.f_bavail  = sv.f_bavail;
    o.f_files   = sv.f_files;
    o.f_ffree   = sv.f_ffree;
    o.f_namelen = sv.f_namemax ? sv.f_namemax : 255;
    o.f_frsize  = (int64_t)sv.f_bsize;
    return syscall_copy_to_user((void *)uptr, &o, sizeof o);
}

static int64_t lx_statfs(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    char kp[VFS_MAX_PATH];
    int r = syscall_resolve_at(t, LX_AT_FDCWD, a1, kp, sizeof kp);
    if (r < 0) return r;
    return lx_statfs_out(kp, a2);
}

static int64_t lx_fstatfs(A6)
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
    return lx_statfs_out(kp, a2);
}

static int64_t lx_getrandom(A6)
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

static int64_t lx_set_tid_address(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    t->clear_tid = a1;
    return sys_gettid();
}

static int64_t lx_set_robust_list(A6)
{
    UNUSED6;
    return a2 == 24 ? 0 : -EINVAL;
}

static int64_t lx_getrlimit(A6) { UNUSED6; return sys_prlimit(0, a1, 0, a2); }
static int64_t lx_setrlimit(A6) { UNUSED6; return sys_prlimit(0, a1, a2, 0); }
static int64_t lx_prlimit64(A6) { UNUSED6; return sys_prlimit(a1, a2, a3, a4); }

static int64_t lx_sched_getaffinity(A6)
{
    UNUSED6;
    if (a2 < sizeof(uint64_t) || (a2 & 7)) return -EINVAL;
    int64_t m = sys_sched_getaffinity(a1);
    if (m < 0) return m;
    uint64_t mask = (uint64_t)m;
    if (syscall_copy_to_user((void *)a3, &mask, sizeof mask) < 0) return -EFAULT;
    return sizeof mask;
}

static int64_t lx_sched_setaffinity(A6)
{
    UNUSED6;
    uint64_t mask = 0;
    if (a2 < sizeof mask) return -EINVAL;
    if (syscall_copy_from_user(&mask, (const void *)a3, sizeof mask) < 0) return -EFAULT;
    return sys_sched_setaffinity(a1, mask);
}

static int64_t lx_getgroups(A6)
{
    UNUSED6;
    if (a1 == 0) return 1;
    task_t *t = syscall_cur_task();
    uint32_t g = t ? t->gid : 0;
    if (syscall_copy_to_user((void *)a2, &g, sizeof g) < 0) return -EFAULT;
    return 1;
}

static int64_t lx_getresuid(A6)
{
    UNUSED6;
    uint32_t u = (uint32_t)sys_getuid();
    if (syscall_copy_to_user((void *)a1, &u, 4) < 0 || syscall_copy_to_user((void *)a2, &u, 4) < 0 ||
        syscall_copy_to_user((void *)a3, &u, 4) < 0) return -EFAULT;
    return 0;
}

static int64_t lx_getresgid(A6)
{
    UNUSED6;
    uint32_t g = (uint32_t)sys_getgid();
    if (syscall_copy_to_user((void *)a1, &g, 4) < 0 || syscall_copy_to_user((void *)a2, &g, 4) < 0 ||
        syscall_copy_to_user((void *)a3, &g, 4) < 0) return -EFAULT;
    return 0;
}

static int64_t lx_renameat2(A6)
{
    UNUSED6;
    if (a5) return -EINVAL;
    return sys_renameat(a1, a2, a3, a4);
}

static int64_t lx_getcpu(A6)
{
    UNUSED6;
    uint32_t c = smp_cpu_index(), n = 0;
    if (a1 && syscall_copy_to_user((void *)a1, &c, 4) < 0) return -EFAULT;
    if (a2 && syscall_copy_to_user((void *)a2, &n, 4) < 0) return -EFAULT;
    return 0;
}

static int64_t lx_clone(A6)
{
    UNUSED6;
    if (a1 & 0x00000100) return -ENOSYS;
    int64_t pid = sys_fork();
    if (pid <= 0) return pid;
    task_t *child = task_find_by_pid((uint32_t)pid);
    if (child) {
        if ((a1 & 0x01000000) && child->mm) {
            int32_t v = (int32_t)pid;
            mm_copy_to(child->mm, a4, &v, sizeof v);
        }
        if (a1 & 0x00200000) child->clear_tid = a4;
    }
    if (a1 & 0x00100000) {
        int32_t v = (int32_t)pid;
        syscall_copy_to_user((void *)a3, &v, sizeof v);
    }
    return pid;
}

LX0(lx_zero,       0)
LX0(lx_enotsup,    -EOPNOTSUPP)
LX0(lx_enodata,    -ENODATA)

static int64_t lx_xattr_list(A6)
{
    UNUSED6;
    return 0;
}

static int64_t lx_times(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    uint64_t ticks = sched_now_ns() / 10000000ULL;
    if (a1 && t) {
        uint64_t ut = t->total_runtime / 10000000ULL;
        int64_t tms[4] = { (int64_t)ut, 0, 0, 0 };
        if (syscall_copy_to_user((void *)a1, tms, sizeof tms) < 0) return -EFAULT;
    }
    return (int64_t)ticks;
}

static int64_t lx_sched_getparam(A6)
{
    UNUSED6;
    int32_t prio = 0;
    return syscall_copy_to_user((void *)a2, &prio, sizeof prio);
}

static int64_t lx_sched_prio_max(A6) { UNUSED6; return (a1 == 1 || a1 == 2) ? 99 : 0; }
static int64_t lx_sched_prio_min(A6) { UNUSED6; return (a1 == 1 || a1 == 2) ? 1 : 0; }

static int64_t lx_sched_rr_get_interval(A6)
{
    UNUSED6;
    lx_timespec_t ts = { 0, 10000000 };
    return syscall_copy_to_user((void *)a2, &ts, sizeof ts);
}

static int64_t lx_capget(A6)
{
    UNUSED6;
    uint32_t hdr[2];
    if (syscall_copy_from_user(hdr, (const void *)a1, sizeof hdr) < 0) return -EFAULT;
    if (!a2) return 0;
    task_t *t = syscall_cur_task();
    uint32_t full = (t && t->uid == 0) ? 0xFFFFFFFFu : 0;
    uint32_t data[6] = { full, full, 0, full, full, 0 };
    return syscall_copy_to_user((void *)a2, data, sizeof data);
}

static int64_t lx_flock(A6)
{
    UNUSED6;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)a1);
    if (!f) return -EBADF;
    fd_put(f);
    return 0;
}

static const lx_fn_t g_lx_table[LX_NR_MAX] = {
    [0]   = lx_read,        [1]   = lx_write,       [2]   = lx_open,        [3]   = lx_close,
    [4]   = lx_stat,        [5]   = lx_fstat,       [6]   = lx_lstat,       [7]   = lx_poll,
    [8]   = lx_lseek,       [9]   = lx_mmap,        [10]  = lx_mprotect,    [11]  = lx_munmap,
    [12]  = lx_brk,         [13]  = lx_rt_sigaction, [14] = lx_rt_sigprocmask, [15] = lx_rt_sigreturn,
    [16]  = lx_ioctl,       [17]  = lx_pread64,     [18]  = lx_pwrite64,    [19]  = lx_readv,
    [20]  = lx_writev,      [21]  = lx_access,      [22]  = lx_pipe,        [23]  = lx_select,
    [24]  = lx_sched_yield, [25]  = lx_mremap,      [26]  = lx_msync,       [27]  = lx_mincore,
    [28]  = lx_madvise,     [29]  = lx_shmget,      [30]  = lx_shmat,       [31]  = lx_shmctl,
    [32]  = lx_dup,         [33]  = lx_dup2,        [35]  = lx_nanosleep,   [36]  = lx_getitimer,
    [37]  = lx_alarm,       [38]  = lx_setitimer,   [39]  = lx_getpid,      [41]  = lx_socket,
    [42]  = lx_connect,     [43]  = lx_accept,      [44]  = lx_sendto,      [45]  = lx_recvfrom,
    [46]  = lx_sendmsg,     [47]  = lx_recvmsg,     [48]  = lx_shutdown,    [49]  = lx_bind,
    [50]  = lx_listen,      [51]  = lx_getsockname, [52]  = lx_getpeername, [53]  = lx_socketpair,
    [54]  = lx_setsockopt,  [55]  = lx_getsockopt,  [56]  = lx_clone,       [57]  = lx_fork,
    [58]  = lx_fork,        [59]  = lx_execve,      [60]  = lx_exit,        [61]  = lx_wait4,
    [62]  = lx_kill,        [63]  = lx_uname,       [64]  = lx_semget,      [65]  = lx_semop,
    [66]  = lx_semctl,      [67]  = lx_shmdt,       [68]  = lx_msgget,      [69]  = lx_msgsnd,
    [70]  = lx_msgrcv,      [71]  = lx_msgctl,      [72]  = lx_fcntl,       [74]  = lx_fsync,
    [75]  = lx_fdatasync,   [76]  = lx_truncate,    [77]  = lx_ftruncate,   [79]  = lx_getcwd,
    [80]  = lx_chdir,       [81]  = lx_fchdir,      [82]  = lx_rename,      [83]  = lx_mkdir,
    [84]  = lx_rmdir,       [85]  = lx_creat,       [86]  = lx_link,        [87]  = lx_unlink,
    [88]  = lx_symlink,     [89]  = lx_readlink,    [90]  = lx_chmod,       [91]  = lx_fchmod,
    [92]  = lx_chown,       [93]  = lx_fchown,      [94]  = lx_lchown,      [95]  = lx_umask,
    [96]  = lx_gettimeofday, [97] = lx_getrlimit,   [98]  = lx_getrusage,   [99]  = lx_sysinfo,
    [101] = lx_ptrace,      [102] = lx_getuid,      [104] = lx_getgid,      [105] = lx_setuid,
    [106] = lx_setgid,      [107] = lx_geteuid,     [108] = lx_getegid,     [109] = lx_setpgid,
    [110] = lx_getppid,     [111] = lx_getpgrp,     [112] = lx_setsid,      [115] = lx_getgroups,
    [118] = lx_getresuid,   [120] = lx_getresgid,   [121] = lx_getpgid,     [124] = lx_getsid,
    [127] = lx_rt_sigpending, [128] = lx_rt_sigtimedwait, [129] = lx_rt_sigqueueinfo,
    [130] = lx_rt_sigsuspend, [131] = lx_sigaltstack, [135] = lx_personality,
    [137] = lx_statfs,      [138] = lx_fstatfs,     [140] = lx_getpriority, [141] = lx_setpriority,
    [144] = lx_sched_setscheduler, [145] = lx_sched_getscheduler,
    [149] = lx_mlock,       [150] = lx_munlock,     [151] = lx_mlockall,    [152] = lx_munlockall,
    [157] = lx_prctl,       [158] = lx_arch_prctl,  [160] = lx_setrlimit,   [161] = lx_chroot,
    [162] = lx_sync,        [186] = lx_gettid,      [200] = lx_tkill,       [201] = lx_time,
    [202] = lx_futex,       [203] = lx_sched_setaffinity, [204] = lx_sched_getaffinity,
    [213] = lx_epoll_create, [217] = lx_getdents64, [218] = lx_set_tid_address,
    [222] = lx_timer_create, [223] = lx_timer_settime, [224] = lx_timer_gettime,
    [225] = lx_timer_getoverrun, [226] = lx_timer_delete, [228] = lx_clock_gettime,
    [229] = lx_clock_getres, [230] = lx_clock_nanosleep, [231] = lx_exit_group,
    [232] = lx_epoll_wait,  [233] = lx_epoll_ctl,   [234] = lx_tgkill,      [247] = lx_waitid,
    [257] = lx_openat,      [258] = lx_mkdirat,     [259] = lx_mknodat,     [260] = lx_fchownat,
    [262] = lx_newfstatat,  [263] = lx_unlinkat,    [264] = lx_renameat,    [265] = lx_linkat,
    [266] = lx_symlinkat,   [267] = lx_readlinkat,  [268] = lx_fchmodat,    [269] = lx_faccessat,
    [270] = lx_pselect6,    [271] = lx_ppoll,       [273] = lx_set_robust_list,
    [280] = lx_utimensat,   [281] = lx_epoll_pwait, [283] = lx_timerfd_create, [284] = lx_eventfd,
    [286] = lx_timerfd_settime, [287] = lx_timerfd_gettime, [288] = lx_accept4,
    [289] = lx_signalfd4,   [290] = lx_eventfd2,    [291] = lx_epoll_create1, [292] = lx_dup3,
    [293] = lx_pipe2,       [295] = lx_preadv,      [296] = lx_pwritev,     [302] = lx_prlimit64,
    [309] = lx_getcpu,      [316] = lx_renameat2,   [318] = lx_getrandom,   [319] = lx_memfd_create,
    [322] = lx_execveat,    [332] = lx_statx,       [334] = lx_enosys,      [435] = lx_enosys,
    [436] = lx_close_range, [439] = lx_faccessat2,  [220] = lx_semtimedop,
    [73]  = lx_flock,       [100] = lx_times,       [125] = lx_capget,      [126] = lx_zero,
    [142] = lx_zero,        [143] = lx_sched_getparam, [146] = lx_sched_prio_max,
    [147] = lx_sched_prio_min, [148] = lx_sched_rr_get_interval, [187] = lx_zero,
    [188] = lx_enotsup,     [189] = lx_enotsup,     [190] = lx_enotsup,     [191] = lx_enodata,
    [192] = lx_enodata,     [193] = lx_enodata,     [194] = lx_xattr_list,  [195] = lx_xattr_list,
    [196] = lx_xattr_list,  [197] = lx_enodata,     [198] = lx_enodata,     [199] = lx_enodata,
    [221] = lx_zero,        [277] = lx_zero,        [306] = lx_zero,        [324] = lx_enosys,
    [253] = lx_enosys,      [254] = lx_enosys,      [255] = lx_enosys,      [294] = lx_enosys,
};

static volatile uint64_t g_lx_reported[LX_NR_MAX / 64];

int64_t linux_syscall(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
                      uint64_t a4, uint64_t a5, uint64_t a6)
{
    if (nr < LX_NR_MAX && g_lx_table[nr]) return g_lx_table[nr](a1, a2, a3, a4, a5, a6);
    if (nr < LX_NR_MAX) {
        uint64_t bit = 1ULL << (nr & 63);
        if (!(__atomic_fetch_or(&g_lx_reported[nr / 64], bit, __ATOMIC_RELAXED) & bit)) {
            task_t *t = syscall_cur_task();
            serial_printf("[linux] %s: unimplemented syscall %llu\n", t ? t->name : "?",
                          (unsigned long long)nr);
        }
    }
    return -ENOSYS;
}

bool linux_syscall_known(uint64_t nr)
{
    return nr < LX_NR_MAX;
}

bool linux_syscall_restartable(uint64_t nr)
{
    switch (nr) {
    case 0: case 1: case 2: case 16: case 17: case 18: case 19: case 20:
    case 42: case 43: case 44: case 45: case 46: case 47: case 61: case 72:
    case 247: case 257: case 288: case 295: case 296:
        return true;
    }
    return false;
}

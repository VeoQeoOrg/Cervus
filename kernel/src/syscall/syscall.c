#include "../../include/syscall/syscall.h"
#include "../../include/syscall/syscall_nums.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/syscall/errno.h"
#include "../../include/sched/sched.h"
#include "../../include/smp/smp.h"
#include "../../include/smp/percpu.h"
#include "../../include/gdt/gdt.h"
#include "../../include/memory/vmm.h"
#include "../../include/io/serial.h"
#include "../../include/panic/panic.h"
#include <stdint.h>

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084
#define EFER_SCE   (1ULL << 0)
#define EFER_NXE   (1ULL << 11)

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t val)
{
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val),
                            "d"((uint32_t)(val >> 32)));
}

extern int64_t sys_exit       (uint64_t code);
extern int64_t sys_exit_group (uint64_t code);
extern int64_t sys_getpid     (void);
extern int64_t sys_getppid    (void);
extern int64_t sys_getuid     (void);
extern int64_t sys_getgid     (void);
extern int64_t sys_setuid     (uint64_t u);
extern int64_t sys_setgid     (uint64_t g);
extern int64_t sys_fork       (void);
extern int64_t sys_execve     (uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr);
extern int64_t sys_wait       (uint64_t pid_arg, uint64_t status_ptr, uint64_t flags);
extern int64_t sys_yield      (void);
extern int64_t sys_cap_get    (void);
extern int64_t sys_cap_drop   (uint64_t mask);
extern int64_t sys_task_info  (uint64_t pid_arg, uint64_t buf_ptr);
extern int64_t sys_task_kill  (uint64_t pid_arg);

extern int64_t sys_read       (uint64_t fd, uint64_t buf_ptr, uint64_t count);
extern int64_t sys_write      (uint64_t fd, uint64_t buf_ptr, uint64_t count);
extern int64_t sys_open       (uint64_t path_ptr, uint64_t flags, uint64_t mode);
extern int64_t sys_close      (uint64_t fd);
extern int64_t sys_seek       (uint64_t fd, uint64_t offset, uint64_t whence);
extern int64_t sys_stat       (uint64_t path_ptr, uint64_t stat_ptr);
extern int64_t sys_fstat      (uint64_t fd, uint64_t stat_ptr);
extern int64_t sys_ioctl      (uint64_t fd, uint64_t request, uint64_t arg_ptr);
extern int64_t sys_dup        (uint64_t fd);
extern int64_t sys_dup2       (uint64_t oldfd, uint64_t newfd);
extern int64_t sys_pipe       (uint64_t fds_ptr);
extern int64_t sys_fcntl      (uint64_t fd, uint64_t cmd, uint64_t arg);
extern int64_t sys_readdir    (uint64_t fd, uint64_t dirent_ptr);

extern int64_t sys_brk        (uint64_t new_brk);
extern int64_t sys_mmap       (uint64_t hint, uint64_t length, uint64_t prot,
                               uint64_t flags, uint64_t fd, uint64_t offset);
extern int64_t sys_munmap     (uint64_t addr, uint64_t length);
extern int64_t sys_meminfo    (uint64_t buf_ptr);

extern int64_t sys_clock_get  (uint64_t id, uint64_t ts_ptr);
extern int64_t sys_sleep_ns   (uint64_t ns);
extern int64_t sys_uptime     (void);

extern int64_t sys_dbg_print   (uint64_t str, uint64_t len);
extern int64_t sys_ioport_read (uint64_t port, uint64_t width);
extern int64_t sys_ioport_write(uint64_t port, uint64_t width, uint64_t val);
extern int64_t sys_shutdown    (void);
extern int64_t sys_reboot      (void);

extern int64_t sys_disk_mount        (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_disk_umount       (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_disk_format       (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_disk_info         (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_disk_read_raw     (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_disk_write_raw    (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_disk_partition    (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_disk_mkfs_fat32   (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_disk_list_parts   (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_disk_bios_install (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_unlink            (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_rmdir             (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_mkdir             (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_rename            (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_list_mounts       (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_statvfs           (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

typedef int64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

#define W0(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return fn();}
#define W1(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return fn(a);}
#define W2(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)c;(void)d;(void)e;(void)f;return fn(a,b);}
#define W3(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return fn(a,b,c);}
#define W6(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){return fn(a,b,c,d,e,f);}

W1(sys_exit)        W1(sys_exit_group)
W0(sys_getpid)      W0(sys_getppid)
W0(sys_getuid)      W0(sys_getgid)
W1(sys_setuid)      W1(sys_setgid)
W0(sys_fork)        W0(sys_yield)
W0(sys_cap_get)     W1(sys_cap_drop)
W2(sys_task_info)   W1(sys_task_kill)
W3(sys_read)        W3(sys_write)
W3(sys_open)        W1(sys_close)
W3(sys_seek)        W2(sys_stat)
W2(sys_fstat)       W1(sys_dup)
W2(sys_dup2)        W1(sys_pipe)
W3(sys_fcntl)
W3(sys_ioctl)
W2(sys_readdir)
W1(sys_brk)         W6(sys_mmap)
W2(sys_munmap)
W2(sys_clock_get)   W1(sys_sleep_ns)   W0(sys_uptime)   W1(sys_meminfo)
W2(sys_dbg_print)
W2(sys_ioport_read) W3(sys_ioport_write)
W0(sys_shutdown)    W0(sys_reboot)

W3(sys_execve)
W3(sys_wait)

static const syscall_fn_t syscall_table[SYSCALL_TABLE_SIZE] = {
    [SYS_EXIT]              = _sys_exit,
    [SYS_EXIT_GROUP]        = _sys_exit_group,
    [SYS_GETPID]            = _sys_getpid,
    [SYS_GETPPID]           = _sys_getppid,
    [SYS_GETUID]            = _sys_getuid,
    [SYS_GETGID]            = _sys_getgid,
    [SYS_SETUID]            = _sys_setuid,
    [SYS_SETGID]            = _sys_setgid,
    [SYS_FORK]              = _sys_fork,
    [SYS_EXECVE]            = _sys_execve,
    [SYS_WAIT]              = _sys_wait,
    [SYS_YIELD]             = _sys_yield,
    [SYS_CAP_GET]           = _sys_cap_get,
    [SYS_CAP_DROP]          = _sys_cap_drop,
    [SYS_TASK_INFO]         = _sys_task_info,
    [SYS_TASK_KILL]         = _sys_task_kill,
    [SYS_READ]              = _sys_read,
    [SYS_WRITE]             = _sys_write,
    [SYS_OPEN]              = _sys_open,
    [SYS_CLOSE]             = _sys_close,
    [SYS_SEEK]              = _sys_seek,
    [SYS_STAT]              = _sys_stat,
    [SYS_FSTAT]             = _sys_fstat,
    [SYS_IOCTL]             = _sys_ioctl,
    [SYS_DUP]               = _sys_dup,
    [SYS_DUP2]              = _sys_dup2,
    [SYS_PIPE]              = _sys_pipe,
    [SYS_FCNTL]             = _sys_fcntl,
    [SYS_READDIR]           = _sys_readdir,
    [SYS_BRK]               = _sys_brk,
    [SYS_MMAP]              = _sys_mmap,
    [SYS_MUNMAP]            = _sys_munmap,
    [SYS_CLOCK_GET]         = _sys_clock_get,
    [SYS_SLEEP_NS]          = _sys_sleep_ns,
    [SYS_UPTIME]            = _sys_uptime,
    [SYS_MEMINFO]           = _sys_meminfo,
    [SYS_DBG_PRINT]         = _sys_dbg_print,
    [SYS_IOPORT_READ]       = _sys_ioport_read,
    [SYS_IOPORT_WRITE]      = _sys_ioport_write,
    [SYS_SHUTDOWN]          = _sys_shutdown,
    [SYS_REBOOT]            = _sys_reboot,
    [SYS_DISK_MOUNT]        = sys_disk_mount,
    [SYS_DISK_UMOUNT]       = sys_disk_umount,
    [SYS_DISK_FORMAT]       = sys_disk_format,
    [SYS_DISK_INFO]         = sys_disk_info,
    [SYS_UNLINK]            = sys_unlink,
    [SYS_RMDIR]             = sys_rmdir,
    [SYS_MKDIR]             = sys_mkdir,
    [SYS_RENAME]            = sys_rename,
    [SYS_DISK_READ_RAW]     = sys_disk_read_raw,
    [SYS_DISK_WRITE_RAW]    = sys_disk_write_raw,
    [SYS_DISK_PARTITION]    = sys_disk_partition,
    [SYS_DISK_MKFS_FAT32]   = sys_disk_mkfs_fat32,
    [SYS_DISK_LIST_PARTS]   = sys_disk_list_parts,
    [SYS_DISK_BIOS_INSTALL] = sys_disk_bios_install,
    [SYS_LIST_MOUNTS]       = sys_list_mounts,
    [SYS_STATVFS]           = sys_statvfs,
};

__attribute__((noreturn)) void sysret_bad_rip_panic(uint64_t bad_rip, uint64_t retval)
{
    task_t *t = syscall_cur_task();
    serial_printf("[SYSRET-PANIC] Non-canonical user RIP=0x%llx before SYSRET!\n"
                  "  syscall retval=0x%llx task=%s pid=%u\n"
                  "  user_saved_rip=0x%llx user_rsp=0x%llx cr3=0x%llx\n",
                  bad_rip, retval,
                  t ? t->name : "?", t ? t->pid : 0,
                  t ? t->user_saved_rip : 0,
                  t ? t->user_rsp : 0,
                  t ? t->cr3 : 0);
    kernel_panic("SYSRET: non-canonical user RIP — would have caused triple fault");
}

__attribute__((noreturn)) void sysret_bad_rsp_panic(uint64_t bad_rsp, uint64_t user_rip)
{
    task_t *t = syscall_cur_task();
    serial_printf("[SYSRET-PANIC] Non-canonical user RSP=0x%llx before SYSRET!\n"
                  "  user_rip=0x%llx task=%s pid=%u\n"
                  "  user_saved_rip=0x%llx user_rsp=0x%llx cr3=0x%llx\n",
                  bad_rsp, user_rip,
                  t ? t->name : "?", t ? t->pid : 0,
                  t ? t->user_saved_rip : 0,
                  t ? t->user_rsp : 0,
                  t ? t->cr3 : 0);
    kernel_panic("SYSRET: non-canonical user RSP — would have caused triple fault");
}

int64_t syscall_handler_c(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t user_rip)
{
    (void)user_rip;
    task_t *t = syscall_cur_task();
    if (t) {
        syscall_save_user_regs(t);
    }
    if (nr >= SYSCALL_TABLE_SIZE || !syscall_table[nr]) {
        serial_printf("[SYSCALL] unknown nr=%llu\n", nr);
        return -ENOSYS;
    }

    int64_t ret = syscall_table[nr](a1, a2, a3, a4, a5, 0);

    task_t *me = syscall_cur_task();
    if (me && me->pending_kill) {
        me->pending_kill = false;
        me->exit_code = 130;
        vmm_switch_pagemap(vmm_get_kernel_pagemap());
        task_exit();
    }
    return ret;
}

void syscall_init(void)
{
    extern void syscall_entry(void);

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE | EFER_NXE);

    uint64_t star = ((uint64_t)GDT_STAR_SYSRET_BASE << 48)
                  | ((uint64_t)GDT_STAR_SYSCALL_CS  << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, (1U << 9) | (1U << 10) | (1U << 8) | (1U << 18));

    percpu_t *pc = get_percpu();
    if (!pc) {
        serial_printf("[SYSCALL] WARNING: no percpu, skipping kernel_rsp\n");
        return;
    }

    extern tss_t *tss[MAX_CPUS];
    cpu_info_t *cpu_info = smp_get_current_cpu();
    if (!cpu_info) return;

    uint32_t idx = cpu_info->cpu_index;
    if (idx < MAX_CPUS && tss[idx]) {
        pc->syscall_kernel_rsp = tss[idx]->rsp0;
        serial_printf("[SYSCALL] CPU %u (index %u): kernel_rsp=0x%llx\n",
                      pc->cpu_id, idx, pc->syscall_kernel_rsp);
    }
}

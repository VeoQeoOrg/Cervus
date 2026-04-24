#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <stdint.h>

#define SYS_EXIT              0
#define SYS_EXIT_GROUP        1
#define SYS_GETPID            2
#define SYS_GETPPID           3
#define SYS_FORK              4
#define SYS_WAIT              5
#define SYS_YIELD             6
#define SYS_GETUID            7
#define SYS_GETGID            8
#define SYS_SETUID            9
#define SYS_SETGID           10
#define SYS_CAP_GET          11
#define SYS_CAP_DROP         12
#define SYS_TASK_INFO        13
#define SYS_EXECVE           14

#define SYS_READ             20
#define SYS_WRITE            21
#define SYS_OPEN             22
#define SYS_CLOSE            23
#define SYS_SEEK             24
#define SYS_STAT             25
#define SYS_FSTAT            26
#define SYS_IOCTL            27
#define SYS_DUP              28
#define SYS_DUP2             29
#define SYS_PIPE             30
#define SYS_FCNTL            31
#define SYS_READDIR          32

#define SYS_MMAP             40
#define SYS_MUNMAP           41
#define SYS_MPROTECT         42
#define SYS_BRK              43

#define SYS_CLOCK_GET        60
#define SYS_SLEEP_NS         61
#define SYS_UPTIME           62
#define SYS_MEMINFO          63

#define SYS_FUTEX_WAIT       80
#define SYS_FUTEX_WAKE       81

#define SYS_DBG_PRINT       512
#define SYS_TASK_KILL       515
#define SYS_IOPORT_READ     521
#define SYS_IOPORT_WRITE    522
#define SYS_SHUTDOWN        523
#define SYS_REBOOT          524

#define SYS_DISK_MOUNT      530
#define SYS_DISK_UMOUNT     531
#define SYS_DISK_FORMAT     532
#define SYS_DISK_INFO       533
#define SYS_UNLINK          534
#define SYS_RMDIR           535
#define SYS_MKDIR           536
#define SYS_RENAME          537

#define SYS_DISK_READ_RAW   540
#define SYS_DISK_WRITE_RAW  541
#define SYS_DISK_PARTITION  542
#define SYS_DISK_MKFS_FAT32 543
#define SYS_DISK_LIST_PARTS 544
#define SYS_DISK_BIOS_INSTALL 545

#define SYS_LIST_MOUNTS     546
#define SYS_STATVFS         547

static inline int64_t
__syscall6(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
           uint64_t a4, uint64_t a5, uint64_t a6)
{
    int64_t ret;
    register uint64_t r10 asm("r10") = a4;
    register uint64_t r8  asm("r8")  = a5;
    register uint64_t r9  asm("r9")  = a6;
    asm volatile ("syscall"
                  : "=a"(ret)
                  : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                  : "rcx", "r11", "memory");
    return ret;
}

#define syscall0(n)             __syscall6((n), 0, 0, 0, 0, 0, 0)
#define syscall1(n,a)           __syscall6((n), (uint64_t)(a), 0, 0, 0, 0, 0)
#define syscall2(n,a,b)         __syscall6((n), (uint64_t)(a), (uint64_t)(b), 0, 0, 0, 0)
#define syscall3(n,a,b,c)       __syscall6((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), 0, 0, 0)
#define syscall4(n,a,b,c,d)     __syscall6((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), (uint64_t)(d), 0, 0)
#define syscall5(n,a,b,c,d,e)   __syscall6((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), (uint64_t)(d), (uint64_t)(e), 0)
#define syscall6(n,a,b,c,d,e,f) __syscall6((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), (uint64_t)(d), (uint64_t)(e), (uint64_t)(f))

#endif

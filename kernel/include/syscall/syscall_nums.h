#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

#define SYS_EXIT          0
#define SYS_EXIT_GROUP    1
#define SYS_GETPID        2
#define SYS_GETPPID       3
#define SYS_FORK          4
#define SYS_WAIT          5
#define SYS_EXECVE        14
#define SYS_YIELD         6
#define SYS_GETUID        7
#define SYS_GETGID        8
#define SYS_SETUID        9
#define SYS_SETGID       10
#define SYS_CAP_GET      11
#define SYS_CAP_DROP     12
#define SYS_TASK_INFO    13

#define SYS_READ         20
#define SYS_WRITE        21
#define SYS_OPEN         22
#define SYS_CLOSE        23
#define SYS_SEEK         24
#define SYS_STAT         25
#define SYS_FSTAT        26
#define SYS_IOCTL        27
#define SYS_DUP          28
#define SYS_DUP2         29
#define SYS_PIPE         30
#define SYS_FCNTL        31
#define SYS_READDIR      32

#define SYS_MMAP         40
#define SYS_MUNMAP       41
#define SYS_MPROTECT     42
#define SYS_BRK          43

#define SYS_CLOCK_GET    60
#define SYS_SLEEP_NS     61
#define SYS_UPTIME       62
#define SYS_MEMINFO      63

#define SYS_FUTEX_WAIT   80
#define SYS_FUTEX_WAKE   81

#define SYS_CERVUS_BASE       512

#define SYS_DBG_PRINT         512
#define SYS_DBG_DUMP          513
#define SYS_TASK_SPAWN        514
#define SYS_TASK_KILL         515
#define SYS_SHMEM_CREATE      516
#define SYS_SHMEM_MAP         517
#define SYS_SHMEM_UNMAP       518
#define SYS_IPC_SEND          519
#define SYS_IPC_RECV          520
#define SYS_IOPORT_READ       521
#define SYS_IOPORT_WRITE      522
#define SYS_SHUTDOWN          523
#define SYS_REBOOT            524

#define SYS_DISK_MOUNT        530
#define SYS_DISK_UMOUNT       531
#define SYS_DISK_FORMAT       532
#define SYS_DISK_INFO         533
#define SYS_UNLINK            534
#define SYS_RMDIR             535
#define SYS_MKDIR             536
#define SYS_RENAME            537

#define SYS_DISK_READ_RAW     540
#define SYS_DISK_WRITE_RAW    541
#define SYS_DISK_PARTITION    542
#define SYS_DISK_MKFS_FAT32   543
#define SYS_DISK_LIST_PARTS   544

#define SYS_DISK_BIOS_INSTALL 545
#define SYS_LIST_MOUNTS       546
#define SYS_STATVFS           547

#define SYSCALL_TABLE_SIZE    548

#define PROT_NONE    0x0
#define PROT_READ    0x1
#define PROT_WRITE   0x2
#define PROT_EXEC    0x4

#define MAP_PRIVATE    0x02
#define MAP_ANONYMOUS  0x20
#define MAP_FIXED      0x10
#define MAP_FAILED     ((void*)-1)

#define WNOHANG    0x1

#define CLOCK_REALTIME   0
#define CLOCK_MONOTONIC  1

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t gid;
    uint64_t capabilities;
    char     name[32];
    uint32_t state;
    uint32_t priority;
    uint64_t total_runtime_ns;
} cervus_task_info_t;

typedef struct {
    int64_t  tv_sec;
    int64_t  tv_nsec;
} cervus_timespec_t;

typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t used_bytes;
    uint64_t usable_bytes;
    uint64_t page_size;
} cervus_meminfo_t;

typedef struct __attribute__((packed)) {
    uint8_t  boot_flag;
    uint8_t  type;
    uint32_t lba_start;
    uint32_t sector_count;
} cervus_mbr_part_t;

typedef struct __attribute__((packed)) {
    char     disk_name[32];
    char     part_name[32];
    uint32_t part_num;
    uint8_t  type;
    uint8_t  bootable;
    uint64_t lba_start;
    uint64_t sector_count;
    uint64_t size_bytes;
} cervus_part_info_t;

#endif
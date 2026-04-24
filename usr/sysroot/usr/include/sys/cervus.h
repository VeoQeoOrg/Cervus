#ifndef _SYS_CERVUS_H
#define _SYS_CERVUS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define CAP_IOPORT     (1ULL << 0)
#define CAP_RAWMEM     (1ULL << 1)
#define CAP_KILL_ANY   (1ULL << 4)
#define CAP_SET_PRIO   (1ULL << 5)
#define CAP_TASK_SPAWN (1ULL << 6)
#define CAP_TASK_INFO  (1ULL << 7)
#define CAP_MMAP_EXEC  (1ULL << 8)
#define CAP_SETUID     (1ULL << 17)
#define CAP_DBG_SERIAL (1ULL << 20)

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

typedef struct {
    char     name[32];
    uint64_t sectors;
    uint64_t size_bytes;
    char     model[41];
    uint8_t  present;
    uint8_t  _pad[6];
} cervus_disk_info_t;

typedef struct {
    uint32_t pid, ppid, uid, gid;
    uint64_t capabilities;
    char     name[32];
    uint32_t state;
    uint32_t priority;
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
    char     path[512];
    char     device[32];
    char     fstype[16];
    uint32_t flags;
} cervus_mount_info_t;

typedef struct {
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint32_t f_flag;
    uint32_t f_namemax;
} cervus_statvfs_t;

#define CLOCK_REALTIME   0
#define CLOCK_MONOTONIC  1

#define WNOHANG        0x1
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFEXITED(s)   (((s) & 0x7F) == 0)

int      cervus_task_info(pid_t pid, cervus_task_info_t *out);
int      cervus_task_kill(pid_t pid);
uint64_t cervus_cap_get(void);
int      cervus_cap_drop(uint64_t mask);

int      cervus_meminfo(cervus_meminfo_t *out);
uint64_t cervus_uptime_ns(void);
int      cervus_clock_gettime(int id, cervus_timespec_t *ts);
int      cervus_nanosleep(uint64_t ns);

int      cervus_shutdown(void);
int      cervus_reboot(void);

int      cervus_disk_info(int index, cervus_disk_info_t *out);
int      cervus_disk_mount(const char *dev, const char *path);
int      cervus_disk_umount(const char *path);
int      cervus_disk_format(const char *dev, const char *label);
int      cervus_disk_mkfs_fat32(const char *dev, const char *label);
int      cervus_disk_partition(const char *dev, const cervus_mbr_part_t *specs, uint64_t n);
int      cervus_disk_read_raw(const char *dev, uint64_t lba, uint64_t count, void *buf);
int      cervus_disk_write_raw(const char *dev, uint64_t lba, uint64_t count, const void *buf);
long     cervus_disk_list_parts(cervus_part_info_t *out, int max);
long     cervus_disk_bios_install(const char *disk, const void *sys_data, uint32_t sys_size);

long     cervus_list_mounts(cervus_mount_info_t *out, int max);
long     cervus_statvfs(const char *path, cervus_statvfs_t *out);

uint32_t cervus_ioport_read(uint16_t port, int width);
int      cervus_ioport_write(uint16_t port, int width, uint32_t val);

#define PROT_NONE     0x0
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4

#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10
#define MAP_FAILED    ((void *)-1)

void    *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int      munmap(void *addr, size_t len);

pid_t    waitpid(pid_t pid, int *status, int options);
pid_t    wait(int *status);

ssize_t  cervus_dbg_print(const char *buf, size_t n);

#endif

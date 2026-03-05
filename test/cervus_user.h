#ifndef CERVUS_USER_H
#define CERVUS_USER_H

#include <stdint.h>
#include <stddef.h>

typedef int32_t  pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t  ssize_t;
typedef int64_t  off_t;

#define SYS_EXIT          0
#define SYS_EXIT_GROUP    1
#define SYS_GETPID        2
#define SYS_GETPPID       3
#define SYS_FORK          4
#define SYS_WAIT          5
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

#define SYS_MMAP         40
#define SYS_MUNMAP       41
#define SYS_MPROTECT     42
#define SYS_BRK          43

#define SYS_CLOCK_GET    60
#define SYS_SLEEP_NS     61
#define SYS_UPTIME       62

#define SYS_DBG_PRINT    512
#define SYS_TASK_KILL    515
#define SYS_IOPORT_READ  521
#define SYS_IOPORT_WRITE 522

#define EPERM          1
#define ENOENT         2
#define ESRCH          3
#define EINTR          4
#define EIO            5
#define EBADF          9
#define ECHILD        10
#define EAGAIN        11
#define ENOMEM        12
#define EACCES        13
#define EFAULT        14
#define EBUSY         16
#define EEXIST        17
#define EINVAL        22
#define ENOSPC        28
#define ENOSYS        38

#define PROT_NONE    0x0
#define PROT_READ    0x1
#define PROT_WRITE   0x2
#define PROT_EXEC    0x4

#define MAP_PRIVATE    0x02
#define MAP_ANONYMOUS  0x20
#define MAP_FIXED      0x10
#define MAP_FAILED     ((void*)-1)

#define WNOHANG  0x1

#define CAP_IOPORT      (1ULL <<  0)
#define CAP_RAWMEM      (1ULL <<  1)
#define CAP_KILL_ANY    (1ULL <<  4)
#define CAP_SET_PRIO    (1ULL <<  5)
#define CAP_TASK_SPAWN  (1ULL <<  6)
#define CAP_TASK_INFO   (1ULL <<  7)
#define CAP_MMAP_EXEC   (1ULL <<  8)
#define CAP_SETUID      (1ULL << 17)
#define CAP_DBG_SERIAL  (1ULL << 20)

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
    int64_t tv_sec;
    int64_t tv_nsec;
} cervus_timespec_t;

static inline int64_t
__syscall(uint64_t nr,
          uint64_t a1, uint64_t a2, uint64_t a3,
          uint64_t a4, uint64_t a5, uint64_t a6)
{
    int64_t ret;
    register uint64_t _nr  asm("rax") = nr;
    register uint64_t _a1  asm("rdi") = a1;
    register uint64_t _a2  asm("rsi") = a2;
    register uint64_t _a3  asm("rdx") = a3;
    register uint64_t _a4  asm("r10") = a4;
    register uint64_t _a5  asm("r8")  = a5;
    register uint64_t _a6  asm("r9")  = a6;
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "r"(_nr), "r"(_a1), "r"(_a2), "r"(_a3),
          "r"(_a4), "r"(_a5), "r"(_a6)
        : "rcx", "r11", "memory"
    );
    return ret;
}

#define syscall0(nr)             __syscall(nr,0,0,0,0,0,0)
#define syscall1(nr,a)           __syscall(nr,(uint64_t)(a),0,0,0,0,0)
#define syscall2(nr,a,b)         __syscall(nr,(uint64_t)(a),(uint64_t)(b),0,0,0,0)
#define syscall3(nr,a,b,c)       __syscall(nr,(uint64_t)(a),(uint64_t)(b),(uint64_t)(c),0,0,0)
#define syscall4(nr,a,b,c,d)     __syscall(nr,(uint64_t)(a),(uint64_t)(b),(uint64_t)(c),(uint64_t)(d),0,0)
#define syscall6(nr,a,b,c,d,e,f) __syscall(nr,(uint64_t)(a),(uint64_t)(b),(uint64_t)(c),(uint64_t)(d),(uint64_t)(e),(uint64_t)(f))

static inline void __attribute__((noreturn)) exit(int code) {
    syscall1(SYS_EXIT, code);
    __builtin_unreachable();
}

static inline pid_t getpid(void)  { return (pid_t)syscall0(SYS_GETPID);  }
static inline pid_t getppid(void) { return (pid_t)syscall0(SYS_GETPPID); }
static inline uid_t getuid(void)  { return (uid_t)syscall0(SYS_GETUID);  }
static inline gid_t getgid(void)  { return (gid_t)syscall0(SYS_GETGID);  }

static inline int setuid(uid_t uid) { return (int)syscall1(SYS_SETUID, uid); }
static inline int setgid(gid_t gid) { return (int)syscall1(SYS_SETGID, gid); }

static inline pid_t fork(void)  { return (pid_t)syscall0(SYS_FORK);  }
static inline void  yield(void) { syscall0(SYS_YIELD); }

static inline pid_t wait(int* status) {
    return (pid_t)syscall3(SYS_WAIT, (uint64_t)-1, status, 0);
}

static inline pid_t waitpid(pid_t pid, int* status, int flags) {
    return (pid_t)syscall3(SYS_WAIT, pid, status, flags);
}

static inline uint64_t cap_get(void)         { return (uint64_t)syscall0(SYS_CAP_GET); }
static inline int      cap_drop(uint64_t m)  { return (int)syscall1(SYS_CAP_DROP, m);  }

static inline int task_info(pid_t pid, cervus_task_info_t* buf) {
    return (int)syscall2(SYS_TASK_INFO, pid, buf);
}

static inline ssize_t write(int fd, const void* buf, size_t count) {
    return (ssize_t)syscall3(SYS_WRITE, fd, buf, count);
}

static inline ssize_t read(int fd, void* buf, size_t count) {
    return (ssize_t)syscall3(SYS_READ, fd, buf, count);
}

static inline void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t offset) {
    return (void*)syscall6(SYS_MMAP, addr, len, prot, flags, fd, offset);
}

static inline int munmap(void* addr, size_t len) {
    return (int)syscall2(SYS_MUNMAP, addr, len);
}

static inline void* sbrk(intptr_t delta) {
    uintptr_t cur = (uintptr_t)syscall1(SYS_BRK, 0);
    if (delta == 0) return (void*)cur;
    uintptr_t new = (uintptr_t)syscall1(SYS_BRK, cur + delta);
    if (new == cur) return (void*)-1;
    return (void*)cur;
}

static inline int clock_gettime(int clk_id, cervus_timespec_t* ts) {
    return (int)syscall2(SYS_CLOCK_GET, clk_id, ts);
}

static inline int nanosleep_simple(uint64_t ns) {
    return (int)syscall1(SYS_SLEEP_NS, ns);
}

static inline uint64_t uptime_ns(void) {
    return (uint64_t)syscall0(SYS_UPTIME);
}

static inline ssize_t dbg_print(const char* str, size_t len) {
    return (ssize_t)syscall2(SYS_DBG_PRINT, str, len);
}

static inline int task_kill(pid_t pid) {
    return (int)syscall1(SYS_TASK_KILL, pid);
}

static inline uint32_t ioport_read(uint16_t port, int width) {
    return (uint32_t)syscall2(SYS_IOPORT_READ, port, width);
}

static inline int ioport_write(uint16_t port, int width, uint32_t val) {
    return (int)syscall3(SYS_IOPORT_WRITE, port, width, val);
}

static inline size_t strlen(const char* s) {
    size_t n = 0; while (s[n]) n++; return n;
}

static inline void puts(const char* s) {
    write(1, s, strlen(s));
    write(1, "\n", 1);
}

static inline void print(const char* s) {
    write(1, s, strlen(s));
}

#endif
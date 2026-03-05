#ifndef CAPABILITIES_H
#define CAPABILITIES_H

#include <stdint.h>
#include <stdbool.h>

#define UID_ROOT        0
#define GID_ROOT        0
#define UID_NOBODY   65534
#define GID_NOBODY   65534

#define CAP_IOPORT      (1ULL <<  0)
#define CAP_RAWMEM      (1ULL <<  1)
#define CAP_DMA         (1ULL <<  2)
#define CAP_IRQ         (1ULL <<  3)
#define CAP_KILL_ANY    (1ULL <<  4)
#define CAP_SET_PRIO    (1ULL <<  5)
#define CAP_TASK_SPAWN  (1ULL <<  6)
#define CAP_TASK_INFO   (1ULL <<  7)
#define CAP_MMAP_EXEC   (1ULL <<  8)
#define CAP_MMAP_PHYS   (1ULL <<  9)
#define CAP_FS_ROOT     (1ULL << 10)
#define CAP_FS_OWNER    (1ULL << 11)
#define CAP_NET_RAW     (1ULL << 12)
#define CAP_NET_BIND    (1ULL << 13)
#define CAP_SYSADMIN    (1ULL << 14)
#define CAP_REBOOT      (1ULL << 15)
#define CAP_MODULE      (1ULL << 16)
#define CAP_SETUID      (1ULL << 17)
#define CAP_AUDIT       (1ULL << 18)
#define CAP_PTRACE      (1ULL << 19)
#define CAP_DBG_SERIAL  (1ULL << 20)
#define CAP_ALL         (~0ULL)
#define CAP_BASIC_SET   (CAP_MMAP_EXEC | CAP_TASK_SPAWN)
#define CAP_SERVICE_SET (CAP_BASIC_SET | CAP_TASK_INFO | CAP_SET_PRIO | CAP_NET_BIND)

static inline bool cap_has(uint64_t caps, uint64_t cap) {
    return (caps & cap) == cap;
}

static inline uint64_t cap_drop(uint64_t caps, uint64_t cap) {
    return caps & ~cap;
}

static inline uint64_t cap_initial(uint32_t uid) {
    return (uid == UID_ROOT) ? CAP_ALL : CAP_BASIC_SET;
}

#endif
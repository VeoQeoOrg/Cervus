#ifndef _SYS_SIGNALFD_H
#define _SYS_SIGNALFD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <signal.h>

#define SFD_NONBLOCK 0x800
#define SFD_CLOEXEC  0x80000

struct signalfd_siginfo {
    uint32_t ssi_signo;
    int32_t  ssi_errno;
    int32_t  ssi_code;
    uint32_t ssi_pid;
    uint32_t ssi_uid;
    int32_t  ssi_fd;
    uint32_t ssi_tid;
    uint32_t ssi_band;
    uint32_t ssi_overrun;
    uint32_t ssi_trapno;
    int32_t  ssi_status;
    int32_t  ssi_int;
    uint64_t ssi_ptr;
    uint64_t ssi_utime;
    uint64_t ssi_stime;
    uint64_t ssi_addr;
    uint16_t ssi_addr_lsb;
    uint8_t  __pad[46];
};

int signalfd(int fd, const sigset_t *mask, int flags);

#ifdef __cplusplus
}
#endif
#endif

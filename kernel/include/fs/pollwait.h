#ifndef _KERNEL_FS_POLLWAIT_H
#define _KERNEL_FS_POLLWAIT_H

#include <stdint.h>

uint64_t poll_seq(void);
void     poll_notify(void);
void     poll_wait_since(uint64_t seen, uint64_t deadline_ns);

#endif

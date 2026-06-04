#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/timer.h"

int64_t sys_uptime(void) { return (int64_t)sched_now_ns(); }

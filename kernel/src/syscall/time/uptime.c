#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/apic/apic.h"

int64_t sys_uptime(void) { return (int64_t)hpet_elapsed_ns(); }

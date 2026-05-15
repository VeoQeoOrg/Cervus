#include "../../../include/syscall/syscall_internal.h"

int64_t sys_yield(void)
{
    task_yield();
    return 0;
}

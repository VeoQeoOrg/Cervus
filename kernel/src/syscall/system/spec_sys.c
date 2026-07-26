#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/spec.h"

int64_t sys_spec(uint64_t op, uint64_t a, uint64_t b)
{
    (void)a;
    (void)b;
    switch (op) {
        case 0:
            return (int64_t)spec_selftest2();
        default:
            return -EINVAL;
    }
}

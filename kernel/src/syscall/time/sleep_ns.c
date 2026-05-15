#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/apic/apic.h"
#include "../../../include/io/serial.h"

int64_t sys_sleep_ns(uint64_t ns)
{
    if (ns == 0) return 0;
    if (!hpet_is_available()) {
        task_yield();
        return 0;
    }
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;

    syscall_save_user_regs(me);

    serial_printf("[SLEEP] pid=%u sleeping %llu ns\n", me->pid, ns);

    uint64_t now = hpet_elapsed_ns();
    me->wakeup_time_ns = now + ns;
    me->runnable = false;
    me->state    = TASK_BLOCKED;

    sched_reschedule();

    serial_printf("[SLEEP] pid=%u woke up\n", me ? me->pid : 0);
    return 0;
}

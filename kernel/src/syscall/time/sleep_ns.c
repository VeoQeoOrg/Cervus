#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/io/serial.h"

int64_t sys_sleep_ns(uint64_t ns)
{
    if (ns == 0) return 0;
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;

    uint64_t now = sched_now_ns();
    me->wakeup_time_ns = now + ns;
    sched_note_wakeup(me->wakeup_time_ns);
    me->runnable = false;
    me->state    = TASK_BLOCKED;

    sched_reschedule();

    return 0;
}

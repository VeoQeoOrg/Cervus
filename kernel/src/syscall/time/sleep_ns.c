#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/io/serial.h"
#include "../../../include/signal/signal.h"

int64_t sys_sleep_ns(uint64_t ns)
{
    if (ns == 0) return 0;
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;

    uint64_t deadline = sched_now_ns() + ns;
    for (;;) {
        me->wakeup_time_ns = deadline;
        sched_note_wakeup(deadline);
        me->runnable = false;
        me->state    = TASK_BLOCKED;

        sched_reschedule();

        if (sched_now_ns() >= deadline) return 0;
        if (me->pending_kill || signal_pending_deliverable(me)) {
            me->wakeup_time_ns = 0;
            return -EINTR;
        }
    }
}

#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/timer.h"
#include <string.h>

#define ITIMER_REAL_K 0

typedef struct {
    int64_t it_interval_sec;
    int64_t it_interval_usec;
    int64_t it_value_sec;
    int64_t it_value_usec;
} user_itimerval_t;

static uint64_t to_ns(int64_t sec, int64_t usec)
{
    if (sec < 0 || usec < 0) return 0;
    return (uint64_t)sec * 1000000000ULL + (uint64_t)usec * 1000ULL;
}

static void from_ns(uint64_t ns, int64_t *sec, int64_t *usec)
{
    *sec  = (int64_t)(ns / 1000000000ULL);
    *usec = (int64_t)((ns % 1000000000ULL) / 1000ULL);
}

int64_t sys_setitimer(uint64_t which, uint64_t unew, uint64_t uold)
{
    if (which != ITIMER_REAL_K) return -EINVAL;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;

    user_itimerval_t nv;
    memset(&nv, 0, sizeof nv);
    if (unew && syscall_copy_from_user(&nv, (const void *)unew, sizeof nv) < 0)
        return -EFAULT;

    uint64_t old_value = 0, old_interval = 0;
    task_set_itimer(t, to_ns(nv.it_value_sec, nv.it_value_usec),
                    to_ns(nv.it_interval_sec, nv.it_interval_usec),
                    &old_value, &old_interval);

    if (uold) {
        user_itimerval_t ov;
        from_ns(old_interval, &ov.it_interval_sec, &ov.it_interval_usec);
        from_ns(old_value,    &ov.it_value_sec,    &ov.it_value_usec);
        if (syscall_copy_to_user((void *)uold, &ov, sizeof ov) < 0) return -EFAULT;
    }
    return 0;
}

int64_t sys_getitimer(uint64_t which, uint64_t uold)
{
    if (which != ITIMER_REAL_K) return -EINVAL;
    if (!uold) return -EINVAL;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;

    uint64_t now = sched_now_ns();
    uint64_t remaining = (t->alarm_at_ns && t->alarm_at_ns > now) ? t->alarm_at_ns - now : 0;

    user_itimerval_t ov;
    from_ns(t->alarm_interval_ns, &ov.it_interval_sec, &ov.it_interval_usec);
    from_ns(remaining,            &ov.it_value_sec,    &ov.it_value_usec);
    if (syscall_copy_to_user((void *)uold, &ov, sizeof ov) < 0) return -EFAULT;
    return 0;
}

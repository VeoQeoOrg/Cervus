#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/console/console.h"

int64_t sys_vt_spawn_poll(void) {
    return vt_take_spawn_request();
}

int64_t sys_vt_set_ctty(uint64_t n) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if ((int)n < 0 || (int)n >= VT_COUNT) return -EINVAL;
    t->ctty = (int)n;
    return 0;
}

int64_t sys_vt_clear_shell(uint64_t n) {
    vt_mark_shell_running((int)n, 0);
    return 0;
}

int64_t sys_vt_switch(uint64_t n) {
    vt_switch((int)n);
    return 0;
}

#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"
#include "../../../include/io/serial.h"

int64_t sys_dbg_print(uint64_t str, uint64_t len)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != UID_ROOT && !cap_has(t->capabilities, CAP_DBG_SERIAL)) return -EPERM;
    if (!len) return 0;
    if (len > 512) len = 512;
    char kbuf[513];
    if (syscall_copy_from_user(kbuf, (const void *)str, len) < 0) return -EFAULT;
    kbuf[len] = '\0';
    serial_printf("[DBG pid=%u] %s", t->pid, kbuf);
    return (int64_t)len;
}

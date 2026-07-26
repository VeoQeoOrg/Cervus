#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/security/auth.h"
#include "../../../include/sched/capabilities.h"
#include "../../../include/io/serial.h"
#include <string.h>

extern void task_sleep_ms(uint64_t ms);

#define AUTH_MAX_PW 256

static void auth_fail_delay(void) {
    task_sleep_ms(1000);
}

int64_t sys_auth(uint64_t uid, uint64_t pass_ptr)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (uid > 65535) return -EINVAL;

    char pw[AUTH_MAX_PW];
    if (syscall_strncpy_from_user(pw, (const char *)pass_ptr, sizeof(pw)) < 0)
        return -EFAULT;

    int ok = auth_verify((uint32_t)uid, pw);
    memset(pw, 0, sizeof(pw));

    serial_printf("[AUTH] pid=%u uid=%u -> auth to uid=%u: %s\n",
                  t->pid, t->uid, (uint32_t)uid, ok ? "OK" : "DENIED");
    if (!ok) { auth_fail_delay(); return -EACCES; }

    t->uid          = (uint32_t)uid;
    t->capabilities = cap_initial((uint32_t)uid);
    return 0;
}

int64_t sys_sudo(uint64_t pass_ptr, uint64_t a, uint64_t b)
{
    (void)a; (void)b;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;

    if (t->uid == UID_ROOT) { t->capabilities = CAP_ALL; return 0; }

    if (!auth_is_sudoer(t->uid)) {
        serial_printf("[AUTH] sudo DENIED (not a sudoer) pid=%u uid=%u\n", t->pid, t->uid);
        auth_fail_delay();
        return -EPERM;
    }

    char pw[AUTH_MAX_PW];
    if (syscall_strncpy_from_user(pw, (const char *)pass_ptr, sizeof(pw)) < 0)
        return -EFAULT;

    int ok = auth_verify(t->uid, pw);
    memset(pw, 0, sizeof(pw));

    serial_printf("[AUTH] sudo pid=%u uid=%u: %s\n", t->pid, t->uid, ok ? "GRANTED root" : "DENIED (bad pw)");
    if (!ok) { auth_fail_delay(); return -EACCES; }

    t->uid          = UID_ROOT;
    t->capabilities = CAP_ALL;
    return 0;
}

int64_t sys_passwd_set(uint64_t uid, uint64_t pass_ptr)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (uid > 65535) return -EINVAL;

    if (t->uid != UID_ROOT && t->uid != (uint32_t)uid) return -EPERM;

    char pw[AUTH_MAX_PW];
    if (syscall_strncpy_from_user(pw, (const char *)pass_ptr, sizeof(pw)) < 0)
        return -EFAULT;

    int r = auth_set_password((uint32_t)uid, pw);
    memset(pw, 0, sizeof(pw));

    serial_printf("[AUTH] passwd set for uid=%u by pid=%u uid=%u: %s\n",
                  (uint32_t)uid, t->pid, t->uid, r == 0 ? "OK" : "FAIL");
    return (r == 0) ? 0 : -EIO;
}

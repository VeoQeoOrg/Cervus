#include "../../../include/sched/spinlock.h"
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

#define SEAT_SESSIONS 16

static uint32_t g_seat_uid;
static uint32_t g_seat_sess_uid[SEAT_SESSIONS];
static uint32_t g_seat_sess_pid[SEAT_SESSIONS];
static int      g_seat_sess_vt[SEAT_SESSIONS];
static int      g_seat_active_vt;
static int      g_seat_sessions;
static spinlock_t g_seat_lock = SPINLOCK_INIT;

uint32_t seat_owner_uid(void) { return g_seat_uid; }
int      seat_active_vt(void) { return g_seat_active_vt; }

int seat_may_use(void)
{
    task_t *t = syscall_cur_task();
    return !t || t->uid == UID_ROOT || t->uid == g_seat_uid;
}

static void seat_update(void)
{
    g_seat_uid = UID_ROOT;
    for (int i = g_seat_sessions - 1; i >= 0; i--) {
        if (g_seat_sess_vt[i] != g_seat_active_vt) continue;
        g_seat_uid = g_seat_sess_uid[i];
        break;
    }
}

static void seat_drop(int i)
{
    for (int k = i; k + 1 < g_seat_sessions; k++) {
        g_seat_sess_uid[k] = g_seat_sess_uid[k + 1];
        g_seat_sess_pid[k] = g_seat_sess_pid[k + 1];
        g_seat_sess_vt[k]  = g_seat_sess_vt[k + 1];
    }
    g_seat_sessions--;
}

void seat_vt_switched(int vt)
{
    uint64_t f = spinlock_acquire_irqsave(&g_seat_lock);
    g_seat_active_vt = vt;
    seat_update();
    spinlock_release_irqrestore(&g_seat_lock, f);
}

static void seat_claim(uint32_t uid, uint32_t pid, int vt)
{
    uint64_t f = spinlock_acquire_irqsave(&g_seat_lock);
    for (int i = 0; i < g_seat_sessions; i++)
        if (g_seat_sess_pid[i] == pid) { seat_drop(i); break; }
    if (g_seat_sessions == SEAT_SESSIONS) seat_drop(0);
    g_seat_sess_uid[g_seat_sessions] = uid;
    g_seat_sess_pid[g_seat_sessions] = pid;
    g_seat_sess_vt[g_seat_sessions]  = vt;
    g_seat_sessions++;
    seat_update();
    spinlock_release_irqrestore(&g_seat_lock, f);
}

void seat_task_exit(uint32_t pid)
{
    uint64_t f = spinlock_acquire_irqsave(&g_seat_lock);
    for (int i = 0; i < g_seat_sessions; i++) {
        if (g_seat_sess_pid[i] != pid) continue;
        seat_drop(i);
        seat_update();
        break;
    }
    spinlock_release_irqrestore(&g_seat_lock, f);
}

int64_t sys_auth(uint64_t uid, uint64_t pass_ptr, uint64_t opts)
{
    if (pass_ptr == 0) {
        if (uid > 65535) return -EINVAL;
        return auth_has_any_password((uint32_t)uid) ? 1 : 0;
    }

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

    int was_root = (t->uid == UID_ROOT);
    t->uid          = (uint32_t)uid;
    t->capabilities = cap_initial((uint32_t)uid);
    if (opts & AUTH_SET_GID) t->gid = (uint32_t)(opts & 0xFFFFFFFFu);
    if ((opts & AUTH_CLAIM_SEAT) && was_root) seat_claim((uint32_t)uid, t->pid, t->ctty);
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

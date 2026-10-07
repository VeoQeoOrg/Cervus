#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/signal/signal.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/io/serial.h"
#include <string.h>

#define WNOHANG_K    0x00000001
#define WUNTRACED_K  0x00000002
#define WEXITED_K    0x00000004
#define WCONTINUED_K 0x00000008
#define WNOWAIT_K    0x01000000

#define P_ALL_K  0
#define P_PID_K  1
#define P_PGID_K 2

#define RUSAGE_SELF_K     0
#define RUSAGE_CHILDREN_K (-1)
#define RUSAGE_THREAD_K   1

typedef struct {
    uint32_t pid;
    uint32_t uid;
    int      status;
    int      code;
    int      si_status;
    uint64_t runtime_ns;
    uint64_t maxrss_bytes;
} wait_result_t;

typedef struct {
    int64_t utime_sec, utime_usec;
    int64_t stime_sec, stime_usec;
    int64_t maxrss;
    int64_t rest[13];
} k_rusage_t;

typedef struct {
    int32_t  si_signo;
    int32_t  si_errno;
    int32_t  si_code;
    int32_t  pad0;
    uint32_t si_pid;
    uint32_t si_uid;
    int32_t  si_status;
    int32_t  pad1;
    uint8_t  rest[96];
} k_waitinfo_t;

_Static_assert(sizeof(k_rusage_t) == 144, "struct rusage is 144 bytes");
_Static_assert(sizeof(k_waitinfo_t) == 128, "siginfo_t is 128 bytes");

void task_sample_rss(task_t *t)
{
    if (!t || !t->is_userspace || !t->pagemap) return;
    uint64_t rss = vmm_count_user_pages(t->pagemap) * 4096ULL;
    if (rss > t->maxrss_bytes) t->maxrss_bytes = rss;
}

static bool child_selected(task_t *parent, task_t *c, int idtype, uint32_t id)
{
    switch (idtype) {
    case P_PID_K:  return c->pid == id;
    case P_PGID_K: return c->pgid == (id ? id : parent->pgid);
    default:       return true;
    }
}

static void fill_result(task_t *z, wait_result_t *res)
{
    res->pid = z->pid;
    res->uid = z->uid;
    if (z->term_sig) {
        res->status    = z->term_sig & 0x7F;
        res->code      = CLD_KILLED_CODE;
        res->si_status = z->term_sig;
    } else {
        res->status    = (z->exit_code & 0xFF) << 8;
        res->code      = CLD_EXITED_CODE;
        res->si_status = z->exit_code & 0xFF;
    }
    res->runtime_ns   = z->total_runtime + z->child_runtime_ns;
    res->maxrss_bytes = z->maxrss_bytes;
}

static void reap(task_t *parent, task_t *zombie)
{
    uint32_t zpid = zombie->pid;
    parent->child_runtime_ns += zombie->total_runtime + zombie->child_runtime_ns;
    uint64_t rss = zombie->maxrss_bytes > zombie->child_maxrss_bytes ? zombie->maxrss_bytes
                                                                     : zombie->child_maxrss_bytes;
    if (rss > parent->child_maxrss_bytes) parent->child_maxrss_bytes = rss;

    uint64_t f = spinlock_acquire_irqsave(&children_lock);
    if (parent->children == zombie) {
        parent->children = zombie->sibling;
    } else {
        task_t *prev = parent->children;
        while (prev && prev->sibling != zombie) prev = prev->sibling;
        if (prev) prev->sibling = zombie->sibling;
    }
    zombie->sibling = NULL;
    zombie->parent  = NULL;
    spinlock_release_irqrestore(&children_lock, f);

    extern void task_reassign_foreground(uint32_t dead_pid, task_t *parent);
    task_reassign_foreground(zpid, parent);
    task_destroy(zombie);
}

static int64_t do_wait(int idtype, uint32_t id, int options, wait_result_t *res)
{
    task_t *self = syscall_cur_task();
    if (!self) return -ESRCH;
    task_t *parent = task_leader(self);
    if (idtype == P_PGID_K && id == 0) id = parent->pgid;

    for (;;) {
        task_t *zombie = NULL;
        bool any = false, busy = false;
        uint64_t f = spinlock_acquire_irqsave(&children_lock);
        for (task_t *c = parent->children; c; c = c->sibling) {
            if (!child_selected(parent, c, idtype, id)) continue;
            any = true;
            if (c->state != TASK_ZOMBIE) continue;
            if (__atomic_load_n(&c->group_live, __ATOMIC_ACQUIRE) > 0) continue;
            if (__atomic_load_n(&c->on_cpu._val, __ATOMIC_ACQUIRE)) { busy = true; continue; }
            zombie = c;
            break;
        }
        spinlock_release_irqrestore(&children_lock, f);

        if (!any) return -ECHILD;
        if (zombie) {
            fill_result(zombie, res);
            uint32_t pid = zombie->pid;
            if (!(options & WNOWAIT_K)) reap(parent, zombie);
            return (int64_t)pid;
        }
        if (busy) { task_yield(); continue; }
        if (options & WNOHANG_K) return 0;

        self->wait_for_pid = idtype == P_PID_K ? id : (uint32_t)-1;
        self->runnable = false;
        self->state    = TASK_BLOCKED;
        if (idtype == P_PID_K) task_set_foreground(id);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (self->pending_kill || signal_pending_deliverable(self)) task_unblock(self);
        sched_reschedule();
        if (self->pending_kill || signal_pending_deliverable(self)) return -EINTR;
    }
}

static void select_from_pid(task_t *me, int64_t pid, int *idtype, uint32_t *id)
{
    if (pid == -1)      { *idtype = P_ALL_K;  *id = 0; }
    else if (pid > 0)   { *idtype = P_PID_K;  *id = (uint32_t)pid; }
    else if (pid == 0)  { *idtype = P_PGID_K; *id = me ? me->pgid : 0; }
    else                { *idtype = P_PGID_K; *id = (uint32_t)(-pid); }
}

static void ns_to_tv(uint64_t ns, int64_t *sec, int64_t *usec)
{
    *sec  = (int64_t)(ns / 1000000000ULL);
    *usec = (int64_t)((ns % 1000000000ULL) / 1000ULL);
}

int64_t sys_wait4(uint64_t pid_arg, uint64_t status_ptr, uint64_t options, uint64_t rusage_ptr)
{
    if (options & ~(uint64_t)(WNOHANG_K | WUNTRACED_K | WCONTINUED_K)) return -EINVAL;
    int idtype;
    uint32_t id;
    select_from_pid(syscall_cur_task(), (int64_t)pid_arg, &idtype, &id);
    wait_result_t res;
    memset(&res, 0, sizeof(res));
    int64_t r = do_wait(idtype, id, (int)options, &res);
    if (r <= 0) {
        if (r == 0 && rusage_ptr) {
            k_rusage_t ru;
            memset(&ru, 0, sizeof(ru));
            if (syscall_copy_to_user((void *)rusage_ptr, &ru, sizeof(ru)) < 0) return -EFAULT;
        }
        return r;
    }
    if (status_ptr && syscall_copy_to_user((void *)status_ptr, &res.status, sizeof(int)) < 0) return -EFAULT;
    if (rusage_ptr) {
        k_rusage_t ru;
        memset(&ru, 0, sizeof(ru));
        ns_to_tv(res.runtime_ns, &ru.utime_sec, &ru.utime_usec);
        ru.maxrss = (int64_t)(res.maxrss_bytes / 1024);
        if (syscall_copy_to_user((void *)rusage_ptr, &ru, sizeof(ru)) < 0) return -EFAULT;
    }
    return r;
}

int64_t sys_wait(uint64_t pid_arg, uint64_t status_ptr, uint64_t flags)
{
    return sys_wait4(pid_arg, status_ptr, flags, 0);
}

int64_t sys_waitid(uint64_t idtype_a, uint64_t id_a, uint64_t info_ptr, uint64_t options)
{
    int idtype = (int)idtype_a;
    if (idtype != P_ALL_K && idtype != P_PID_K && idtype != P_PGID_K) return -EINVAL;
    if (options & ~(uint64_t)(WNOHANG_K | WUNTRACED_K | WEXITED_K | WCONTINUED_K | WNOWAIT_K)) return -EINVAL;
    if (!(options & (WEXITED_K | WUNTRACED_K | WCONTINUED_K))) return -EINVAL;

    k_waitinfo_t info;
    memset(&info, 0, sizeof(info));
    wait_result_t res;
    memset(&res, 0, sizeof(res));
    if (!(options & WEXITED_K)) {
        int64_t r = do_wait(idtype, (uint32_t)id_a, WNOHANG_K | WNOWAIT_K, &res);
        if (r < 0) return r;
        if (!(options & WNOHANG_K)) {
            task_t *t = syscall_cur_task();
            while (!t->pending_kill && !signal_pending_deliverable(t)) {
                t->runnable = false;
                t->state = TASK_BLOCKED;
                __atomic_thread_fence(__ATOMIC_SEQ_CST);
                if (t->pending_kill || signal_pending_deliverable(t)) task_unblock(t);
                sched_reschedule();
            }
            return -EINTR;
        }
        if (info_ptr && syscall_copy_to_user((void *)info_ptr, &info, sizeof(info)) < 0) return -EFAULT;
        return 0;
    }

    int64_t r = do_wait(idtype, (uint32_t)id_a, (int)options, &res);
    if (r < 0) return r;
    if (r > 0) {
        info.si_signo  = SIGCHLD;
        info.si_code   = res.code;
        info.si_pid    = res.pid;
        info.si_uid    = res.uid;
        info.si_status = res.si_status;
    }
    if (info_ptr && syscall_copy_to_user((void *)info_ptr, &info, sizeof(info)) < 0) return -EFAULT;
    return 0;
}

int64_t sys_getrusage(uint64_t who_a, uint64_t ptr)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    int who = (int)(int64_t)who_a;
    k_rusage_t ru;
    memset(&ru, 0, sizeof(ru));
    if (who == RUSAGE_SELF_K || who == RUSAGE_THREAD_K) {
        task_sample_rss(t);
        ns_to_tv(t->total_runtime, &ru.utime_sec, &ru.utime_usec);
        ru.maxrss = (int64_t)(t->maxrss_bytes / 1024);
    } else if (who == RUSAGE_CHILDREN_K) {
        ns_to_tv(t->child_runtime_ns, &ru.utime_sec, &ru.utime_usec);
        ru.maxrss = (int64_t)(t->child_maxrss_bytes / 1024);
    } else {
        return -EINVAL;
    }
    return syscall_copy_to_user((void *)ptr, &ru, sizeof(ru));
}

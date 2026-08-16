#include "../../include/signal/signal.h"
#include "../../include/sched/sched.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/memory/vmm.h"
#include <string.h>

extern void task_unblock(task_t *t);
extern int  task_collect_pids(uint32_t *out, int max);

struct k_sigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
};

static int sig_default_ignore(int sig) {
    switch (sig) {
        case SIGCHLD: case SIGWINCH: case SIGURG: case SIGCONT: case SIGIO:
        case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
            return 1;
    }
    return 0;
}

static __attribute__((noreturn)) void signal_terminate(task_t *t, int sig) {
    t->exit_code = 128 + sig;
    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    task_exit();
}

void signal_send(task_t *t, int sig) {
    if (!t || sig <= 0 || sig >= NSIG) return;
    t->sig_pending |= (1ULL << sig);
    if (sig == SIGKILL) t->pending_kill = true;
    if (t->state == TASK_BLOCKED &&
        (sig == SIGKILL || !(t->sig_blocked & (1ULL << sig))))
        task_unblock(t);
}

bool signal_pending_deliverable(task_t *t) {
    if (!t) return false;
    return (t->sig_pending & ~t->sig_blocked) != 0;
}

void signal_send_group(uint32_t pgid, int sig) {
    if (pgid == 0) return;
    uint32_t pids[256];
    int n = task_collect_pids(pids, 256);
    for (int i = 0; i < n; i++) {
        task_t *t = task_find_by_pid(pids[i]);
        if (t && t->pgid == pgid) signal_send(t, sig);
    }
}

int64_t signal_deliver_pending(task_t *t, int64_t retval) {
    if (!t || !t->is_userspace) return retval;

    uint64_t deliverable = t->sig_pending & ~t->sig_blocked;
    if (!deliverable) return retval;

    int sig = __builtin_ctzll(deliverable);
    if (sig <= 0 || sig >= NSIG) { t->sig_pending &= ~deliverable; return retval; }

    uint64_t h = t->sig_handler[sig];

    if (sig == SIGKILL || h == SIG_DFL_ADDR) {
        t->sig_pending &= ~(1ULL << sig);
        if (sig != SIGKILL && sig_default_ignore(sig)) return retval;
        signal_terminate(t, sig);
    }
    if (h == SIG_IGN_ADDR) { t->sig_pending &= ~(1ULL << sig); return retval; }

    uint64_t restorer = t->sig_restorer[sig];
    if (!restorer) { t->sig_pending &= ~(1ULL << sig); signal_terminate(t, sig); }

    size_t need = 16 + sizeof(sig_ucontext_t);
    uint64_t sp = (t->user_rsp - need) & ~0xFULL;
    if (!syscall_uptr_validate((void *)sp, need)) {
        t->sig_pending &= ~(1ULL << sig);
        signal_terminate(t, SIGSEGV);
    }

    uint64_t *base = (uint64_t *)sp;
    base[0] = h;
    base[1] = (uint64_t)sig;
    sig_ucontext_t *uc = (sig_ucontext_t *)(base + 2);
    uc->rip    = t->user_saved_rip;
    uc->rsp    = t->user_rsp;
    uc->rflags = t->user_saved_r11;
    uc->rax    = (uint64_t)retval;
    uc->rbx    = t->user_saved_rbx;
    uc->rbp    = t->user_saved_rbp;
    uc->r12    = t->user_saved_r12;
    uc->r13    = t->user_saved_r13;
    uc->r14    = t->user_saved_r14;
    uc->r15    = t->user_saved_r15;
    uc->mask   = t->sig_blocked;

    t->sig_blocked |= t->sig_mask[sig];
    if (!(t->sig_flags[sig] & SA_NODEFER)) t->sig_blocked |= (1ULL << sig);
    if (t->sig_flags[sig] & SA_RESETHAND)  t->sig_handler[sig] = SIG_DFL_ADDR;
    t->sig_pending &= ~(1ULL << sig);

    t->user_rsp = sp;
    t->user_saved_rip = restorer;
    return retval;
}

int64_t sys_rt_sigaction(uint64_t sig, uint64_t act, uint64_t oldact, uint64_t sigsetsize) {
    (void)sigsetsize;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (sig == 0 || sig >= NSIG) return -EINVAL;
    if (sig == SIGKILL || sig == SIGSTOP) return -EINVAL;

    if (oldact) {
        if (!syscall_uptr_validate((void *)oldact, sizeof(struct k_sigaction))) return -EFAULT;
        struct k_sigaction *o = (struct k_sigaction *)oldact;
        o->handler  = t->sig_handler[sig];
        o->flags    = t->sig_flags[sig];
        o->restorer = t->sig_restorer[sig];
        o->mask     = t->sig_mask[sig];
    }
    if (act) {
        if (!syscall_uptr_validate((void *)act, sizeof(struct k_sigaction))) return -EFAULT;
        struct k_sigaction *a = (struct k_sigaction *)act;
        t->sig_handler[sig]  = a->handler;
        t->sig_flags[sig]    = a->flags;
        t->sig_restorer[sig] = a->restorer;
        t->sig_mask[sig]     = a->mask & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
    }
    return 0;
}

int64_t sys_rt_sigprocmask(uint64_t how, uint64_t set, uint64_t oldset, uint64_t sigsetsize) {
    (void)sigsetsize;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (oldset) {
        if (!syscall_uptr_validate((void *)oldset, 8)) return -EFAULT;
        *(uint64_t *)oldset = t->sig_blocked;
    }
    if (set) {
        if (!syscall_uptr_validate((void *)set, 8)) return -EFAULT;
        uint64_t s = (*(uint64_t *)set) & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
        if (how == 0)      t->sig_blocked |= s;
        else if (how == 1) t->sig_blocked &= ~s;
        else if (how == 2) t->sig_blocked = s;
        else return -EINVAL;
    }
    return 0;
}

int64_t sys_kill(uint64_t pid_a, uint64_t sig_a) {
    int pid = (int)(int64_t)pid_a;
    int sig = (int)sig_a;
    if (sig < 0 || sig >= NSIG) return -EINVAL;

    if (pid > 0) {
        task_t *t = task_find_by_pid((uint32_t)pid);
        if (!t) return -ESRCH;
        if (sig != 0) signal_send(t, sig);
        return 0;
    }

    task_t *me = syscall_cur_task();
    uint32_t pgid;
    if (pid == 0)       pgid = me ? me->pgid : 0;
    else if (pid < -1)  pgid = (uint32_t)(-pid);
    else                return -EPERM;

    uint32_t pids[256];
    int n = task_collect_pids(pids, 256);
    int found = 0;
    for (int i = 0; i < n; i++) {
        task_t *t = task_find_by_pid(pids[i]);
        if (t && t->pgid == pgid) {
            found = 1;
            if (sig != 0) signal_send(t, sig);
        }
    }
    return found ? 0 : -ESRCH;
}

int64_t sys_rt_sigreturn(void) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    uint64_t sp = t->user_rsp;
    if (!syscall_uptr_validate((void *)sp, sizeof(sig_ucontext_t)))
        signal_terminate(t, SIGSEGV);

    sig_ucontext_t *uc = (sig_ucontext_t *)sp;
    t->user_saved_rip = uc->rip;
    t->user_rsp       = uc->rsp;
    t->user_saved_r11 = uc->rflags;
    t->user_saved_rbx = uc->rbx;
    t->user_saved_rbp = uc->rbp;
    t->user_saved_r12 = uc->r12;
    t->user_saved_r13 = uc->r13;
    t->user_saved_r14 = uc->r14;
    t->user_saved_r15 = uc->r15;
    t->sig_blocked    = uc->mask;
    return (int64_t)uc->rax;
}

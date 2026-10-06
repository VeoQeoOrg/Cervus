#include "../../include/sched/capabilities.h"
#include "../../include/signal/signal.h"
#include "../../include/sched/sched.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/memory/vmm.h"
#include "../../include/interrupts/interrupts.h"
#include "../../include/io/serial.h"
#include "../../include/smp/smp.h"
#include "../../include/sched/spinlock.h"
#include "../../include/drivers/timer.h"
#include <string.h>

extern void task_unblock(task_t *t);
extern int  task_collect_pids(uint32_t *out, int max);

struct k_sigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
};

static int sig_is_core(int sig) {
    switch (sig) {
        case SIGQUIT: case SIGILL: case SIGTRAP: case SIGABRT: case SIGBUS:
        case SIGFPE: case SIGSEGV:
            return 1;
    }
    return 0;
}

static int sig_default_ignore(int sig) {
    switch (sig) {
        case SIGCHLD: case SIGWINCH: case SIGURG: case SIGCONT: case SIGIO:
        case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
            return 1;
    }
    return 0;
}

static __attribute__((noreturn)) void signal_terminate(task_t *t, int sig) {
    t->term_sig  = sig;
    t->exit_code = 128 + sig;
    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    task_exit();
}

extern int signalfd_deliver(uint32_t pid, int sig, const ksiginfo_t *si);

static int signal_ignored(task_t *t, int sig) {
    uint64_t h = t->sig_handler[sig];
    if (h == SIG_IGN_ADDR) return 1;
    return h == SIG_DFL_ADDR && sig_default_ignore(sig);
}

static spinlock_t g_sig_lock = SPINLOCK_INIT;

int signal_post(task_t *t, int sig, const ksiginfo_t *si) {
    if (!t || sig <= 0 || sig >= NSIG) return -EINVAL;
    if (sig != SIGKILL && signalfd_deliver(t->pid, sig, si)) return 0;
    uint64_t bit = 1ULL << sig;
    if (sig != SIGKILL && sig != SIGCONT && !(t->sig_blocked & bit) && !(t->sig_waitset & bit) &&
        signal_ignored(t, sig))
        return 0;

    uint64_t f = spinlock_acquire_irqsave(&g_sig_lock);
    if (sig >= SIGRTMIN_K) {
        if (t->rtq_n >= SIGQUEUE_MAX) {
            spinlock_release_irqrestore(&g_sig_lock, f);
            return -EAGAIN;
        }
        t->rtq[t->rtq_n].sig   = sig;
        t->rtq[t->rtq_n].code  = si->code;
        t->rtq[t->rtq_n].pid   = si->pid;
        t->rtq[t->rtq_n].uid   = si->uid;
        t->rtq[t->rtq_n].value = si->value;
        t->rtq_n++;
    } else if (!(t->sig_pending & bit)) {
        t->sig_si[sig].code   = si->code;
        t->sig_si[sig].pid    = si->pid;
        t->sig_si[sig].uid    = si->uid;
        t->sig_si[sig].status = si->status;
        t->sig_si[sig].addr   = si->addr;
        t->sig_si[sig].trapno = si->trapno;
        t->sig_si[sig].err    = si->err;
        t->sig_si[sig].value  = si->value;
    }
    t->sig_pending |= bit;
    if (sig == SIGKILL) {
        t->kill_sig = SIGKILL;
        t->pending_kill = true;
    }
    spinlock_release_irqrestore(&g_sig_lock, f);

    if (t->state == TASK_BLOCKED &&
        (sig == SIGKILL || !(t->sig_blocked & bit) || (t->sig_waitset & bit)))
        task_unblock(t);
    return 0;
}

void signal_send(task_t *t, int sig) {
    ksiginfo_t si = { .code = SI_KERNEL_CODE };
    signal_post(t, sig, &si);
}

void signal_send_child(task_t *parent, task_t *child) {
    ksiginfo_t si = { .pid = child->pid, .uid = child->uid };
    if (child->term_sig) {
        si.code   = sig_is_core(child->term_sig) ? CLD_DUMPED_CODE : CLD_KILLED_CODE;
        si.status = child->term_sig;
    } else {
        si.code   = CLD_EXITED_CODE;
        si.status = child->exit_code & 0xFF;
    }
    signal_post(parent, SIGCHLD, &si);
}

static void take_signal(task_t *t, int sig, ksiginfo_t *out) {
    uint64_t bit = 1ULL << sig;
    memset(out, 0, sizeof(*out));
    uint64_t f = spinlock_acquire_irqsave(&g_sig_lock);
    if (sig >= SIGRTMIN_K) {
        int idx = -1, more = 0;
        for (uint32_t i = 0; i < t->rtq_n; i++) {
            if (t->rtq[i].sig != sig) continue;
            if (idx < 0) idx = (int)i;
            else { more = 1; break; }
        }
        if (idx >= 0) {
            out->code  = t->rtq[idx].code;
            out->pid   = t->rtq[idx].pid;
            out->uid   = t->rtq[idx].uid;
            out->value = t->rtq[idx].value;
            for (uint32_t i = (uint32_t)idx; i + 1 < t->rtq_n; i++) t->rtq[i] = t->rtq[i + 1];
            t->rtq_n--;
        } else {
            out->code = SI_KERNEL_CODE;
        }
        if (!more) t->sig_pending &= ~bit;
    } else {
        out->code   = t->sig_si[sig].code;
        out->pid    = t->sig_si[sig].pid;
        out->uid    = t->sig_si[sig].uid;
        out->status = t->sig_si[sig].status;
        out->addr   = t->sig_si[sig].addr;
        out->trapno = t->sig_si[sig].trapno;
        out->err    = t->sig_si[sig].err;
        out->value  = t->sig_si[sig].value;
        t->sig_pending &= ~bit;
    }
    spinlock_release_irqrestore(&g_sig_lock, f);
}

static void discard_signal(task_t *t, int sig) {
    uint64_t f = spinlock_acquire_irqsave(&g_sig_lock);
    if (sig >= SIGRTMIN_K) {
        uint32_t w = 0;
        for (uint32_t i = 0; i < t->rtq_n; i++)
            if (t->rtq[i].sig != sig) t->rtq[w++] = t->rtq[i];
        t->rtq_n = w;
    }
    t->sig_pending &= ~(1ULL << sig);
    spinlock_release_irqrestore(&g_sig_lock, f);
}

static void fill_siginfo(uint8_t *info, int sig, const ksiginfo_t *si) {
    int32_t si_signo = sig, si_errno = 0, si_code = si->code;
    memset(info, 0, 128);
    memcpy(info + 0, &si_signo, 4);
    memcpy(info + 4, &si_errno, 4);
    memcpy(info + 8, &si_code, 4);
    bool fault = si->code > 0 && si->code != SI_KERNEL_CODE &&
                 (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE || sig == SIGTRAP);
    if (fault) {
        memcpy(info + 16, &si->addr, 8);
    } else if (sig == SIGCHLD && si->code > 0 && si->code != SI_KERNEL_CODE) {
        memcpy(info + 16, &si->pid, 4);
        memcpy(info + 20, &si->uid, 4);
        memcpy(info + 24, &si->status, 4);
    } else {
        memcpy(info + 16, &si->pid, 4);
        memcpy(info + 20, &si->uid, 4);
        memcpy(info + 24, &si->value, 8);
    }
}

void signal_kill_exit(task_t *t) {
    t->pending_kill = false;
    signal_terminate(t, t->kill_sig > 0 && t->kill_sig < NSIG ? t->kill_sig : SIGKILL);
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

static void signal_restore_saved_mask(task_t *t) {
    if (!t->sig_saved_valid) return;
    t->sig_blocked = t->sig_saved_mask;
    t->sig_saved_valid = 0;
}

#define SIGFRAME_MAGIC 0x313046535643ULL
#define SF_FULL        1ULL
#define SF_SIGINFO     2ULL
#define SS_ONSTACK_F   1
#define SS_DISABLE_F   2
#define USER_TOP       0x0000800000000000ULL

typedef struct {
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, rflags;
} sig_regs_t;

typedef struct {
    uint64_t magic;
    uint64_t flags;
    uint64_t uctx;
    uint64_t info;
} sig_ext_t;

typedef struct {
    uint64_t uc_flags;
    uint64_t uc_link;
    uint64_t ss_sp;
    uint32_t ss_flags;
    uint32_t ss_pad;
    uint64_t ss_size;
    uint64_t gregs[23];
    uint64_t fpregs;
    uint64_t reserved[8];
    uint64_t sigmask[2];
} k_ucontext_t;

#define FRAME_HEAD    16
#define FRAME_UC      (FRAME_HEAD)
#define FRAME_EXT     (FRAME_UC + sizeof(sig_ucontext_t))
#define FRAME_INFO    144
#define FRAME_UCTX    272
#define FRAME_FPU     592
#define FRAME_SIZE    1104

_Static_assert(FRAME_EXT + sizeof(sig_ext_t) <= FRAME_INFO, "signal frame: ext overlaps siginfo");
_Static_assert(FRAME_UCTX + sizeof(k_ucontext_t) <= FRAME_FPU, "signal frame: ucontext overlaps fpu");

void signal_send_from(task_t *t, int sig, task_t *from) {
    ksiginfo_t si = {
        .code = from ? SI_USER_CODE : SI_KERNEL_CODE,
        .pid  = from ? from->pid : 0,
        .uid  = from ? from->uid : 0,
    };
    signal_post(t, sig, &si);
}

void signal_force_fault(task_t *t, int sig, int code, uint64_t addr, int trapno, int err) {
    if (!t || sig <= 0 || sig >= SIGRTMIN_K) return;
    uint64_t bit = 1ULL << sig;
    if ((t->sig_blocked & bit) || t->sig_handler[sig] == SIG_IGN_ADDR) {
        t->sig_blocked &= ~bit;
        t->sig_handler[sig] = SIG_DFL_ADDR;
    }
    uint64_t f = spinlock_acquire_irqsave(&g_sig_lock);
    t->sig_si[sig].code   = code;
    t->sig_si[sig].pid    = 0;
    t->sig_si[sig].uid    = 0;
    t->sig_si[sig].status = 0;
    t->sig_si[sig].addr   = addr;
    t->sig_si[sig].trapno = trapno;
    t->sig_si[sig].err    = err;
    t->sig_si[sig].value  = 0;
    t->sig_pending |= bit;
    spinlock_release_irqrestore(&g_sig_lock, f);
}

static bool on_altstack(task_t *t, uint64_t sp) {
    return t->ss_size && sp > t->ss_sp && sp <= t->ss_sp + t->ss_size;
}

static __attribute__((noreturn)) void terminate_with_report(task_t *t, int sig, sig_regs_t *r) {
    if (sig_is_core(sig)) {
        serial_printf("[signal] %s pid=%u killed by signal %d at rip=0x%llx addr=0x%llx\n",
                      t->name, t->pid, sig, r ? r->rip : 0ULL, (unsigned long long)t->sig_si[sig].addr);
    }
    signal_terminate(t, sig);
}

static int build_frame(task_t *t, int sig, const ksiginfo_t *si, sig_regs_t *r, bool full,
                       uint64_t *new_sp, uint64_t *new_rip) {
    uint64_t h = t->sig_handler[sig];
    uint64_t restorer = t->sig_restorer[sig];
    if (!restorer) return -1;

    uint64_t top = r->rsp;
    bool use_alt = (t->sig_flags[sig] & SA_ONSTACK) && t->ss_size &&
                   !(t->ss_flags & SS_DISABLE_F) && !on_altstack(t, r->rsp);
    if (use_alt) top = t->ss_sp + t->ss_size;
    else top -= 128;
    uint64_t sp = (top - FRAME_SIZE) & ~0xFULL;
    if (sp < 0x1000 || top > USER_TOP) return -1;
    if (!syscall_uptr_validate_write((void *)sp, FRAME_SIZE)) return -1;

    uint64_t mask = t->sig_saved_valid ? t->sig_saved_mask : t->sig_blocked;
    t->sig_saved_valid = 0;

    uint8_t *f = (uint8_t *)sp;
    memset(f, 0, FRAME_SIZE);
    uint64_t *base = (uint64_t *)f;
    base[0] = h;
    base[1] = (uint64_t)sig;

    sig_ucontext_t *uc = (sig_ucontext_t *)(f + FRAME_UC);
    uc->rip    = r->rip;
    uc->rsp    = r->rsp;
    uc->rflags = r->rflags;
    uc->rax    = r->rax;
    uc->rbx    = r->rbx;
    uc->rbp    = r->rbp;
    uc->r12    = r->r12;
    uc->r13    = r->r13;
    uc->r14    = r->r14;
    uc->r15    = r->r15;
    uc->mask   = mask;

    sig_ext_t *ext = (sig_ext_t *)(f + FRAME_EXT);
    ext->magic = SIGFRAME_MAGIC;
    ext->flags = (full ? SF_FULL : 0) | ((t->sig_flags[sig] & SA_SIGINFO) ? SF_SIGINFO : 0);
    ext->uctx  = sp + FRAME_UCTX;
    ext->info  = sp + FRAME_INFO;

    fill_siginfo(f + FRAME_INFO, sig, si);

    k_ucontext_t *kc = (k_ucontext_t *)(f + FRAME_UCTX);
    kc->ss_sp    = t->ss_sp;
    kc->ss_size  = t->ss_size;
    kc->ss_flags = t->ss_size ? (on_altstack(t, r->rsp) ? SS_ONSTACK_F : 0) : SS_DISABLE_F;
    memcpy(kc->gregs, r, sizeof(*r));
    kc->gregs[18] = 0x23;
    kc->gregs[19] = (uint64_t)(int64_t)si->err;
    kc->gregs[20] = (uint64_t)(int64_t)si->trapno;
    kc->gregs[21] = mask;
    kc->gregs[22] = si->addr;
    kc->fpregs = sp + FRAME_FPU;
    kc->sigmask[0] = mask;

    uint8_t fx[512] __attribute__((aligned(16)));
    __asm__ volatile("fxsave64 (%0)" :: "r"(fx) : "memory");
    memcpy(f + FRAME_FPU, fx, 512);

    t->sig_blocked |= t->sig_mask[sig];
    if (!(t->sig_flags[sig] & SA_NODEFER)) t->sig_blocked |= (1ULL << sig);
    if (t->sig_flags[sig] & SA_RESETHAND)  t->sig_handler[sig] = SIG_DFL_ADDR;

    *new_sp = sp;
    *new_rip = restorer;
    return 0;
}

static int pick_signal(task_t *t) {
    for (;;) {
        uint64_t deliverable = t->sig_pending & ~t->sig_blocked;
        if (!deliverable) return 0;
        int sig = __builtin_ctzll(deliverable);
        if (sig <= 0 || sig >= NSIG) { t->sig_pending &= ~1ULL; continue; }
        uint64_t h = t->sig_handler[sig];
        if (sig == SIGKILL) return sig;
        if (h == SIG_IGN_ADDR || (h == SIG_DFL_ADDR && sig_default_ignore(sig))) {
            discard_signal(t, sig);
            continue;
        }
        return sig;
    }
}

int64_t signal_deliver_pending(task_t *t, int64_t retval) {
    if (!t || !t->is_userspace) return retval;

    int sig = pick_signal(t);
    if (!sig) { signal_restore_saved_mask(t); return retval; }

    sig_regs_t r;
    r.r8  = t->user_saved_r8;
    r.r9  = t->user_saved_r9;
    r.r10 = t->user_saved_r10;
    r.r11 = t->ret_iret ? t->user_iret_r11 : t->user_saved_r11;
    r.r12 = t->user_saved_r12;
    r.r13 = t->user_saved_r13;
    r.r14 = t->user_saved_r14;
    r.r15 = t->user_saved_r15;
    r.rdi = t->user_saved_rdi;
    r.rsi = t->user_saved_rsi;
    r.rbp = t->user_saved_rbp;
    r.rbx = t->user_saved_rbx;
    r.rdx = t->user_saved_rdx;
    r.rax = (uint64_t)retval;
    r.rcx = t->ret_iret ? t->user_saved_rcx : t->user_saved_rip;
    r.rsp = t->user_rsp;
    r.rip = t->user_saved_rip;
    r.rflags = t->user_saved_r11;

    if (sig == SIGKILL || t->sig_handler[sig] == SIG_DFL_ADDR) {
        discard_signal(t, sig);
        terminate_with_report(t, sig, &r);
    }

    ksiginfo_t si;
    take_signal(t, sig, &si);
    uint64_t sp, rip;
    if (build_frame(t, sig, &si, &r, t->ret_iret != 0, &sp, &rip) < 0)
        terminate_with_report(t, SIGSEGV, &r);
    t->ret_iret = 0;
    t->user_rsp = sp;
    t->user_saved_rip = rip;
    t->user_saved_r11 &= ~0x500ULL;
    return retval;
}

void signal_irq_return(struct int_frame_t *f) {
    extern task_t *syscall_cur_task(void);
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace || (f->cs & 3) != 3) return;
    if (t->pending_kill) signal_kill_exit(t);
    if (!(t->sig_pending & ~t->sig_blocked)) return;

    __asm__ volatile("sti" ::: "memory");
    int sig = pick_signal(t);
    if (!sig) { __asm__ volatile("cli" ::: "memory"); return; }

    sig_regs_t r;
    r.r8 = f->r8; r.r9 = f->r9; r.r10 = f->r10; r.r11 = f->r11;
    r.r12 = f->r12; r.r13 = f->r13; r.r14 = f->r14; r.r15 = f->r15;
    r.rdi = f->rdi; r.rsi = f->rsi; r.rbp = f->rbp; r.rbx = f->rbx;
    r.rdx = f->rdx; r.rax = f->rax; r.rcx = f->rcx;
    r.rsp = f->rsp; r.rip = f->rip; r.rflags = f->rflags;

    if (sig == SIGKILL || t->sig_handler[sig] == SIG_DFL_ADDR) {
        discard_signal(t, sig);
        terminate_with_report(t, sig, &r);
    }

    ksiginfo_t si;
    take_signal(t, sig, &si);
    uint64_t sp, rip;
    if (build_frame(t, sig, &si, &r, true, &sp, &rip) < 0)
        terminate_with_report(t, SIGSEGV, &r);
    __asm__ volatile("cli" ::: "memory");
    f->rsp = sp;
    f->rip = rip;
    f->rflags &= ~0x500ULL;
}

int64_t sys_sigaltstack(uint64_t ss_ptr, uint64_t old_ptr) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    struct { uint64_t sp; int32_t flags; int32_t pad; uint64_t size; } st;
    bool onstack = on_altstack(t, t->user_rsp);
    if (old_ptr) {
        memset(&st, 0, sizeof(st));
        st.sp = t->ss_sp;
        st.size = t->ss_size;
        st.flags = t->ss_size ? (onstack ? SS_ONSTACK_F : 0) : SS_DISABLE_F;
        if (syscall_copy_to_user((void *)old_ptr, &st, sizeof(st)) < 0) return -EFAULT;
    }
    if (ss_ptr) {
        if (syscall_copy_from_user(&st, (const void *)ss_ptr, sizeof(st)) < 0) return -EFAULT;
        if (onstack) return -EPERM;
        if (st.flags & ~(SS_DISABLE_F | (1 << 31))) return -EINVAL;
        if (st.flags & SS_DISABLE_F) {
            t->ss_sp = 0;
            t->ss_size = 0;
            t->ss_flags = SS_DISABLE_F;
        } else {
            if (st.size < 2048) return -ENOMEM;
            if (st.sp >= USER_TOP || st.sp + st.size > USER_TOP) return -EINVAL;
            t->ss_sp = st.sp;
            t->ss_size = st.size;
            t->ss_flags = 0;
        }
    }
    return 0;
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
        uint64_t m = a->mask & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
        t->sig_handler[sig]  = a->handler;
        t->sig_flags[sig]    = a->flags;
        t->sig_restorer[sig] = a->restorer;
        t->sig_mask[sig]     = m;
        bool drop = a->handler == SIG_IGN_ADDR || (a->handler == SIG_DFL_ADDR && sig_default_ignore((int)sig));
        if (drop) discard_signal(t, (int)sig);
        if (t->mm) {
            uint32_t pids[256];
            int n = task_collect_pids(pids, 256);
            for (int i = 0; i < n; i++) {
                task_t *o = task_find_by_pid(pids[i]);
                if (!o || o == t || o->mm != t->mm) continue;
                o->sig_handler[sig]  = a->handler;
                o->sig_flags[sig]    = a->flags;
                o->sig_restorer[sig] = a->restorer;
                o->sig_mask[sig]     = m;
                if (drop) discard_signal(o, (int)sig);
            }
        }
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

int signal_may_send(task_t *me, task_t *t) {
    if (!me || !t) return 1;
    if (me->uid == 0 || cap_has(me->capabilities, CAP_KILL_ANY)) return 1;
    return t->is_userspace && me->uid == t->uid;
}

int64_t sys_kill(uint64_t pid_a, uint64_t sig_a) {
    int pid = (int)(int64_t)pid_a;
    int sig = (int)sig_a;
    if (sig < 0 || sig >= NSIG) return -EINVAL;

    task_t *me = syscall_cur_task();
    if (pid > 0) {
        task_t *t = task_find_by_pid((uint32_t)pid);
        if (!t) return -ESRCH;
        if (!signal_may_send(me, t)) return -EPERM;
        if (sig != 0) signal_send_from(t, sig, me);
        return 0;
    }

    uint32_t pgid;
    if (pid == 0)       pgid = me ? me->pgid : 0;
    else if (pid < -1)  pgid = (uint32_t)(-pid);
    else                return -EPERM;

    uint32_t pids[256];
    int n = task_collect_pids(pids, 256);
    int found = 0, sent = 0;
    for (int i = 0; i < n; i++) {
        task_t *t = task_find_by_pid(pids[i]);
        if (t && t->pgid == pgid) {
            found = 1;
            if (!signal_may_send(me, t)) continue;
            sent = 1;
            if (sig != 0) signal_send_from(t, sig, me);
        }
    }
    if (!found) return -ESRCH;
    return sent ? 0 : -EPERM;
}

int64_t sys_sigqueue(uint64_t pid_a, uint64_t sig_a, uint64_t value) {
    int pid = (int)(int64_t)pid_a;
    int sig = (int)sig_a;
    if (sig < 0 || sig >= NSIG || pid <= 0) return -EINVAL;
    task_t *me = syscall_cur_task();
    task_t *t = task_find_by_pid((uint32_t)pid);
    if (!t) return -ESRCH;
    if (!signal_may_send(me, t)) return -EPERM;
    if (sig == 0) return 0;
    ksiginfo_t si = {
        .code  = SI_QUEUE_CODE,
        .pid   = me ? me->pid : 0,
        .uid   = me ? me->uid : 0,
        .value = value,
    };
    return signal_post(t, sig, &si);
}

int64_t sys_sigtimedwait(uint64_t set_ptr, uint64_t info_ptr, uint64_t ts_ptr) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    uint64_t set;
    if (syscall_copy_from_user(&set, (const void *)set_ptr, sizeof(set)) < 0) return -EFAULT;
    set &= ~((1ULL << SIGKILL) | (1ULL << SIGSTOP) | 1ULL);

    uint64_t deadline = 0;
    bool timed = false;
    if (ts_ptr) {
        struct { int64_t sec; int64_t nsec; } ts;
        if (syscall_copy_from_user(&ts, (const void *)ts_ptr, sizeof(ts)) < 0) return -EFAULT;
        if (ts.sec < 0 || ts.nsec < 0 || ts.nsec >= 1000000000LL) return -EINVAL;
        timed = true;
        deadline = sched_now_ns() + (uint64_t)ts.sec * 1000000000ULL + (uint64_t)ts.nsec;
    }

    for (;;) {
        uint64_t ready = t->sig_pending & set;
        if (ready) {
            int sig = __builtin_ctzll(ready);
            ksiginfo_t si;
            take_signal(t, sig, &si);
            if (info_ptr) {
                uint8_t info[128];
                fill_siginfo(info, sig, &si);
                if (syscall_copy_to_user((void *)info_ptr, info, sizeof(info)) < 0) return -EFAULT;
            }
            return sig;
        }
        if (t->pending_kill || (t->sig_pending & ~t->sig_blocked)) return -EINTR;
        if (timed && sched_now_ns() >= deadline) return -EAGAIN;

        t->sig_waitset = set;
        t->wakeup_time_ns = timed ? deadline : 0;
        if (timed) sched_note_wakeup(deadline);
        t->runnable = false;
        t->state = TASK_BLOCKED;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if ((t->sig_pending & set) || (t->sig_pending & ~t->sig_blocked) || t->pending_kill)
            task_unblock(t);
        sched_reschedule();
        t->sig_waitset = 0;
        t->wakeup_time_ns = 0;
    }
}

int64_t sys_rt_sigreturn(void) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    uint64_t sp = t->user_rsp;
    if (!syscall_uptr_validate((void *)sp, sizeof(sig_ucontext_t) + sizeof(sig_ext_t)))
        signal_terminate(t, SIGSEGV);

    sig_ucontext_t *uc = (sig_ucontext_t *)sp;
    sig_ext_t *ext = (sig_ext_t *)(sp + sizeof(sig_ucontext_t));
    if (ext->magic != SIGFRAME_MAGIC) {
        t->user_saved_rip = uc->rip;
        t->user_rsp       = uc->rsp;
        t->user_saved_r11 = uc->rflags;
        t->user_saved_rbx = uc->rbx;
        t->user_saved_rbp = uc->rbp;
        t->user_saved_r12 = uc->r12;
        t->user_saved_r13 = uc->r13;
        t->user_saved_r14 = uc->r14;
        t->user_saved_r15 = uc->r15;
        t->sig_blocked    = uc->mask & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
        t->ret_iret       = 0;
        return (int64_t)uc->rax;
    }

    uint64_t up = ext->uctx;
    if (!syscall_uptr_validate((void *)up, sizeof(k_ucontext_t))) signal_terminate(t, SIGSEGV);
    k_ucontext_t kc;
    memcpy(&kc, (void *)up, sizeof(kc));
    uint64_t *g = kc.gregs;
    if (g[16] >= USER_TOP || g[15] >= USER_TOP) signal_terminate(t, SIGSEGV);

    if (kc.fpregs) {
        if (!syscall_uptr_validate((void *)kc.fpregs, 512)) signal_terminate(t, SIGSEGV);
        uint8_t fx[512] __attribute__((aligned(16)));
        memcpy(fx, (void *)kc.fpregs, 512);
        uint32_t mxcsr;
        memcpy(&mxcsr, fx + 24, 4);
        mxcsr &= 0xFFBFu;
        memcpy(fx + 24, &mxcsr, 4);
        uint32_t mask_bits = 0xFFBFu;
        memcpy(fx + 28, &mask_bits, 4);
        __asm__ volatile("fxrstor64 (%0)" :: "r"(fx) : "memory");
    }

    t->user_saved_r8  = g[0];
    t->user_saved_r9  = g[1];
    t->user_saved_r10 = g[2];
    t->user_saved_r11 = g[17];
    t->user_iret_r11  = g[3];
    t->user_saved_r12 = g[4];
    t->user_saved_r13 = g[5];
    t->user_saved_r14 = g[6];
    t->user_saved_r15 = g[7];
    t->user_saved_rdi = g[8];
    t->user_saved_rsi = g[9];
    t->user_saved_rbp = g[10];
    t->user_saved_rbx = g[11];
    t->user_saved_rdx = g[12];
    t->user_saved_rcx = g[14];
    t->user_rsp       = g[15];
    t->user_saved_rip = g[16];
    t->sig_blocked    = kc.sigmask[0] & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
    t->ret_iret       = (ext->flags & SF_FULL) ? 1 : 0;
    return (int64_t)g[13];
}

int64_t sys_rt_sigpending(uint64_t set, uint64_t sigsetsize) {
    (void)sigsetsize;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (!syscall_uptr_validate((void *)set, 8)) return -EFAULT;
    *(uint64_t *)set = t->sig_pending & t->sig_blocked;
    return 0;
}

int64_t sys_rt_sigsuspend(uint64_t set, uint64_t sigsetsize) {
    (void)sigsetsize;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (!syscall_uptr_validate((void *)set, 8)) return -EFAULT;
    uint64_t m = (*(uint64_t *)set) & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
    t->sig_saved_mask = t->sig_blocked;
    t->sig_saved_valid = 1;
    t->sig_blocked = m;
    while (!signal_pending_deliverable(t) && !t->pending_kill) {
        t->runnable = false;
        t->state = TASK_BLOCKED;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (signal_pending_deliverable(t) || t->pending_kill) task_unblock(t);
        sched_reschedule();
    }
    return -EINTR;
}

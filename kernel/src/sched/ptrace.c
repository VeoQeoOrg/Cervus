#include "../../include/gdt/gdt.h"
#include "../../include/sched/ptrace.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/spinlock.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/signal/signal.h"
#include "../../include/interrupts/interrupts.h"
#include "../../include/memory/mm.h"
#include "../../include/memory/pmm.h"
#include "../../include/sse/fpu.h"
#include "../../include/puzzle/puzzle.h"
#include "../../include/io/serial.h"
#include <string.h>

#define WNOHANG_K   0x00000001
#define WNOWAIT_K   0x01000000
#define P_PID_K     1
#define P_PGID_K    2
#define CLD_TRAPPED 4
#define USER_TOP    0x0000800000000000ULL
#define NT_PRSTATUS 1
#define NT_PRFPREG  2
#define NT_X86_XSTATE 0x202
#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004
#define USER_DEBUGREG_OFF 848
#define USER_REGS_SIZE    216

typedef struct {
    uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8;
    uint64_t rax, rcx, rdx, rsi, rdi, orig_rax, rip, cs, eflags, rsp, ss;
    uint64_t fs_base, gs_base, ds, es, fs, gs;
} user_regs_t;

_Static_assert(sizeof(user_regs_t) == USER_REGS_SIZE, "user_regs_struct is 216 bytes");

typedef struct { uint32_t tracer; uint32_t pid; int32_t status; } pt_exit_rec_t;

static spinlock_t    g_pt_lock = SPINLOCK_INIT;
static task_t       *g_tracees;
static pt_exit_rec_t g_exits[64];
static int           g_nexits;

extern void signal_fill_siginfo(uint8_t *info, int sig, const ksiginfo_t *si);

static task_t *self_task(void) { return syscall_cur_task(); }

static bool same_group(task_t *a, task_t *b) {
    return a && b && task_leader(a) == task_leader(b);
}

bool ptrace_traced(task_t *t) {
    return t && t->ptracer && (t->pt_flags & PT_TRACED);
}

static void list_add(task_t *t) {
    t->pt_next = g_tracees;
    g_tracees = t;
}

static void list_del(task_t *t) {
    for (task_t **pp = &g_tracees; *pp; pp = &(*pp)->pt_next) {
        if (*pp == t) { *pp = t->pt_next; break; }
    }
    t->pt_next = NULL;
}

static void notify_tracer(task_t *tracer, task_t *t, int status, int code) {
    ksiginfo_t si = { .code = code, .pid = t->pid, .uid = t->uid };
    si.status = code == CLD_TRAPPED ? (status >> 8) & 0xFF : status;
    task_wakeup_waiters(t->pid);
    signal_post(tracer, SIGCHLD, &si);
}

static uint64_t sane_flags(uint64_t old, uint64_t want) {
    const uint64_t user_bits = 0x00000000003C0DD5ULL | 0x100ULL;
    return (old & ~user_bits) | (want & user_bits) | 0x202;
}

static void ctx_get(task_t *t, pt_ctx_t *c, user_regs_t *r) {
    memset(r, 0, sizeof *r);
    if (c->kind == PT_CTX_FRAME) {
        struct int_frame_t *f = c->f;
        r->r15 = f->r15; r->r14 = f->r14; r->r13 = f->r13; r->r12 = f->r12;
        r->rbp = f->rbp; r->rbx = f->rbx; r->r11 = f->r11; r->r10 = f->r10;
        r->r9 = f->r9; r->r8 = f->r8; r->rax = f->rax; r->rcx = f->rcx;
        r->rdx = f->rdx; r->rsi = f->rsi; r->rdi = f->rdi;
        r->rip = f->rip; r->cs = f->cs; r->eflags = f->rflags; r->rsp = f->rsp; r->ss = f->ss;
        r->orig_rax = c->orig_rax;
    } else {
        r->r15 = t->user_saved_r15; r->r14 = t->user_saved_r14;
        r->r13 = t->user_saved_r13; r->r12 = t->user_saved_r12;
        r->rbp = t->user_saved_rbp; r->rbx = t->user_saved_rbx;
        r->r11 = t->ret_iret ? t->user_iret_r11 : t->user_saved_r11;
        r->r10 = t->user_saved_r10; r->r9 = t->user_saved_r9; r->r8 = t->user_saved_r8;
        r->rax = c->rax ? (uint64_t)*c->rax : 0;
        r->rcx = t->ret_iret ? t->user_saved_rcx : t->user_saved_rip;
        r->rdx = t->user_saved_rdx; r->rsi = t->user_saved_rsi; r->rdi = t->user_saved_rdi;
        r->orig_rax = c->orig_rax;
        r->rip = t->user_saved_rip;
        r->cs = t->ret_iret == 2 ? GDT_USER_CODE32_RPL3 : GDT_USER_CODE_RPL3;
        r->eflags = (t->user_saved_r11 & 0x3C0FFF) | 0x202;
        r->rsp = t->user_rsp; r->ss = GDT_USER_DATA_RPL3;
    }
    r->fs_base = t->fs_base;
    r->gs_base = t->gs_base;
}

static int ctx_set(task_t *t, pt_ctx_t *c, const user_regs_t *r) {
    if (r->rip >= USER_TOP || r->rsp >= USER_TOP || r->fs_base >= USER_TOP || r->gs_base >= USER_TOP) return -EIO;
    if (c->kind == PT_CTX_FRAME) {
        struct int_frame_t *f = c->f;
        f->r15 = r->r15; f->r14 = r->r14; f->r13 = r->r13; f->r12 = r->r12;
        f->rbp = r->rbp; f->rbx = r->rbx; f->r11 = r->r11; f->r10 = r->r10;
        f->r9 = r->r9; f->r8 = r->r8; f->rax = r->rax; f->rcx = r->rcx;
        f->rdx = r->rdx; f->rsi = r->rsi; f->rdi = r->rdi;
        f->rip = r->rip; f->rsp = r->rsp;
        f->rflags = sane_flags(f->rflags, r->eflags);
        if (r->cs == GDT_USER_CODE32_RPL3 || r->cs == GDT_USER_CODE_RPL3) f->cs = r->cs;
    } else {
        t->user_saved_r15 = r->r15; t->user_saved_r14 = r->r14;
        t->user_saved_r13 = r->r13; t->user_saved_r12 = r->r12;
        t->user_saved_rbp = r->rbp; t->user_saved_rbx = r->rbx;
        t->user_saved_r10 = r->r10; t->user_saved_r9 = r->r9; t->user_saved_r8 = r->r8;
        t->user_saved_rdx = r->rdx; t->user_saved_rsi = r->rsi; t->user_saved_rdi = r->rdi;
        t->user_saved_rcx = r->rcx;
        t->user_iret_r11  = r->r11;
        t->user_saved_r11 = sane_flags(t->user_saved_r11, r->eflags) & 0x3C0FFF;
        t->user_saved_rip = r->rip;
        t->user_rsp       = r->rsp;
        if (c->rax) *c->rax = (int64_t)r->rax;
        c->orig_rax = r->orig_rax;
        t->ret_iret = r->cs == GDT_USER_CODE32_RPL3 ? 2 : 1;
    }
    t->fs_base = r->fs_base;
    t->gs_base = r->gs_base;
    return 0;
}

static void ctx_set_tf(task_t *t, pt_ctx_t *c, bool on) {
    if (c->kind == PT_CTX_FRAME) {
        if (on) c->f->rflags |= 0x100; else c->f->rflags &= ~0x100ULL;
    } else {
        if (on) t->user_saved_r11 |= 0x100; else t->user_saved_r11 &= ~0x100ULL;
    }
}

static void stop_siginfo(task_t *t, int status) {
    ksiginfo_t si = { .code = (status >> 8) & 0xFFFF, .pid = t->pid, .uid = t->uid };
    signal_fill_siginfo(t->pt_siginfo, SIGTRAP, &si);
    int32_t code = (status >> 8) & 0xFFFF;
    memcpy(t->pt_siginfo + 8, &code, 4);
}

int ptrace_stop(task_t *t, int status, pt_ctx_t *ctx) {
    uint64_t f = spinlock_acquire_irqsave(&g_pt_lock);
    task_t *tracer = t->ptracer;
    if (!tracer || !(t->pt_flags & PT_TRACED) || t->pending_kill) {
        spinlock_release_irqrestore(&g_pt_lock, f);
        return 0;
    }
    t->pt_status = status;
    t->pt_ctx = ctx;
    t->pt_resume_sig = 0;
    t->pt_flags |= PT_STOPPED | PT_NOTIFY;
    spinlock_release_irqrestore(&g_pt_lock, f);

    notify_tracer(tracer, t, status, CLD_TRAPPED);

    t->sleep_noint++;
    for (;;) {
        f = spinlock_acquire_irqsave(&g_pt_lock);
        if (!(t->pt_flags & PT_STOPPED) || t->pending_kill || !t->ptracer) {
            t->pt_flags &= ~(PT_STOPPED | PT_NOTIFY);
            t->pt_ctx = NULL;
            spinlock_release_irqrestore(&g_pt_lock, f);
            break;
        }
        t->wakeup_time_ns = 0;
        t->runnable = false;
        t->state = TASK_BLOCKED;
        spinlock_release_irqrestore(&g_pt_lock, f);
        sched_reschedule();
    }
    t->sleep_noint--;
    if (ctx) ctx_set_tf(t, ctx, (t->pt_flags & PT_STEP) != 0);
    return t->pt_resume_sig;
}

static void si_from_raw(const uint8_t *info, ksiginfo_t *si) {
    memset(si, 0, sizeof *si);
    int32_t code;
    memcpy(&code, info + 8, 4);
    si->code = code;
    memcpy(&si->pid, info + 16, 4);
    memcpy(&si->uid, info + 20, 4);
    memcpy(&si->addr, info + 16, 8);
    memcpy(&si->value, info + 24, 8);
}

int ptrace_signal_stop(task_t *t, int sig, void *sip, pt_ctx_t *ctx) {
    ksiginfo_t *si = sip;
    signal_fill_siginfo(t->pt_siginfo, sig, si);
    int nsig = ptrace_stop(t, (sig << 8) | 0x7F, ctx);
    if (t->pending_kill || nsig <= 0 || nsig >= NSIG) return 0;
    int32_t raw_signo;
    memcpy(&raw_signo, t->pt_siginfo, 4);
    if (nsig == raw_signo) {
        si_from_raw(t->pt_siginfo, si);
    } else {
        task_t *tr = t->ptracer;
        memset(si, 0, sizeof *si);
        si->code = SI_USER_CODE;
        si->pid = tr ? tr->pid : 0;
        si->uid = tr ? tr->uid : 0;
    }
    return nsig;
}

static int event_status(int event) {
    return (event << 16) | (SIGTRAP << 8) | 0x7F;
}

void ptrace_syscall_enter(task_t *t, uint64_t *nr, uint64_t args[6], int64_t *ret, bool *skip) {
    *skip = false;
    if (!ptrace_traced(t) || !(t->pt_flags & PT_SYSCALL)) return;
    int64_t rax = -ENOSYS;
    pt_ctx_t ctx = { .kind = PT_CTX_SYSCALL, .rax = &rax, .orig_rax = *nr };
    int sig = SIGTRAP | ((t->pt_opts & PTRACE_O_TRACESYSGOOD) ? 0x80 : 0);
    stop_siginfo(t, sig << 8);
    ptrace_stop(t, (sig << 8) | 0x7F, &ctx);
    if (t->pending_kill) return;
    *nr = ctx.orig_rax;
    args[0] = t->user_saved_rdi; args[1] = t->user_saved_rsi; args[2] = t->user_saved_rdx;
    args[3] = t->user_saved_r10; args[4] = t->user_saved_r8;  args[5] = t->user_saved_r9;
    if (*nr == (uint64_t)-1) { *skip = true; *ret = rax; }
}

void ptrace_syscall_exit(task_t *t, uint64_t nr, int64_t *ret) {
    if (!ptrace_traced(t)) { t->pt_pending = 0; return; }
    pt_ctx_t ctx = { .kind = PT_CTX_SYSCALL, .rax = ret, .orig_rax = nr };
    if (t->pt_pending & PT_PEND_EVENT) {
        t->pt_pending &= ~PT_PEND_EVENT;
        int st = event_status(t->pt_event);
        stop_siginfo(t, st);
        ptrace_stop(t, st, &ctx);
        if (t->pending_kill) return;
    }
    if (t->pt_pending & PT_PEND_EXEC) {
        t->pt_pending &= ~PT_PEND_EXEC;
        if (t->pt_opts & PTRACE_O_TRACEEXEC) {
            int st = event_status(PTRACE_EVENT_EXEC);
            stop_siginfo(t, st);
            ptrace_stop(t, st, &ctx);
            if (t->pending_kill) return;
        } else {
            signal_send(t, SIGTRAP);
        }
    }
    if (t->pt_flags & PT_SYSCALL) {
        int sig = SIGTRAP | ((t->pt_opts & PTRACE_O_TRACESYSGOOD) ? 0x80 : 0);
        stop_siginfo(t, sig << 8);
        ptrace_stop(t, (sig << 8) | 0x7F, &ctx);
    }
}

void ptrace_note_exec(task_t *t) {
    if (!ptrace_traced(t)) return;
    t->pt_msg = t->pid;
    t->pt_pending |= PT_PEND_EXEC;
    if (t->puzzle) { puzzle_destroy(t->puzzle); t->puzzle = NULL; }
}

void ptrace_note_child(task_t *parent, task_t *child, int event) {
    if (!ptrace_traced(parent) || !child) return;
    uint32_t want = event == PTRACE_EVENT_CLONE ? PTRACE_O_TRACECLONE
                  : event == PTRACE_EVENT_VFORK ? PTRACE_O_TRACEVFORK : PTRACE_O_TRACEFORK;
    if (!(parent->pt_opts & want)) return;
    uint64_t f = spinlock_acquire_irqsave(&g_pt_lock);
    if (parent->ptracer && !child->ptracer) {
        child->ptracer = parent->ptracer;
        child->pt_opts = parent->pt_opts;
        child->pt_flags = PT_TRACED | (parent->pt_flags & PT_SEIZED);
        list_add(child);
    }
    spinlock_release_irqrestore(&g_pt_lock, f);
    if (child->puzzle) { puzzle_destroy(child->puzzle); child->puzzle = NULL; }
    signal_send(child, SIGSTOP);
    parent->pt_msg = child->pid;
    parent->pt_event = event;
    parent->pt_pending |= PT_PEND_EVENT;
}

void ptrace_task_exit(task_t *t, int status) {
    if (!ptrace_traced(t) || !(t->pt_opts & PTRACE_O_TRACEEXIT)) return;
    int64_t rax = 0;
    pt_ctx_t ctx = { .kind = PT_CTX_SYSCALL, .rax = &rax, .orig_rax = (uint64_t)-1 };
    t->pt_msg = (uint64_t)(uint32_t)status;
    int st = event_status(PTRACE_EVENT_EXIT);
    stop_siginfo(t, st);
    ptrace_stop(t, st, &ctx);
}

static void detach_locked(task_t *t) {
    list_del(t);
    t->ptracer = NULL;
    t->pt_flags &= ~(PT_TRACED | PT_SEIZED | PT_SYSCALL | PT_STEP | PT_NOTIFY);
    t->pt_opts = 0;
    t->pt_pending = 0;
    t->pt_dr_on = 0;
}

void ptrace_task_dead(task_t *t, int status) {
    task_t *tracer = NULL;
    uint64_t f = spinlock_acquire_irqsave(&g_pt_lock);
    for (task_t *c = g_tracees, *n; c; c = n) {
        n = c->pt_next;
        if (c->ptracer != t) continue;
        bool kill = (c->pt_opts & PTRACE_O_EXITKILL) != 0;
        bool stopped = (c->pt_flags & PT_STOPPED) != 0;
        detach_locked(c);
        c->pt_flags &= ~PT_STOPPED;
        if (kill) signal_send(c, SIGKILL);
        else if (stopped) task_unblock(c);
    }
    if (t->ptracer && (t->pt_flags & PT_TRACED)) {
        tracer = t->ptracer;
        task_t *parent = task_find_by_pid(task_leader(t)->ppid);
        bool parent_reports = task_leader(t) == t && parent && same_group(parent, tracer);
        if (!parent_reports && g_nexits < (int)(sizeof g_exits / sizeof g_exits[0])) {
            g_exits[g_nexits].tracer = task_leader(tracer)->pid;
            g_exits[g_nexits].pid = t->pid;
            g_exits[g_nexits].status = status;
            g_nexits++;
        } else if (parent_reports) {
            tracer = NULL;
        }
        detach_locked(t);
    }
    spinlock_release_irqrestore(&g_pt_lock, f);
    if (tracer) notify_tracer(tracer, t, status, (status & 0x7F) ? CLD_KILLED_CODE : CLD_EXITED_CODE);
}

void ptrace_unlink(task_t *t) {
    uint64_t f = spinlock_acquire_irqsave(&g_pt_lock);
    for (task_t *c = g_tracees, *n; c; c = n) {
        n = c->pt_next;
        if (c->ptracer == t) detach_locked(c);
    }
    if (t->ptracer) detach_locked(t);
    spinlock_release_irqrestore(&g_pt_lock, f);
}

static bool selected(task_t *c, int idtype, uint32_t id) {
    if (idtype == P_PID_K) return c->pid == id;
    if (idtype == P_PGID_K) return c->pgid == id;
    return true;
}

int ptrace_wait(task_t *self, int idtype, uint32_t id, int options, int *status, bool *any) {
    task_t *me = task_leader(self);
    uint64_t f = spinlock_acquire_irqsave(&g_pt_lock);
    for (int i = 0; i < g_nexits; i++) {
        if (g_exits[i].tracer != me->pid) continue;
        if (idtype == P_PID_K && g_exits[i].pid != id) continue;
        *any = true;
        int pid = (int)g_exits[i].pid;
        *status = g_exits[i].status;
        if (!(options & WNOWAIT_K)) {
            g_exits[i] = g_exits[--g_nexits];
        }
        spinlock_release_irqrestore(&g_pt_lock, f);
        return pid;
    }
    for (task_t *c = g_tracees; c; c = c->pt_next) {
        if (!c->ptracer || task_leader(c->ptracer) != me) continue;
        if (!selected(c, idtype, id)) continue;
        *any = true;
        if ((c->pt_flags & (PT_STOPPED | PT_NOTIFY)) == (PT_STOPPED | PT_NOTIFY)) {
            *status = c->pt_status;
            if (!(options & WNOWAIT_K)) c->pt_flags &= ~PT_NOTIFY;
            int pid = (int)c->pid;
            spinlock_release_irqrestore(&g_pt_lock, f);
            return pid;
        }
    }
    spinlock_release_irqrestore(&g_pt_lock, f);
    return 0;
}

static inline void wr_dr(int n, uint64_t v) {
    switch (n) {
    case 0: asm volatile("mov %0, %%dr0" :: "r"(v)); break;
    case 1: asm volatile("mov %0, %%dr1" :: "r"(v)); break;
    case 2: asm volatile("mov %0, %%dr2" :: "r"(v)); break;
    case 3: asm volatile("mov %0, %%dr3" :: "r"(v)); break;
    case 6: asm volatile("mov %0, %%dr6" :: "r"(v)); break;
    case 7: asm volatile("mov %0, %%dr7" :: "r"(v)); break;
    }
}

static inline uint64_t rd_dr6(void) {
    uint64_t v;
    asm volatile("mov %%dr6, %0" : "=r"(v));
    return v;
}

void ptrace_switch_dr(task_t *old, task_t *next) {
    bool had = old && old->pt_dr_on;
    bool want = next && next->pt_dr_on;
    if (!had && !want) return;
    if (had && !want) { wr_dr(7, 0x400); return; }
    wr_dr(7, 0x400);
    for (int i = 0; i < 4; i++) wr_dr(i, next->pt_dr[i]);
    wr_dr(7, next->pt_dr[7]);
}

int ptrace_debug_trap(task_t *t, struct int_frame_t *f, uint64_t *addr) {
    uint64_t dr6 = rd_dr6();
    wr_dr(6, 0xFFFF0FF0);
    int code = 2;
    if (dr6 & 0xF) {
        code = 4;
        for (int i = 0; i < 4; i++) if (dr6 & (1u << i)) { *addr = t->pt_dr[i]; break; }
    }
    if (t) {
        t->pt_dr[6] = dr6;
        if ((f->cs & 3) == 3 && (t->pt_flags & PT_STEP)) f->rflags &= ~0x100ULL;
    }
    return code;
}

static uint64_t dr7_sane(uint64_t v) {
    uint64_t en = v & 0xFF;
    uint64_t local = 0;
    for (int i = 0; i < 4; i++) if (en & (3u << (i * 2))) local |= 1u << (i * 2);
    return (v & 0xFFFF0000ULL) | local | 0x400;
}

static int peek_user(task_t *t, uint64_t off, uint64_t *out) {
    if (off & 7) return -EIO;
    if (off < USER_REGS_SIZE) {
        if (!t->pt_ctx) return -ESRCH;
        user_regs_t r;
        ctx_get(t, t->pt_ctx, &r);
        *out = ((uint64_t *)&r)[off / 8];
        return 0;
    }
    if (off >= USER_DEBUGREG_OFF && off < USER_DEBUGREG_OFF + 64) {
        *out = t->pt_dr[(off - USER_DEBUGREG_OFF) / 8];
        return 0;
    }
    *out = 0;
    return 0;
}

static int poke_user(task_t *t, uint64_t off, uint64_t val) {
    if (off & 7) return -EIO;
    if (off < USER_REGS_SIZE) {
        if (!t->pt_ctx) return -ESRCH;
        user_regs_t r;
        ctx_get(t, t->pt_ctx, &r);
        ((uint64_t *)&r)[off / 8] = val;
        return ctx_set(t, t->pt_ctx, &r);
    }
    if (off >= USER_DEBUGREG_OFF && off < USER_DEBUGREG_OFF + 64) {
        int n = (int)((off - USER_DEBUGREG_OFF) / 8);
        if (n == 4 || n == 5) return -EIO;
        if (n < 4) {
            if (val >= USER_TOP - 8) return -EIO;
            t->pt_dr[n] = val;
        } else if (n == 6) {
            t->pt_dr[6] = val;
        } else {
            t->pt_dr[7] = dr7_sane(val);
            t->pt_dr_on = (t->pt_dr[7] & 0xFF) != 0;
        }
        return 0;
    }
    return 0;
}

static int regset_get(task_t *t, uint64_t type, uint64_t iov_ptr) {
    struct { uint64_t base, len; } iov;
    if (syscall_copy_from_user(&iov, (void *)iov_ptr, sizeof iov) < 0) return -EFAULT;
    if (type == NT_PRSTATUS) {
        if (!t->pt_ctx) return -ESRCH;
        user_regs_t r;
        ctx_get(t, t->pt_ctx, &r);
        size_t n = iov.len < sizeof r ? iov.len : sizeof r;
        if (syscall_copy_to_user((void *)iov.base, &r, n) < 0) return -EFAULT;
        iov.len = n;
    } else if (type == NT_PRFPREG || type == NT_X86_XSTATE) {
        if (!t->fpu_state) return -EIO;
        if (type == NT_X86_XSTATE && !fpu_xcr0) return -ENODEV;
        size_t total = type == NT_PRFPREG ? 512 : fpu_area_size;
        uint8_t *tmp = kmalloc(total);
        if (!tmp) return -ENOMEM;
        memcpy(tmp, t->fpu_state, total);
        if (type == NT_X86_XSTATE) memcpy(tmp + 464, &fpu_xcr0, 8);
        size_t n = iov.len < total ? iov.len : total;
        int r = syscall_copy_to_user((void *)iov.base, tmp, n);
        kfree(tmp);
        if (r < 0) return -EFAULT;
        iov.len = n;
    } else {
        return -EINVAL;
    }
    return syscall_copy_to_user((void *)iov_ptr, &iov, sizeof iov) < 0 ? -EFAULT : 0;
}

static int fpu_put(task_t *t, const uint8_t *src, size_t n, bool xstate) {
    if (!t->fpu_state) return -EIO;
    uint8_t *dst = (uint8_t *)t->fpu_state;
    uint8_t keep[48];
    memcpy(keep, dst + 464, sizeof keep);
    size_t lim = xstate ? fpu_area_size : 512;
    if (n > lim) n = lim;
    memcpy(dst, src, n);
    memcpy(dst + 464, keep, sizeof keep);
    uint32_t mx;
    memcpy(&mx, dst + 24, 4);
    mx &= fpu_mxcsr_mask ? fpu_mxcsr_mask : 0xFFBF;
    memcpy(dst + 24, &mx, 4);
    if (fpu_xcr0 && n > 512) {
        uint64_t bv;
        memcpy(&bv, dst + 512, 8);
        bv &= fpu_xcr0;
        memcpy(dst + 512, &bv, 8);
        memset(dst + 520, 0, 56);
    }
    return 0;
}

static int regset_set(task_t *t, uint64_t type, uint64_t iov_ptr) {
    struct { uint64_t base, len; } iov;
    if (syscall_copy_from_user(&iov, (void *)iov_ptr, sizeof iov) < 0) return -EFAULT;
    if (type == NT_PRSTATUS) {
        if (!t->pt_ctx) return -ESRCH;
        user_regs_t r;
        ctx_get(t, t->pt_ctx, &r);
        size_t n = iov.len < sizeof r ? iov.len : sizeof r;
        if (syscall_copy_from_user(&r, (void *)iov.base, n) < 0) return -EFAULT;
        return ctx_set(t, t->pt_ctx, &r);
    }
    if (type == NT_PRFPREG || type == NT_X86_XSTATE) {
        size_t total = type == NT_PRFPREG ? 512 : fpu_area_size;
        if (type == NT_X86_XSTATE && !fpu_xcr0) return -ENODEV;
        size_t n = iov.len < total ? iov.len : total;
        uint8_t *tmp = kmalloc(total);
        if (!tmp) return -ENOMEM;
        memcpy(tmp, t->fpu_state, total);
        int r = syscall_copy_from_user(tmp, (void *)iov.base, n) < 0 ? -EFAULT : fpu_put(t, tmp, n, type == NT_X86_XSTATE);
        kfree(tmp);
        return r;
    }
    return -EINVAL;
}

int ptrace_access(task_t *t, uintptr_t addr, void *buf, size_t len, bool write) {
    struct mm *m = t->mm;
    if (!m || !mm_tryget(m)) return -ESRCH;
    int r = mm_access_remote(m, addr, buf, len, write ? 1 : 0);
    mm_put(m);
    return r;
}

static int may_trace(task_t *me, task_t *t) {
    if (!t || !t->is_userspace) return -ESRCH;
    if (same_group(me, t)) return -EPERM;
    if (t->pid <= 1) return -EPERM;
    if (me->uid != 0 && (me->uid != t->uid)) return -EPERM;
    return 0;
}

static task_t *stopped_tracee(task_t *me, uint32_t pid) {
    task_t *t = task_find_by_pid(pid);
    if (!t || !t->ptracer || !same_group(t->ptracer, me)) return NULL;
    if (!(t->pt_flags & PT_STOPPED)) return NULL;
    return t;
}

static void resume(task_t *t, int sig, uint32_t mode) {
    uint64_t f = spinlock_acquire_irqsave(&g_pt_lock);
    t->pt_flags &= ~(PT_SYSCALL | PT_STEP);
    t->pt_flags |= mode;
    t->pt_resume_sig = sig;
    t->pt_flags &= ~(PT_STOPPED | PT_NOTIFY);
    spinlock_release_irqrestore(&g_pt_lock, f);
    task_unblock(t);
}

int64_t sys_ptrace(uint64_t req, uint64_t pid_a, uint64_t addr, uint64_t data) {
    task_t *me = self_task();
    if (!me) return -ESRCH;
    uint32_t pid = (uint32_t)pid_a;

    if (req == PTRACE_TRACEME) {
        task_t *parent = task_find_by_pid(task_leader(me)->ppid);
        if (!parent) return -EPERM;
        uint64_t f = spinlock_acquire_irqsave(&g_pt_lock);
        if (me->ptracer) { spinlock_release_irqrestore(&g_pt_lock, f); return -EPERM; }
        me->ptracer = parent;
        me->pt_flags = PT_TRACED;
        me->pt_opts = 0;
        list_add(me);
        spinlock_release_irqrestore(&g_pt_lock, f);
        return 0;
    }

    if (req == PTRACE_ATTACH || req == PTRACE_SEIZE) {
        task_t *t = task_find_by_pid(pid);
        int r = may_trace(me, t);
        if (r < 0) return r;
        if (req == PTRACE_SEIZE && (data & ~(uint64_t)PTRACE_O_MASK)) return -EINVAL;
        uint64_t f = spinlock_acquire_irqsave(&g_pt_lock);
        if (t->ptracer) { spinlock_release_irqrestore(&g_pt_lock, f); return -EPERM; }
        t->ptracer = me;
        t->pt_flags = PT_TRACED | (req == PTRACE_SEIZE ? PT_SEIZED : 0);
        t->pt_opts = req == PTRACE_SEIZE ? (uint32_t)data : 0;
        list_add(t);
        spinlock_release_irqrestore(&g_pt_lock, f);
        if (t->puzzle) { puzzle_destroy(t->puzzle); t->puzzle = NULL; }
        if (req == PTRACE_ATTACH) signal_send(t, SIGSTOP);
        return 0;
    }

    if (req == PTRACE_KILL) {
        task_t *t = task_find_by_pid(pid);
        if (!t || !t->ptracer || !same_group(t->ptracer, me)) return -ESRCH;
        signal_send(t, SIGKILL);
        if (t->pt_flags & PT_STOPPED) resume(t, 0, 0);
        return 0;
    }

    if (req == PTRACE_INTERRUPT) {
        task_t *t = task_find_by_pid(pid);
        if (!t || !t->ptracer || !same_group(t->ptracer, me)) return -ESRCH;
        if (!(t->pt_flags & PT_SEIZED)) return -EIO;
        if (!(t->pt_flags & PT_STOPPED)) signal_send(t, SIGSTOP);
        return 0;
    }

    task_t *t = stopped_tracee(me, pid);
    if (!t) return -ESRCH;
    pt_ctx_t *ctx = t->pt_ctx;

    switch (req) {
    case PTRACE_PEEKTEXT:
    case PTRACE_PEEKDATA: {
        uint64_t v;
        if (ptrace_access(t, addr, &v, 8, false) != 8) return -EIO;
        return syscall_copy_to_user((void *)data, &v, 8) < 0 ? -EFAULT : 0;
    }
    case PTRACE_POKETEXT:
    case PTRACE_POKEDATA:
        return ptrace_access(t, addr, &data, 8, true) == 8 ? 0 : -EIO;
    case PTRACE_PEEKUSER: {
        uint64_t v;
        int r = peek_user(t, addr, &v);
        if (r < 0) return r;
        return syscall_copy_to_user((void *)data, &v, 8) < 0 ? -EFAULT : 0;
    }
    case PTRACE_POKEUSER:
        return poke_user(t, addr, data);
    case PTRACE_GETREGS: {
        if (!ctx) return -ESRCH;
        user_regs_t r;
        ctx_get(t, ctx, &r);
        return syscall_copy_to_user((void *)data, &r, sizeof r) < 0 ? -EFAULT : 0;
    }
    case PTRACE_SETREGS: {
        if (!ctx) return -ESRCH;
        user_regs_t r;
        if (syscall_copy_from_user(&r, (void *)data, sizeof r) < 0) return -EFAULT;
        return ctx_set(t, ctx, &r);
    }
    case PTRACE_GETFPREGS:
        if (!t->fpu_state) return -EIO;
        return syscall_copy_to_user((void *)data, t->fpu_state, 512) < 0 ? -EFAULT : 0;
    case PTRACE_SETFPREGS: {
        uint8_t tmp[512];
        if (syscall_copy_from_user(tmp, (void *)data, sizeof tmp) < 0) return -EFAULT;
        return fpu_put(t, tmp, sizeof tmp, false);
    }
    case PTRACE_GETREGSET:
        return regset_get(t, addr, data);
    case PTRACE_SETREGSET:
        return regset_set(t, addr, data);
    case PTRACE_ARCH_PRCTL: {
        if (addr == ARCH_GET_FS) return syscall_copy_to_user((void *)data, &t->fs_base, 8) < 0 ? -EFAULT : 0;
        if (addr == ARCH_GET_GS) return syscall_copy_to_user((void *)data, &t->gs_base, 8) < 0 ? -EFAULT : 0;
        if (addr == ARCH_SET_FS) { if (data >= USER_TOP) return -EPERM; t->fs_base = data; return 0; }
        if (addr == ARCH_SET_GS) { if (data >= USER_TOP) return -EPERM; t->gs_base = data; return 0; }
        return -EINVAL;
    }
    case PTRACE_SETOPTIONS:
        if (data & ~(uint64_t)PTRACE_O_MASK) return -EINVAL;
        t->pt_opts = (uint32_t)data;
        return 0;
    case PTRACE_GETEVENTMSG:
        return syscall_copy_to_user((void *)data, &t->pt_msg, 8) < 0 ? -EFAULT : 0;
    case PTRACE_GETSIGINFO:
        return syscall_copy_to_user((void *)data, t->pt_siginfo, 128) < 0 ? -EFAULT : 0;
    case PTRACE_SETSIGINFO:
        return syscall_copy_from_user(t->pt_siginfo, (void *)data, 128) < 0 ? -EFAULT : 0;
    case PTRACE_GETSIGMASK:
        if (addr != 8) return -EINVAL;
        return syscall_copy_to_user((void *)data, &t->sig_blocked, 8) < 0 ? -EFAULT : 0;
    case PTRACE_SETSIGMASK: {
        if (addr != 8) return -EINVAL;
        uint64_t m;
        if (syscall_copy_from_user(&m, (void *)data, 8) < 0) return -EFAULT;
        t->sig_blocked = m & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
        return 0;
    }
    case PTRACE_CONT:
    case PTRACE_SYSCALL:
    case PTRACE_SINGLESTEP:
    case PTRACE_LISTEN: {
        if (data >= NSIG) return -EIO;
        uint32_t mode = req == PTRACE_SYSCALL ? PT_SYSCALL : req == PTRACE_SINGLESTEP ? PT_STEP : 0;
        if (ctx) ctx_set_tf(t, ctx, mode == PT_STEP);
        resume(t, (int)data, mode);
        return 0;
    }
    case PTRACE_DETACH: {
        if (data >= NSIG) return -EIO;
        if (ctx) ctx_set_tf(t, ctx, false);
        uint64_t f = spinlock_acquire_irqsave(&g_pt_lock);
        t->pt_resume_sig = (int)data;
        detach_locked(t);
        t->pt_flags &= ~PT_STOPPED;
        spinlock_release_irqrestore(&g_pt_lock, f);
        task_unblock(t);
        return 0;
    }
    default:
        return -EIO;
    }
}

int64_t sys_personality(uint64_t persona) {
    task_t *me = self_task();
    if (!me) return -ESRCH;
    uint32_t old = me->persona;
    if ((uint32_t)persona != 0xFFFFFFFFu) {
        if ((persona & 0xFF) != 0) return -EINVAL;
        me->persona = (uint32_t)persona;
    }
    return old;
}

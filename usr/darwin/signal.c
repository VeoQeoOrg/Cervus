#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/ucontext.h>
#include "darwin.h"

#define DSA_ONSTACK   0x1
#define DSA_RESTART   0x2
#define DSA_RESETHAND 0x4
#define DSA_NOCLDSTOP 0x8
#define DSA_NODEFER   0x10
#define DSA_NOCLDWAIT 0x20
#define DSA_SIGINFO   0x40

#define DSS_ONSTACK 1
#define DSS_DISABLE 4

static const unsigned char d2lx[32] = {
    0, SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, 0, SIGFPE, SIGKILL,
    SIGBUS, SIGSEGV, SIGSYS, SIGPIPE, SIGALRM, SIGTERM, SIGURG, SIGSTOP, SIGTSTP,
    SIGCONT, SIGCHLD, SIGTTIN, SIGTTOU, SIGIO, SIGXCPU, SIGXFSZ, SIGVTALRM,
    SIGPROF, SIGWINCH, SIGPWR, SIGUSR1, SIGUSR2,
};

int d_sig_d2lx(int s)
{
    return s > 0 && s < 32 ? d2lx[s] : 0;
}

int d_sig_lx2d(int s)
{
    for (int i = 1; i < 32; i++)
        if (d2lx[i] == s)
            return i;
    return 0;
}

static void set_d2lx(uint32_t d, sigset_t *out)
{
    sigemptyset(out);
    for (int i = 1; i < 32; i++)
        if ((d & (1u << (i - 1))) && d2lx[i])
            sigaddset(out, d2lx[i]);
}

static uint32_t set_lx2d(const sigset_t *s)
{
    uint32_t d = 0;
    for (int i = 1; i < 32; i++)
        if (d2lx[i] && sigismember(s, d2lx[i]) == 1)
            d |= 1u << (i - 1);
    return d;
}

int d_wstatus(int st)
{
    if (WIFSIGNALED(st))
        return (st & ~0x7f) | (d_sig_lx2d(WTERMSIG(st)) & 0x7f);
    if (WIFSTOPPED(st))
        return (st & 0xff) | (d_sig_lx2d(WSTOPSIG(st)) << 8);
    return st;
}

struct dsigaction {
    void *handler;
    uint32_t mask;
    int flags;
};

struct dsiginfo {
    int signo, err, code;
    int pid;
    uint32_t uid;
    int status;
    void *addr;
    void *value;
    long band;
    unsigned long pad[7];
};

struct dthread_state {
    uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags, cs, fs, gs;
};

struct dmcontext {
    uint16_t trapno, cpu;
    uint32_t err;
    uint64_t faultvaddr;
    struct dthread_state ss;
    uint8_t fs[524];
};

struct dstack {
    void *ss_sp;
    size_t ss_size;
    int ss_flags;
};

struct ducontext {
    int uc_onstack;
    uint32_t uc_sigmask;
    struct dstack uc_stack;
    struct ducontext *uc_link;
    size_t uc_mcsize;
    struct dmcontext *uc_mcontext;
};

static struct dsigaction g_act[32];

static int si_code_lx2d(int sig, int code)
{
    if (code == SI_USER)
        return 0x10001;
    if (code == SI_QUEUE)
        return 0x10002;
    if (code == SI_TIMER)
        return 0x10003;
    if (code == SI_MESGQ)
        return 0x10005;
    if (sig == SIGFPE && code >= 1 && code <= 8) {
        static const int fpe[] = { 0, 7, 8, 1, 2, 3, 4, 5, 6 };
        return fpe[code];
    }
    return code;
}

static void dispatch(int lsig, siginfo_t *si, void *ctx)
{
    int dsig = d_sig_lx2d(lsig);
    struct dsigaction *a = &g_act[dsig];
    void *h = a->handler;
    int e = errno;
    if (!(a->flags & DSA_SIGINFO)) {
        if (h && h != (void *)1)
            ((void (*)(int))h)(dsig);
        errno = e;
        return;
    }
    ucontext_t *uc = ctx;
    struct dsiginfo dsi;
    struct dmcontext mc;
    struct ducontext duc;
    memset(&dsi, 0, sizeof dsi);
    memset(&mc, 0, sizeof mc);
    memset(&duc, 0, sizeof duc);
    dsi.signo = dsig;
    dsi.err = d_errno_lx2d(si->si_errno);
    dsi.code = si_code_lx2d(lsig, si->si_code);
    if (lsig == SIGSEGV || lsig == SIGBUS || lsig == SIGILL || lsig == SIGFPE || lsig == SIGTRAP) {
        dsi.addr = si->si_addr;
    } else {
        dsi.pid = si->si_pid;
        dsi.uid = si->si_uid;
        if (lsig == SIGCHLD) {
            dsi.status = si->si_status;
            if (si->si_code == CLD_KILLED || si->si_code == CLD_DUMPED || si->si_code == CLD_STOPPED)
                dsi.status = d_sig_lx2d(si->si_status);
        } else {
            dsi.value = si->si_ptr;
        }
    }
    greg_t *g = uc->uc_mcontext.gregs;
    mc.ss.rax = (uint64_t)g[REG_RAX];
    mc.ss.rbx = (uint64_t)g[REG_RBX];
    mc.ss.rcx = (uint64_t)g[REG_RCX];
    mc.ss.rdx = (uint64_t)g[REG_RDX];
    mc.ss.rdi = (uint64_t)g[REG_RDI];
    mc.ss.rsi = (uint64_t)g[REG_RSI];
    mc.ss.rbp = (uint64_t)g[REG_RBP];
    mc.ss.rsp = (uint64_t)g[REG_RSP];
    mc.ss.r8 = (uint64_t)g[REG_R8];
    mc.ss.r9 = (uint64_t)g[REG_R9];
    mc.ss.r10 = (uint64_t)g[REG_R10];
    mc.ss.r11 = (uint64_t)g[REG_R11];
    mc.ss.r12 = (uint64_t)g[REG_R12];
    mc.ss.r13 = (uint64_t)g[REG_R13];
    mc.ss.r14 = (uint64_t)g[REG_R14];
    mc.ss.r15 = (uint64_t)g[REG_R15];
    mc.ss.rip = (uint64_t)g[REG_RIP];
    mc.ss.rflags = (uint64_t)g[REG_EFL];
    mc.ss.cs = 0x2b;
    mc.trapno = (uint16_t)g[REG_TRAPNO];
    mc.err = (uint32_t)g[REG_ERR];
    mc.faultvaddr = (uint64_t)g[REG_CR2];
    duc.uc_sigmask = set_lx2d(&uc->uc_sigmask);
    duc.uc_mcsize = sizeof mc;
    duc.uc_mcontext = &mc;
    ((void (*)(int, struct dsiginfo *, struct ducontext *))h)(dsig, &dsi, &duc);
    g[REG_RAX] = (greg_t)mc.ss.rax;
    g[REG_RBX] = (greg_t)mc.ss.rbx;
    g[REG_RCX] = (greg_t)mc.ss.rcx;
    g[REG_RDX] = (greg_t)mc.ss.rdx;
    g[REG_RDI] = (greg_t)mc.ss.rdi;
    g[REG_RSI] = (greg_t)mc.ss.rsi;
    g[REG_RBP] = (greg_t)mc.ss.rbp;
    g[REG_RSP] = (greg_t)mc.ss.rsp;
    g[REG_R8] = (greg_t)mc.ss.r8;
    g[REG_R9] = (greg_t)mc.ss.r9;
    g[REG_R10] = (greg_t)mc.ss.r10;
    g[REG_R11] = (greg_t)mc.ss.r11;
    g[REG_R12] = (greg_t)mc.ss.r12;
    g[REG_R13] = (greg_t)mc.ss.r13;
    g[REG_R14] = (greg_t)mc.ss.r14;
    g[REG_R15] = (greg_t)mc.ss.r15;
    g[REG_RIP] = (greg_t)mc.ss.rip;
    g[REG_EFL] = (greg_t)mc.ss.rflags;
    errno = e;
}

static int d_sigaction(int dsig, const struct dsigaction *act, struct dsigaction *old)
{
    int lsig = d_sig_d2lx(dsig);
    if (!lsig) {
        errno = EINVAL;
        return -1;
    }
    struct sigaction la, lo;
    struct dsigaction prev = g_act[dsig];
    if (sigaction(lsig, NULL, &lo) < 0)
        return -1;
    if (lo.sa_handler == SIG_DFL || lo.sa_handler == SIG_IGN) {
        prev.handler = (void *)lo.sa_handler;
        prev.mask = set_lx2d(&lo.sa_mask);
        prev.flags = 0;
    }
    if (act) {
        memset(&la, 0, sizeof la);
        set_d2lx(act->mask, &la.sa_mask);
        int f = 0;
        if (act->flags & DSA_ONSTACK) f |= SA_ONSTACK;
        if (act->flags & DSA_RESTART) f |= SA_RESTART;
        if (act->flags & DSA_RESETHAND) f |= SA_RESETHAND;
        if (act->flags & DSA_NOCLDSTOP) f |= SA_NOCLDSTOP;
        if (act->flags & DSA_NODEFER) f |= SA_NODEFER;
        if (act->flags & DSA_NOCLDWAIT) f |= SA_NOCLDWAIT;
        if (act->handler == (void *)0 || act->handler == (void *)1) {
            la.sa_handler = act->handler ? SIG_IGN : SIG_DFL;
        } else {
            la.sa_sigaction = dispatch;
            f |= SA_SIGINFO;
        }
        la.sa_flags = f;
        g_act[dsig] = *act;
        if (sigaction(lsig, &la, NULL) < 0) {
            g_act[dsig] = prev;
            return -1;
        }
    }
    if (old)
        *old = prev;
    return 0;
}

static void *d_signal(int dsig, void *h)
{
    struct dsigaction a = { h, 0, DSA_RESTART }, o;
    if (d_sigaction(dsig, &a, &o) < 0)
        return (void *)-1;
    return o.handler;
}

static int d_sigprocmask(int how, const uint32_t *set, uint32_t *old)
{
    int lh = how == 1 ? SIG_BLOCK : how == 2 ? SIG_UNBLOCK : how == 3 ? SIG_SETMASK : -1;
    sigset_t ls, lo;
    if (set && lh < 0) {
        errno = EINVAL;
        return -1;
    }
    if (set)
        set_d2lx(*set, &ls);
    int r = sigprocmask(set ? lh : SIG_BLOCK, set ? &ls : NULL, &lo);
    if (r == 0 && old)
        *old = set_lx2d(&lo);
    return r;
}

static int d_pthread_sigmask(int how, const uint32_t *set, uint32_t *old)
{
    return d_sigprocmask(how, set, old) < 0 ? d_errno_lx2d(errno) : 0;
}

static int d_sigpending(uint32_t *set)
{
    sigset_t s;
    int r = sigpending(&s);
    if (!r)
        *set = set_lx2d(&s);
    return r;
}

static int d_sigsuspend(const uint32_t *set)
{
    sigset_t s;
    set_d2lx(*set, &s);
    return sigsuspend(&s);
}

static int d_sigwait(const uint32_t *set, int *sig)
{
    sigset_t s;
    int ls;
    set_d2lx(*set, &s);
    int r = sigwait(&s, &ls);
    if (r)
        return d_errno_lx2d(r);
    *sig = d_sig_lx2d(ls);
    return 0;
}

static int d_sigemptyset(uint32_t *s) { *s = 0; return 0; }
static int d_sigfillset(uint32_t *s) { *s = ~0u; return 0; }

static int d_sigaddset(uint32_t *s, int n)
{
    if (n <= 0 || n >= 32) {
        errno = EINVAL;
        return -1;
    }
    *s |= 1u << (n - 1);
    return 0;
}

static int d_sigdelset(uint32_t *s, int n)
{
    if (n <= 0 || n >= 32) {
        errno = EINVAL;
        return -1;
    }
    *s &= ~(1u << (n - 1));
    return 0;
}

static int d_sigismember(const uint32_t *s, int n)
{
    if (n <= 0 || n >= 32) {
        errno = EINVAL;
        return -1;
    }
    return (*s >> (n - 1)) & 1;
}

static int d_kill(pid_t pid, int dsig)
{
    int l = dsig ? d_sig_d2lx(dsig) : 0;
    if (dsig && !l) {
        errno = EINVAL;
        return -1;
    }
    return kill(pid, l);
}

static int d_killpg(pid_t pg, int dsig) { return d_kill(-pg, dsig); }

static int d_raise(int dsig)
{
    int l = d_sig_d2lx(dsig);
    if (!l) {
        errno = EINVAL;
        return -1;
    }
    return raise(l);
}

static int d_sigaltstack(const struct dstack *ss, struct dstack *old)
{
    stack_t ls, lo;
    if (ss) {
        ls.ss_sp = ss->ss_sp;
        ls.ss_size = ss->ss_size;
        ls.ss_flags = (ss->ss_flags & DSS_DISABLE) ? SS_DISABLE : 0;
    }
    int r = sigaltstack(ss ? &ls : NULL, &lo);
    if (r == 0 && old) {
        old->ss_sp = lo.ss_sp;
        old->ss_size = lo.ss_size;
        old->ss_flags = ((lo.ss_flags & SS_DISABLE) ? DSS_DISABLE : 0) | ((lo.ss_flags & SS_ONSTACK) ? DSS_ONSTACK : 0);
    }
    return r;
}

static int d_siginterrupt(int dsig, int flag)
{
    struct dsigaction a;
    if (d_sigaction(dsig, NULL, &a) < 0)
        return -1;
    if (flag)
        a.flags &= ~DSA_RESTART;
    else
        a.flags |= DSA_RESTART;
    return d_sigaction(dsig, &a, NULL);
}

static char *d_strsignal(int dsig)
{
    static char buf[32];
    int l = d_sig_d2lx(dsig);
    if (dsig == 7)
        return "EMT trap";
    if (dsig == 29)
        return "Information request";
    if (!l) {
        snprintf(buf, sizeof buf, "Unknown signal: %d", dsig);
        return buf;
    }
    return strsignal(l);
}

static void d_psignal(int dsig, const char *s)
{
    if (s && *s)
        fprintf(stderr, "%s: %s\n", s, d_strsignal(dsig));
    else
        fprintf(stderr, "%s\n", d_strsignal(dsig));
}

static pid_t d_waitpid(pid_t pid, int *st, int opt)
{
    int s;
    pid_t r = waitpid(pid, &s, opt);
    if (r > 0 && st)
        *st = d_wstatus(s);
    return r;
}

static pid_t d_wait(int *st) { return d_waitpid(-1, st, 0); }

static pid_t d_wait4(pid_t pid, int *st, int opt, struct rusage *ru)
{
    int s;
    pid_t r = wait4(pid, &s, opt, ru);
    if (r > 0 && st)
        *st = d_wstatus(s);
    return r;
}

static pid_t d_wait3(int *st, int opt, struct rusage *ru) { return d_wait4(-1, st, opt, ru); }

static int d_system(const char *cmd)
{
    int r = system(cmd);
    return r == -1 || !cmd ? r : d_wstatus(r);
}

struct ditimerval {
    long it_int_sec;
    int it_int_usec;
    long it_val_sec;
    int it_val_usec;
};

static int d_setitimer(int which, const struct ditimerval *v, struct ditimerval *o)
{
    struct itimerval lv, lo;
    lv.it_interval.tv_sec = v->it_int_sec;
    lv.it_interval.tv_usec = v->it_int_usec;
    lv.it_value.tv_sec = v->it_val_sec;
    lv.it_value.tv_usec = v->it_val_usec;
    int r = setitimer(which, &lv, &lo);
    if (r == 0 && o) {
        o->it_int_sec = lo.it_interval.tv_sec;
        o->it_int_usec = (int)lo.it_interval.tv_usec;
        o->it_val_sec = lo.it_value.tv_sec;
        o->it_val_usec = (int)lo.it_value.tv_usec;
    }
    return r;
}

static int d_getitimer(int which, struct ditimerval *o)
{
    struct itimerval lo;
    int r = getitimer(which, &lo);
    if (r == 0) {
        o->it_int_sec = lo.it_interval.tv_sec;
        o->it_int_usec = (int)lo.it_interval.tv_usec;
        o->it_val_sec = lo.it_value.tv_sec;
        o->it_val_usec = (int)lo.it_value.tv_usec;
    }
    return r;
}

DSYM("_sigaction", d_sigaction);
DSYM("_signal", d_signal);
DSYM("_bsd_signal", d_signal);
DSYM("_sigprocmask", d_sigprocmask);
DSYM("_pthread_sigmask", d_pthread_sigmask);
DSYM("_sigpending", d_sigpending);
DSYM("_sigsuspend", d_sigsuspend);
DSYM("_sigwait", d_sigwait);
DSYM("_sigemptyset", d_sigemptyset);
DSYM("_sigfillset", d_sigfillset);
DSYM("_sigaddset", d_sigaddset);
DSYM("_sigdelset", d_sigdelset);
DSYM("_sigismember", d_sigismember);
DSYM("_kill", d_kill);
DSYM("_killpg", d_killpg);
DSYM("_raise", d_raise);
DSYM("_sigaltstack", d_sigaltstack);
DSYM("_siginterrupt", d_siginterrupt);
DSYM("_strsignal", d_strsignal);
DSYM("_psignal", d_psignal);
DSYM("_waitpid", d_waitpid);
DSYM("_wait", d_wait);
DSYM("_wait3", d_wait3);
DSYM("_wait4", d_wait4);
DSYM("_system", d_system);
DSYM("_setitimer", d_setitimer);
DSYM("_getitimer", d_getitimer);

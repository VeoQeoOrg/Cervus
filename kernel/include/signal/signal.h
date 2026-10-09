#ifndef CERVUS_SIGNAL_H
#define CERVUS_SIGNAL_H

#include <stdint.h>
#include <stdbool.h>

struct task;

#define NSIG        64
#define SIGRTMIN_K  32
#define SIGQUEUE_MAX 64

#define SIGHUP      1
#define SIGINT      2
#define SIGQUIT     3
#define SIGILL      4
#define SIGTRAP     5
#define SIGABRT     6
#define SIGBUS      7
#define SIGFPE      8
#define SIGKILL     9
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20
#define SIGTTIN     21
#define SIGTTOU     22
#define SIGURG      23
#define SIGWINCH    28
#define SIGIO       29
#define SIGSTKFLT   16
#define SIGXCPU     24
#define SIGXFSZ     25
#define SIGVTALRM   26
#define SIGPROF     27
#define SIGPWR      30
#define SIGSYS      31

#define SA_SIGINFO   0x00000004
#define SA_ONSTACK   0x08000000
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

#define SIG_DFL_ADDR 0
#define SIG_IGN_ADDR 1

typedef struct {
    uint64_t rip, rsp, rflags, rax, rbx, rbp, r12, r13, r14, r15;
    uint64_t mask;
} sig_ucontext_t;

#define SI_USER_CODE    0
#define SI_QUEUE_CODE   (-1)
#define SI_KERNEL_CODE  0x80
#define CLD_EXITED_CODE 1
#define CLD_KILLED_CODE 2
#define CLD_DUMPED_CODE 3

typedef struct {
    int32_t  code;
    uint32_t pid;
    uint32_t uid;
    int32_t  status;
    uint64_t addr;
    int32_t  trapno;
    int32_t  err;
    uint64_t value;
} ksiginfo_t;

int     signal_post(struct task *t, int sig, const ksiginfo_t *si);
int     signal_post_timer(struct task *t, int sig, const ksiginfo_t *si);
void    signal_flush_timers(struct task *t);
void    signal_send_child(struct task *parent, struct task *child);
int64_t sys_sigqueue(uint64_t pid, uint64_t sig, uint64_t value);
int64_t sys_sigtimedwait(uint64_t set, uint64_t info, uint64_t ts);
void    signal_send(struct task *t, int sig);
void    signal_send_group(uint32_t pgid, int sig);
void    signal_send_subtree(struct task *root, int sig);
bool    signal_pending_deliverable(struct task *t);
int64_t signal_deliver_pending(struct task *t, int64_t retval, uint64_t nr);
void    signal_send_from(struct task *t, int sig, struct task *from);
void    signal_force_fault(struct task *t, int sig, int code, uint64_t addr, int trapno, int err);
struct int_frame_t;
void    signal_irq_return(struct int_frame_t *f);
__attribute__((noreturn)) void signal_kill_exit(struct task *t);
int64_t sys_sigaltstack(uint64_t ss, uint64_t old);

int64_t sys_rt_sigaction(uint64_t sig, uint64_t act, uint64_t oldact, uint64_t sigsetsize);
int64_t sys_rt_sigprocmask(uint64_t how, uint64_t set, uint64_t oldset, uint64_t sigsetsize);
int64_t sys_kill(uint64_t pid, uint64_t sig);
int64_t sys_rt_sigreturn(void);

#endif

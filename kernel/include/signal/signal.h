#ifndef CERVUS_SIGNAL_H
#define CERVUS_SIGNAL_H

#include <stdint.h>
#include <stdbool.h>

struct task;

#define NSIG        32

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

#define SA_SIGINFO   0x00000004
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

#define SIG_DFL_ADDR 0
#define SIG_IGN_ADDR 1

typedef struct {
    uint64_t rip, rsp, rflags, rax, rbx, rbp, r12, r13, r14, r15;
    uint64_t mask;
} sig_ucontext_t;

void    signal_send(struct task *t, int sig);
void    signal_send_group(uint32_t pgid, int sig);
bool    signal_pending_deliverable(struct task *t);
int64_t signal_deliver_pending(struct task *t, int64_t retval);

int64_t sys_rt_sigaction(uint64_t sig, uint64_t act, uint64_t oldact, uint64_t sigsetsize);
int64_t sys_rt_sigprocmask(uint64_t how, uint64_t set, uint64_t oldset, uint64_t sigsetsize);
int64_t sys_kill(uint64_t pid, uint64_t sig);
int64_t sys_rt_sigreturn(void);

#endif

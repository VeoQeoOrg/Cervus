#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef int sig_atomic_t;

#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGIOT     SIGABRT
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGWINCH  28

#define NSIG      32

typedef void (*sighandler_t)(int);

#define SIG_DFL  ((sighandler_t)0)
#define SIG_IGN  ((sighandler_t)1)
#define SIG_ERR  ((sighandler_t)-1)

#define SA_NOCLDSTOP  0x00000001
#define SA_NOCLDWAIT  0x00000002
#define SA_SIGINFO    0x00000004
#define SA_ONSTACK    0x08000000
#define SA_RESTART    0x10000000
#define SA_NODEFER    0x40000000
#define SA_RESETHAND  0x80000000

#define SI_USER    0
#define SI_KERNEL  0x80
#define SI_QUEUE   -1
#define SI_TIMER   -2
#define SI_MESGQ   -3
#define SI_ASYNCIO -4
#define SI_SIGIO   -5
#define SI_TKILL   -6

#define FPE_INTDIV  1
#define FPE_INTOVF  2
#define FPE_FLTDIV  3
#define FPE_FLTOVF  4
#define FPE_FLTUND  5
#define FPE_FLTRES  6
#define FPE_FLTINV  7
#define FPE_FLTSUB  8

#define ILL_ILLOPC  1
#define ILL_ILLOPN  2
#define ILL_ILLADR  3
#define ILL_ILLTRP  4
#define ILL_PRVOPC  5
#define ILL_PRVREG  6
#define ILL_COPROC  7
#define ILL_BADSTK  8

#define SEGV_MAPERR 1
#define SEGV_ACCERR 2

#define BUS_ADRALN  1
#define BUS_ADRERR  2
#define BUS_OBJERR  3

typedef struct {
    unsigned long __bits[2];
} sigset_t;

union sigval {
    int   sival_int;
    void *sival_ptr;
};

typedef struct {
    int          si_signo;
    int          si_errno;
    int          si_code;
    union {
        struct {
            pid_t    si_pid;
            uid_t    si_uid;
        } _kill;
        struct {
            int      si_tid;
            int      si_overrun;
            union sigval si_sigval;
        } _timer;
        struct {
            pid_t    si_pid;
            uid_t    si_uid;
            union sigval si_sigval;
        } _rt;
        struct {
            pid_t    si_pid;
            uid_t    si_uid;
            int      si_status;
        } _sigchld;
        struct {
            void    *si_addr;
        } _sigfault;
        struct {
            long     si_band;
            int      si_fd;
        } _sigpoll;
    } _sifields;
} siginfo_t;

#define si_pid       _sifields._kill.si_pid
#define si_uid       _sifields._kill.si_uid
#define si_status    _sifields._sigchld.si_status
#define si_addr      _sifields._sigfault.si_addr
#define si_band      _sifields._sigpoll.si_band
#define si_fd        _sifields._sigpoll.si_fd

typedef struct {
    void   *ss_sp;
    int     ss_flags;
    size_t  ss_size;
} stack_t;

#define SS_ONSTACK 1
#define SS_DISABLE 2

struct sigaction {
    union {
        sighandler_t  sa_handler;
        void        (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
};

sighandler_t signal(int signum, sighandler_t handler);
int raise(int sig);

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int sig);
int sigdelset(sigset_t *set, int sig);
int sigismember(const sigset_t *set, int sig);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigaltstack(const stack_t *ss, stack_t *oss);

#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2

#endif
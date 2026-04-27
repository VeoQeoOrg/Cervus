#include <signal.h>
#include <string.h>

sighandler_t signal(int signum, sighandler_t handler)
{
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

int raise(int sig)
{
    (void)sig;
    return 0;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact)
{
    (void)sig; (void)act;
    if (oldact) {
        oldact->sa_handler = SIG_DFL;
        memset(&oldact->sa_mask, 0, sizeof(oldact->sa_mask));
        oldact->sa_flags = 0;
    }
    return 0;
}

int sigemptyset(sigset_t *set)                { if (set) memset(set, 0, sizeof(*set)); return 0; }
int sigfillset(sigset_t *set)                 { if (set) memset(set, 0xFF, sizeof(*set)); return 0; }
int sigaddset(sigset_t *set, int sig)         { (void)set; (void)sig; return 0; }
int sigdelset(sigset_t *set, int sig)         { (void)set; (void)sig; return 0; }
int sigismember(const sigset_t *set, int sig) { (void)set; (void)sig; return 0; }

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how; (void)set;
    if (oldset) memset(oldset, 0, sizeof(*oldset));
    return 0;
}
#include <signal.h>
#include <errno.h>
#include <libcervus.h>

static int mask_one(int how, int sig)
{
    sigset_t s;
    sigemptyset(&s);
    if (sigaddset(&s, sig) < 0) return -1;
    return sigprocmask(how, &s, NULL);
}

int sighold(int sig)  { return mask_one(SIG_BLOCK, sig); }
int sigrelse(int sig) { return mask_one(SIG_UNBLOCK, sig); }

int sigignore(int sig)
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    return sigaction(sig, &sa, NULL);
}

int sigpause(int sig)
{
    sigset_t s;
    sigprocmask(SIG_BLOCK, NULL, &s);
    if (sigdelset(&s, sig) < 0) return -1;
    return sigsuspend(&s);
}

void (*sigset(int sig, void (*disp)(int)))(int)
{
    sigset_t mask, old;
    struct sigaction sa, osa;
    sigemptyset(&mask);
    if (sigaddset(&mask, sig) < 0) return SIG_ERR;
    if (disp == SIG_HOLD) {
        if (sigaction(sig, NULL, &osa) < 0) return SIG_ERR;
        if (sigprocmask(SIG_BLOCK, &mask, &old) < 0) return SIG_ERR;
    } else {
        sa.sa_handler = disp;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        if (sigaction(sig, &sa, &osa) < 0) return SIG_ERR;
        if (sigprocmask(SIG_UNBLOCK, &mask, &old) < 0) return SIG_ERR;
    }
    return sigismember(&old, sig) ? SIG_HOLD : osa.sa_handler;
}

int sigisemptyset(const sigset_t *set)
{
    for (unsigned i = 0; i < sizeof set->__bits / sizeof set->__bits[0]; i++)
        if (set->__bits[i]) return 0;
    return 1;
}

int sigandset(sigset_t *dest, const sigset_t *a, const sigset_t *b)
{
    for (unsigned i = 0; i < sizeof dest->__bits / sizeof dest->__bits[0]; i++)
        dest->__bits[i] = a->__bits[i] & b->__bits[i];
    return 0;
}

int sigorset(sigset_t *dest, const sigset_t *a, const sigset_t *b)
{
    for (unsigned i = 0; i < sizeof dest->__bits / sizeof dest->__bits[0]; i++)
        dest->__bits[i] = a->__bits[i] | b->__bits[i];
    return 0;
}

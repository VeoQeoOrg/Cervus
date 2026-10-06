#include <signal.h>
#include <string.h>
#include <errno.h>

sighandler_t signal(int signum, sighandler_t handler)
{
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(signum, &sa, &old) < 0) return SIG_ERR;
    return old.sa_handler;
}

int siginterrupt(int sig, int flag)
{
    struct sigaction sa;
    if (sigaction(sig, NULL, &sa) < 0) return -1;
    if (flag) sa.sa_flags &= ~SA_RESTART;
    else sa.sa_flags |= SA_RESTART;
    return sigaction(sig, &sa, NULL);
}

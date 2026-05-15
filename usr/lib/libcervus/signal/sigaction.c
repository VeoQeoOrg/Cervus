#include <signal.h>
#include <string.h>

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

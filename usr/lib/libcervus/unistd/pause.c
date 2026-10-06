#include <unistd.h>
#include <signal.h>

int pause(void)
{
    sigset_t cur;
    sigprocmask(SIG_BLOCK, NULL, &cur);
    return sigsuspend(&cur);
}

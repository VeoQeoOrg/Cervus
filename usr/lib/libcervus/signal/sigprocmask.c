#include <signal.h>
#include <string.h>

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how; (void)set;
    if (oldset) memset(oldset, 0, sizeof(*oldset));
    return 0;
}

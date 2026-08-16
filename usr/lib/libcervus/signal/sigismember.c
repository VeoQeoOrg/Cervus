#include <signal.h>
#include <errno.h>

int sigismember(const sigset_t *set, int sig) {
    if (!set || sig <= 0 || sig >= 64) { errno = EINVAL; return -1; }
    return (int)((set->__bits[0] >> sig) & 1UL);
}

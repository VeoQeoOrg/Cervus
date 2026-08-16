#include <signal.h>
#include <errno.h>

int sigaddset(sigset_t *set, int sig) {
    if (!set || sig <= 0 || sig >= 64) { errno = EINVAL; return -1; }
    set->__bits[0] |= (1UL << sig);
    return 0;
}

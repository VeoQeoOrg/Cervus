#include <signal.h>
#include <errno.h>
#include <libcervus.h>

int sigaddset(sigset_t *set, int sig) {
    if (!set || sig <= 0 || sig >= 64) { __cervus_set_errno(EINVAL); return -1; }
    set->__bits[0] |= (1UL << sig);
    return 0;
}

#include <signal.h>
#include <errno.h>
#include <libcervus.h>

int sigismember(const sigset_t *set, int sig) {
    if (!set || sig <= 0 || sig >= 64) { __cervus_set_errno(EINVAL); return -1; }
    return (int)((set->__bits[0] >> sig) & 1UL);
}

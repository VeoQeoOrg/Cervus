#include <unistd.h>
#include <errno.h>
#include <sys/resource.h>
#include <libcervus.h>

int nice(int inc)
{
    __cervus_set_errno(0);
    int cur = getpriority(PRIO_PROCESS, 0);
    if (cur == -1 && errno) return -1;
    int want = cur + inc;
    if (want < -20) want = -20;
    if (want > 19) want = 19;
    if (setpriority(PRIO_PROCESS, 0, want) < 0) {
        if (errno == EACCES) __cervus_set_errno(EPERM);
        return -1;
    }
    __cervus_set_errno(0);
    return getpriority(PRIO_PROCESS, 0);
}

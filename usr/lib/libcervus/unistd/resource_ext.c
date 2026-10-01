#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>

static int g_nice;

int getpriority(int which, id_t who)
{
    if (which != PRIO_PROCESS && which != PRIO_PGRP && which != PRIO_USER) { errno = EINVAL; return -1; }
    (void)who;
    errno = 0;
    return g_nice;
}

int setpriority(int which, id_t who, int prio)
{
    if (which != PRIO_PROCESS && which != PRIO_PGRP && which != PRIO_USER) { errno = EINVAL; return -1; }
    if (who != 0 && which == PRIO_PROCESS && who != (id_t)getpid()) { errno = ESRCH; return -1; }
    if (prio < -20) prio = -20;
    if (prio > 19) prio = 19;
    if (prio < g_nice && getuid() != 0) { errno = EACCES; return -1; }
    g_nice = prio;
    return 0;
}

static rlim_t limit_of(int resource)
{
    switch (resource) {
    case RLIMIT_NOFILE: return 512;
    case RLIMIT_STACK:  return 8UL * 1024 * 1024;
    case RLIMIT_CORE:   return 0;
    default:            return RLIM_INFINITY;
    }
}

int getrlimit(int resource, struct rlimit *rl)
{
    if (resource < 0 || resource >= RLIMIT_NLIMITS || !rl) { errno = EINVAL; return -1; }
    rl->rlim_cur = rl->rlim_max = limit_of(resource);
    return 0;
}

int setrlimit(int resource, const struct rlimit *rl)
{
    if (resource < 0 || resource >= RLIMIT_NLIMITS || !rl || rl->rlim_cur > rl->rlim_max) { errno = EINVAL; return -1; }
    rlim_t max = limit_of(resource);
    if (max != RLIM_INFINITY && rl->rlim_max > max) { errno = EPERM; return -1; }
    return 0;
}

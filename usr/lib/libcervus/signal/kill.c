#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);

int kill(pid_t pid, int sig)
{
    return (int)__cervus_sys_ret((long)syscall2(SYS_KILL, (long)pid, sig));
}

int killpg(pid_t pgrp, int sig)
{
    if (pgrp <= 1) { errno = EINVAL; return -1; }
    return kill(-pgrp, sig);
}

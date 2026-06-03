#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

pid_t getpgid(pid_t pid)
{
    return (pid_t)__cervus_sys_ret(syscall1(SYS_GETPGID, pid));
}

int setpgid(pid_t pid, pid_t pgid)
{
    return (int)__cervus_sys_ret(syscall2(SYS_SETPGID, pid, pgid));
}

pid_t getpgrp(void)
{
    return getpgid(0);
}

pid_t getsid(pid_t pid)
{
    return (pid_t)__cervus_sys_ret(syscall1(SYS_GETSID, pid));
}

pid_t setsid(void)
{
    return (pid_t)__cervus_sys_ret(syscall0(SYS_SETSID));
}

#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int getpriority(int which, id_t who)
{
    long r = syscall2(SYS_GETPRIORITY, (uint64_t)which, (uint64_t)who);
    if (r < 0) { errno = (int)-r; return -1; }
    errno = 0;
    return 20 - (int)r;
}

int setpriority(int which, id_t who, int prio)
{
    return (int)__cervus_sys_ret(syscall3(SYS_SETPRIORITY, (uint64_t)which, (uint64_t)who, (uint64_t)(int64_t)prio));
}

int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit)
{
    return (int)__cervus_sys_ret(syscall4(SYS_PRLIMIT, (uint64_t)pid, (uint64_t)resource,
                                          (uint64_t)new_limit, (uint64_t)old_limit));
}

int getrlimit(int resource, struct rlimit *rl)
{
    return prlimit(0, resource, NULL, rl);
}

int setrlimit(int resource, const struct rlimit *rl)
{
    return prlimit(0, resource, rl, NULL);
}



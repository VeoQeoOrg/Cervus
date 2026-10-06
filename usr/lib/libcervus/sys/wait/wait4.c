#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <libcervus.h>

pid_t wait4(pid_t pid, int *status, int options, struct rusage *ru)
{
    int cp = __cervus_cp_enter();
    pid_t r = (pid_t)__cervus_sys_ret(syscall4(SYS_WAIT4, (uint64_t)(int64_t)pid, (uint64_t)status,
                                               (uint64_t)options, (uint64_t)ru));
    __cervus_cp_leave(cp);
    return r;
}

pid_t wait3(int *status, int options, struct rusage *ru)
{
    return wait4(-1, status, options, ru);
}

int waitid(idtype_t idtype, id_t id, siginfo_t *info, int options)
{
    int cp = __cervus_cp_enter();
    int r = (int)__cervus_sys_ret(syscall4(SYS_WAITID, (uint64_t)idtype, (uint64_t)id,
                                           (uint64_t)info, (uint64_t)options));
    __cervus_cp_leave(cp);
    return r;
}

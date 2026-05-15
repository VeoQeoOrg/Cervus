#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int execve(const char *path, char *const argv[], char *const envp[])
{
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__cervus_sys_ret(syscall3(SYS_EXECVE, path, argv, envp));
}

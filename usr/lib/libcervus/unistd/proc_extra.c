#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <libcervus.h>

pid_t vfork(void)
{
    return fork();
}

int fexecve(int fd, char *const argv[], char *const envp[])
{
    long r = syscall5(SYS_EXECVEAT, fd, (long)"", (long)argv, (long)envp, 0x1000);
    return (int)__cervus_sys_ret(r);
}

int getdtablesize(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < INT_MAX)
        return (int)rl.rlim_cur;
    return INT_MAX;
}

int daemon(int nochdir, int noclose)
{
    pid_t p = fork();
    if (p < 0) return -1;
    if (p > 0) _exit(0);
    if (setsid() < 0) return -1;
    if (!nochdir && chdir("/")) return -1;
    if (!noclose) {
        int fd = open("/dev/null", O_RDWR);
        if (fd < 0) return -1;
        int failed = dup2(fd, 0) < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0;
        if (fd > 2) close(fd);
        if (failed) return -1;
    }
    return 0;
}

char *get_current_dir_name(void)
{
    struct stat a, b;
    char *pwd = getenv("PWD");
    if (pwd && *pwd && stat(pwd, &a) == 0 && stat(".", &b) == 0 &&
        a.st_dev == b.st_dev && a.st_ino == b.st_ino)
        return strdup(pwd);
    return getcwd(NULL, 0);
}

pid_t setpgrp(void)
{
    return setpgid(0, 0);
}

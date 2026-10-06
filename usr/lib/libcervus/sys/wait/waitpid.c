#include <sys/wait.h>
#include <sys/syscall.h>
#include <libcervus.h>

pid_t waitpid(pid_t p, int *s, int f) { int __cp = __cervus_cp_enter();
    pid_t __r = (pid_t)__cervus_sys_ret(syscall3(SYS_WAIT, p, s, f));
    __cervus_cp_leave(__cp);
    return __r;
}

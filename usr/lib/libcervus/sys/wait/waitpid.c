#include <sys/wait.h>
#include <sys/syscall.h>
#include <libcervus.h>

pid_t waitpid(pid_t p, int *s, int f) { return (pid_t)__cervus_sys_ret(syscall3(SYS_WAIT, p, s, f)); }

#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_task_kill(pid_t p) { return (int)__cervus_sys_ret(syscall1(SYS_TASK_KILL, p)); }

#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

pid_t fork(void) { return (pid_t)__cervus_sys_ret(syscall0(SYS_FORK)); }

#include <signal.h>
#include <sys/syscall.h>
#include <libcervus.h>

int sigaltstack(const stack_t *ss, stack_t *old_ss)
{
    return (int)__cervus_sys_ret(syscall2(SYS_SIGALTSTACK, ss, old_ss));
}

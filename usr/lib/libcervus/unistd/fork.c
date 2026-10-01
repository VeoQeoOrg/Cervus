#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

void __cervus_atfork_prepare(void);
void __cervus_atfork_parent(void);
void __cervus_atfork_child(void);

pid_t fork(void)
{
    __cervus_atfork_prepare();
    pid_t r = (pid_t)__cervus_sys_ret(syscall0(SYS_FORK));
    if (r == 0) __cervus_atfork_child();
    else __cervus_atfork_parent();
    return r;
}

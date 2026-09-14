#include <sys/time.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);

int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value)
{
    return (int)__cervus_sys_ret(
        (long)syscall3(SYS_SETITIMER, (unsigned long)which,
                       (unsigned long)new_value, (unsigned long)old_value));
}

int getitimer(int which, struct itimerval *curr_value)
{
    return (int)__cervus_sys_ret(
        (long)syscall2(SYS_GETITIMER, (unsigned long)which,
                       (unsigned long)curr_value));
}

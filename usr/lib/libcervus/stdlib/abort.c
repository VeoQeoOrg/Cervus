#include <stdlib.h>
#include <signal.h>
#include <sys/syscall.h>

void abort(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGABRT);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    raise(SIGABRT);

    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGABRT, &sa, NULL);
    raise(SIGABRT);

    syscall1(SYS_EXIT, 127);
    __builtin_unreachable();
}

#include <setjmp.h>
#include <signal.h>

__attribute__((visibility("hidden"))) int __cervus_sigsetjmp_tail(sigjmp_buf jb, int ret)
{
    sigset_t *ss = (sigset_t *)&jb[10];
    if (ret) sigprocmask(SIG_SETMASK, ss, NULL);
    else sigprocmask(SIG_SETMASK, NULL, ss);
    return ret;
}

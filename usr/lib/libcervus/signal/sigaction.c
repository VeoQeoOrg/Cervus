#include <signal.h>
#include <string.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);
void __sigtramp(void);

struct k_sigaction {
    unsigned long handler;
    unsigned long flags;
    unsigned long restorer;
    unsigned long mask;
};

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact)
{
    struct k_sigaction ka, ko;
    struct k_sigaction *kap = 0, *kop = 0;

    if (act) {
        ka.handler  = (unsigned long)act->sa_handler;
        ka.flags    = (unsigned long)(unsigned int)act->sa_flags;
        ka.restorer = (unsigned long)__sigtramp;
        ka.mask     = act->sa_mask.__bits[0];
        kap = &ka;
    }
    if (oldact) kop = &ko;

    long r = (long)syscall4(SYS_RT_SIGACTION, sig, (unsigned long)kap, (unsigned long)kop, 8);

    if (r == 0 && oldact) {
        oldact->sa_handler = (sighandler_t)ko.handler;
        oldact->sa_flags   = (int)ko.flags;
        oldact->sa_restorer = (void (*)(void))ko.restorer;
        memset(&oldact->sa_mask, 0, sizeof(oldact->sa_mask));
        oldact->sa_mask.__bits[0] = ko.mask;
    }
    return (int)__cervus_sys_ret(r);
}

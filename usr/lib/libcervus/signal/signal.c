#include <signal.h>

sighandler_t signal(int signum, sighandler_t handler)
{
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

#include <signal.h>
#include <unistd.h>

int raise(int sig)
{
    return tgkill(getpid(), gettid(), sig);
}

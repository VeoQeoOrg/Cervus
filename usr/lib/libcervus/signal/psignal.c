#include <signal.h>
#include <stdio.h>
#include <string.h>

void psignal(int sig, const char *msg)
{
    if (msg && *msg) fprintf(stderr, "%s: %s\n", msg, strsignal(sig));
    else fprintf(stderr, "%s\n", strsignal(sig));
}

void psiginfo(const siginfo_t *info, const char *msg)
{
    psignal(info ? info->si_signo : 0, msg);
}

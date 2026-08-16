#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t got_usr1 = 0;
static volatile sig_atomic_t got_int  = 0;
static volatile sig_atomic_t got_winch = 0;

static void h_usr1(int s)  { (void)s; got_usr1 = 1; }
static void h_int(int s)   { (void)s; got_int  = 1; }
static void h_winch(int s) { (void)s; got_winch = 1; }

int main(void)
{
    signal(SIGUSR1, h_usr1);
    signal(SIGINT,  h_int);
    signal(SIGWINCH, h_winch);

    printf("sigtest: pid=%d\n", getpid());

    printf("raise(SIGUSR1) -> handler should run synchronously...\n");
    raise(SIGUSR1);
    printf("  got_usr1 = %d  %s\n", got_usr1, got_usr1 ? "OK" : "FAILED");

    printf("\nNow: press Ctrl-C (caught, NOT killed) or change the console font\n");
    printf("(SIGWINCH). After 3 Ctrl-C it exits. Type 'q'<Enter> to quit early.\n\n");

    int cnt = 0;
    char b;
    while (cnt < 3) {
        ssize_t n = read(0, &b, 1);
        if (got_int)   { got_int = 0;  cnt++; printf("SIGINT caught (#%d), still alive\n", cnt); }
        if (got_winch) { got_winch = 0; printf("SIGWINCH: terminal resized\n"); }
        if (n == 1 && (b == 'q' || b == 'Q')) break;
    }

    printf("sigtest done (exited normally = signals are catchable)\n");
    return 0;
}

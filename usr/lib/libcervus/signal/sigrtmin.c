#include <signal.h>

int __libc_current_sigrtmin(void) { return 34; }
int __libc_current_sigrtmax(void) { return NSIG - 1; }

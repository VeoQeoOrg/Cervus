#include <signal.h>
#include <string.h>

int sigfillset(sigset_t *set) { if (set) memset(set, 0xFF, sizeof(*set)); return 0; }

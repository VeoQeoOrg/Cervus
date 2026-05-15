#include <sys/wait.h>

pid_t wait(int *s) { return waitpid(-1, s, 0); }

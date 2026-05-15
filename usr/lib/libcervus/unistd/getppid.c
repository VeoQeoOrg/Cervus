#include <unistd.h>
#include <sys/syscall.h>

pid_t getppid(void) { return (pid_t)syscall0(SYS_GETPPID); }

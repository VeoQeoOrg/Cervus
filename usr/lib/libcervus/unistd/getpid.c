#include <unistd.h>
#include <sys/syscall.h>

pid_t getpid(void)  { return (pid_t)syscall0(SYS_GETPID); }
pid_t gettid(void)  { return (pid_t)syscall0(SYS_GETTID); }

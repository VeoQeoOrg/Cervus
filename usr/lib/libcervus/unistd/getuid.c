#include <unistd.h>
#include <sys/syscall.h>

uid_t getuid(void) { return (uid_t)syscall0(SYS_GETUID); }

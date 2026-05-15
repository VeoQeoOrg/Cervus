#include <unistd.h>
#include <sys/syscall.h>

gid_t getgid(void) { return (gid_t)syscall0(SYS_GETGID); }

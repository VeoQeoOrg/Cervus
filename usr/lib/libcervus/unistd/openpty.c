#include <pty.h>
#include <sys/syscall.h>
#include <libcervus.h>

int openpty(int *master, int *slave) {
    int fds[2];
    long r = __cervus_sys_ret(syscall1(SYS_OPENPTY, (uint64_t)(uintptr_t)fds));
    if (r < 0) return -1;
    if (master) *master = fds[0];
    if (slave) *slave = fds[1];
    return 0;
}

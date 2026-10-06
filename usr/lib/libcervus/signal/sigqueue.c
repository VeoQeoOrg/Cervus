#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <libcervus.h>

int sigqueue(pid_t pid, int sig, const union sigval value)
{
    uint64_t v = 0;
    memcpy(&v, &value, sizeof(value) < sizeof(v) ? sizeof(value) : sizeof(v));
    return (int)__cervus_sys_ret(syscall3(SYS_SIGQUEUE, (uint64_t)(int64_t)pid, (uint64_t)sig, v));
}

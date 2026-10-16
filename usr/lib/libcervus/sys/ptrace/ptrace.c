#include <sys/ptrace.h>
#include <sys/personality.h>
#include <sys/syscall.h>
#include <stdarg.h>
#include <libcervus.h>

long ptrace(int request, ...)
{
    va_list ap;
    va_start(ap, request);
    pid_t pid = va_arg(ap, pid_t);
    void *addr = va_arg(ap, void *);
    void *data = va_arg(ap, void *);
    va_end(ap);
    long result = 0;
    int peek = request == PTRACE_PEEKTEXT || request == PTRACE_PEEKDATA || request == PTRACE_PEEKUSER;
    if (peek) data = &result;
    long r = (long)__cervus_sys_ret(__syscall6(SYS_PTRACE, (uint64_t)request, (uint64_t)pid,
                                               (uint64_t)addr, (uint64_t)data, 0, 0));
    if (r < 0 || !peek) return r;
    return result;
}

int personality(unsigned long persona)
{
    return (int)__cervus_sys_ret(__syscall6(SYS_PERSONALITY, persona, 0, 0, 0, 0, 0));
}

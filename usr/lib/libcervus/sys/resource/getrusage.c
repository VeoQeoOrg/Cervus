#include <sys/resource.h>
#include <sys/syscall.h>
#include <libcervus.h>

int getrusage(int who, struct rusage *usage)
{
    return (int)__cervus_sys_ret(syscall2(SYS_GETRUSAGE, (uint64_t)(int64_t)who, (uint64_t)usage));
}

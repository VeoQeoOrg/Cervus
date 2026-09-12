#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>

int utimes_at(const char *path, int64_t atime, int64_t mtime)
{
    return (int)__cervus_sys_ret(syscall3(SYS_UTIMES, path,
                                          (uint64_t)atime, (uint64_t)mtime));
}

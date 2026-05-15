#include <unistd.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <libcervus.h>

off_t lseek(int fd, off_t off, int whence)
{
    return (off_t)__cervus_sys_ret(syscall3(SYS_SEEK, fd, (uint64_t)off, whence));
}

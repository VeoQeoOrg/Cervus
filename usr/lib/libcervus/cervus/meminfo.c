#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_meminfo(cervus_meminfo_t *m) { return (int)__cervus_sys_ret(syscall1(SYS_MEMINFO, m)); }

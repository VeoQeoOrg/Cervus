#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_clock_gettime(int id, cervus_timespec_t *t) { return (int)__cervus_sys_ret(syscall2(SYS_CLOCK_GET, id, t)); }

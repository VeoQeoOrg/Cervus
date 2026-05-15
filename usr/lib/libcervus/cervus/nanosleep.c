#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_nanosleep(uint64_t ns) { return (int)__cervus_sys_ret(syscall1(SYS_SLEEP_NS, ns)); }

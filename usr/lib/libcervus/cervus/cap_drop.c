#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_cap_drop(uint64_t m) { return (int)__cervus_sys_ret(syscall1(SYS_CAP_DROP, m)); }

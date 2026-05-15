#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_shutdown(void) { return (int)__cervus_sys_ret(syscall0(SYS_SHUTDOWN)); }

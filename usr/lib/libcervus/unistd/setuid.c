#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int setuid(uid_t u) { return (int)__cervus_sys_ret(syscall1(SYS_SETUID, u)); }

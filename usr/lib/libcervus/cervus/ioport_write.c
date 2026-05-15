#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_ioport_write(uint16_t p, int w, uint32_t v) { return (int)__cervus_sys_ret(syscall3(SYS_IOPORT_WRITE, p, w, v)); }

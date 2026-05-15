#include <sys/cervus.h>
#include <sys/syscall.h>

uint32_t cervus_ioport_read(uint16_t p, int w) { return (uint32_t)syscall2(SYS_IOPORT_READ, p, w); }

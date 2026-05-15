#include <sys/cervus.h>
#include <sys/syscall.h>

ssize_t cervus_dbg_print(const char *b, size_t n) { return (ssize_t)syscall2(SYS_DBG_PRINT, b, n); }

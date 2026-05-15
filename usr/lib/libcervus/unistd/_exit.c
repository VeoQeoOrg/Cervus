#include <unistd.h>
#include <sys/syscall.h>

void _exit(int status) { syscall1(SYS_EXIT, status); __builtin_unreachable(); }

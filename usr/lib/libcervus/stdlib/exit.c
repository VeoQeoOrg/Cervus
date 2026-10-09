#include <stdlib.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <libcervus.h>

void __stdio_exit(void) __attribute__((weak));

void exit(int status)
{
    __cervus_run_exit_fns(0);
    if (__stdio_exit) __stdio_exit();
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

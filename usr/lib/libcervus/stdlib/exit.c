#include <stdlib.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <libcervus.h>

void exit(int status)
{
    __cervus_run_exit_fns(0);
    __cervus_flush_all();
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

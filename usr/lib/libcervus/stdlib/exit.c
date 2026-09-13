#include <stdlib.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <libcervus.h>

extern void (*__cervus_atexit_fns[])(void);
extern int   __cervus_atexit_cnt;

void exit(int status)
{
    while (__cervus_atexit_cnt > 0) {
        __cervus_atexit_cnt--;
        if (__cervus_atexit_fns[__cervus_atexit_cnt]) __cervus_atexit_fns[__cervus_atexit_cnt]();
    }
    __cervus_flush_all();
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

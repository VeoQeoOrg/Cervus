#include <stdlib.h>

extern void (*__cervus_atexit_fns[])(void);
extern int   __cervus_atexit_cnt;
extern int   __cervus_atexit_max;

int atexit(void (*fn)(void))
{
    if (__cervus_atexit_cnt >= __cervus_atexit_max) return -1;
    __cervus_atexit_fns[__cervus_atexit_cnt++] = fn;
    return 0;
}

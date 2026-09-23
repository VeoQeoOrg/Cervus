#include <stdlib.h>
#include <libcervus.h>

int atexit(void (*fn)(void))
{
    return __cervus_exit_push((void (*)(void *))fn, 0, 0, 1);
}

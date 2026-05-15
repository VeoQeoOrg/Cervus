#include <stdlib.h>
#include <errno.h>
#include <libcervus.h>

int system(const char *cmd)
{
    (void)cmd;
    __cervus_errno = ENOSYS;
    return -1;
}

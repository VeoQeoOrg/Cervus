#include <stdio.h>
#include <sys/syscall.h>
#include <libcervus.h>

void rewind(FILE *f)
{
    if (f) {
        syscall3(SYS_SEEK, f->fd, 0, 0);
        f->eof = 0;
        f->err = 0;
    }
}

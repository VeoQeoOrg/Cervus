#include <stdio.h>
#include <unistd.h>
#include <libcervus.h>

void rewind(FILE *f)
{
    if (!f) return;
    __cervus_fflush(f);
    f->unget = 0;
    lseek(f->fd, 0, SEEK_SET);
    f->eof = 0;
    f->err = 0;
}

#include <stdio.h>
#include <unistd.h>
#include <libcervus.h>

int fseek(FILE *s, long off, int whence)
{
    if (!s) return -1;
    off_t r = lseek(s->fd, (off_t)off, whence);
    if (r == (off_t)-1) { s->err = 1; return -1; }
    s->eof = 0;
    return 0;
}

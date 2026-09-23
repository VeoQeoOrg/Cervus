#include <stdio.h>
#include <unistd.h>
#include <libcervus.h>

int fseek(FILE *s, long off, int whence)
{
    if (!s) return -1;

    if (whence == SEEK_CUR && s->unget) off -= 1;

    if (__cervus_fflush(s) == EOF) return -1;
    s->unget = 0;

    off_t r = __cervus_io_seek(s, (off_t)off, whence);
    if (r == (off_t)-1) { s->err = 1; return -1; }
    s->eof = 0;
    return 0;
}

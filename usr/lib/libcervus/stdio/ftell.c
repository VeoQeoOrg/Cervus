#include <stdio.h>
#include <unistd.h>
#include <libcervus.h>

long ftell(FILE *s)
{
    if (!s) return -1;
    off_t pos = __cervus_io_seek(s, 0, SEEK_CUR);
    if (pos == (off_t)-1) return -1;

    if (s->dir == __CDIR_READ) pos -= (off_t)(s->buf_len - s->buf_pos);
    else if (s->dir == __CDIR_WRITE) pos += (off_t)s->buf_pos;

    if (s->unget) pos -= 1;
    return (long)pos;
}

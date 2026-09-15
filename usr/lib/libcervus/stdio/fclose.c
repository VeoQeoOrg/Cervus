#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libcervus.h>

extern int __cervus_is_memstream(FILE *f);
extern int __cervus_memstream_close(FILE *f);

int fclose(FILE *s)
{
    if (!s) return EOF;
    if (__cervus_is_memstream(s)) return __cervus_memstream_close(s);
    int rc = __cervus_fflush(s);
    int fd = s->fd;
    int owned = s->flags & __CF_OWNED;
    __cervus_stream_forget(s);
    free(s->buf);
    s->buf = NULL;
    s->buf_size = 0;
    close(fd);
    if (owned) free(s);
    return rc;
}

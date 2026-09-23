#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libcervus.h>

int fclose(FILE *s)
{
    if (!s) return EOF;
    int rc = __cervus_fflush(s);
    int owned = s->flags & __CF_OWNED;
    __cervus_stream_forget(s);
    free(s->buf);
    s->buf = NULL;
    s->buf_size = 0;
    if (__cervus_io_close(s) < 0 && rc == 0) rc = EOF;
    s->has_io = 0;
    if (owned) free(s);
    return rc;
}

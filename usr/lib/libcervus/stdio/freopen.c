#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <libcervus.h>

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    if (!stream) return NULL;

    __cervus_fflush(stream);

    if (!path) return stream;

    FILE *fresh = fopen(path, mode);
    if (!fresh) return NULL;

    if (stream->fd >= 0 && fresh->fd != stream->fd) {
        if (dup2(fresh->fd, stream->fd) < 0) {
            fclose(fresh);
            return NULL;
        }
        close(fresh->fd);
        fresh->fd = stream->fd;
    }

    stream->fd  = fresh->fd;
    stream->eof = 0;
    stream->err = 0;
    stream->buf_pos = 0;
    stream->unget = -1;

    free(fresh);
    return stream;
}

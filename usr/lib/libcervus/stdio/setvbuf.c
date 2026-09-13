#include <stdio.h>
#include <stdlib.h>
#include <libcervus.h>

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
    (void)buf;
    if (!stream) return -1;

    __cervus_fflush(stream);

    switch (mode) {
        case _IONBF: stream->bufmode = __CBUF_NONE; break;
        case _IOLBF: stream->bufmode = __CBUF_LINE; break;
        case _IOFBF: stream->bufmode = __CBUF_FULL; break;
        default: return -1;
    }

    if (stream->bufmode == __CBUF_NONE) {
        free(stream->buf);
        stream->buf = NULL;
        stream->buf_size = 0;
        stream->buf_pos = 0;
        stream->buf_len = 0;
        return 0;
    }

    if (size >= 64 && size != stream->buf_size) {
        char *nb = (char *)malloc(size);
        if (nb) {
            free(stream->buf);
            stream->buf = nb;
            stream->buf_size = size;
            stream->buf_pos = 0;
            stream->buf_len = 0;
            __cervus_stream_register(stream);
        }
    }
    return 0;
}

void setbuf(FILE *stream, char *buf)
{
    setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

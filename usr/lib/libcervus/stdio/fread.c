#include <stdio.h>
#include <unistd.h>
#include <libcervus.h>
#include <string.h>

size_t fread(void *buf, size_t size, size_t nmemb, FILE *s)
{
    if (!s || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t got = 0;
    unsigned char *out = (unsigned char *)buf;

    if (s->unget) {
        out[got++] = (unsigned char)(s->unget - 1);
        s->unget = 0;
    }

    if (s->dir == __CDIR_WRITE) __cervus_fflush(s);
    __cervus_setup_buf(s);

    if (s->bufmode == __CBUF_NONE) {
        while (got < total) {
            ssize_t r = __cervus_io_read(s, out + got, total - got);
            if (r < 0) { s->err = 1; break; }
            if (r == 0) { s->eof = 1; break; }
            got += (size_t)r;
        }
        return got / size;
    }

    while (got < total) {
        size_t avail = (s->dir == __CDIR_READ) ? (s->buf_len - s->buf_pos) : 0;

        if (avail == 0) {
            size_t want = total - got;
            if (want >= __CERVUS_STDIO_BUFSZ) {
                ssize_t r = __cervus_io_read(s, out + got, want);
                if (r < 0) { s->err = 1; break; }
                if (r == 0) { s->eof = 1; break; }
                got += (size_t)r;
                continue;
            }
            if (__cervus_fill(s) <= 0) break;
            avail = s->buf_len - s->buf_pos;
            if (avail == 0) break;
        }

        size_t take = total - got;
        if (take > avail) take = avail;
        memcpy(out + got, s->buf + s->buf_pos, take);
        s->buf_pos += take;
        got += take;
    }

    return got / size;
}

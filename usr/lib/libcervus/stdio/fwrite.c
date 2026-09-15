#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libcervus.h>

static size_t raw_write(int fd, const char *p, size_t total)
{
    size_t sent = 0;
    while (sent < total) {
        ssize_t w = write(fd, p + sent, total - sent);
        if (w < 0) return sent;
        if (w == 0) break;
        sent += (size_t)w;
    }
    return sent;
}

extern int __cervus_is_memstream(FILE *f);
extern int __cervus_memstream_write(FILE *f, const char *buf, size_t len);

size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *s)
{
    if (!s || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    const char *src = (const char *)buf;

    if (__cervus_is_memstream(s))
        return __cervus_memstream_write(s, src, total) < 0 ? 0 : nmemb;

    if (s->dir == __CDIR_READ) __cervus_fflush(s);
    __cervus_setup_buf(s);

    if (s->bufmode == __CBUF_NONE) {
        size_t sent = raw_write(s->fd, src, total);
        if (sent < total) s->err = 1;
        return sent / size;
    }

    if (!s->buf) {
        s->buf = (char *)malloc(BUFSIZ);
        if (s->buf) {
            s->buf_size = BUFSIZ;
            __cervus_stream_register(s);
        }
    }
    if (!s->buf) {
        size_t sent = raw_write(s->fd, src, total);
        if (sent < total) s->err = 1;
        return sent / size;
    }

    s->dir = __CDIR_WRITE;

    if (total >= s->buf_size) {
        if (__cervus_fflush(s) == EOF) return 0;
        s->dir = __CDIR_WRITE;
        size_t sent = raw_write(s->fd, src, total);
        if (sent < total) { s->err = 1; return sent / size; }
        return nmemb;
    }

    size_t done = 0;
    while (done < total) {
        size_t room = s->buf_size - s->buf_pos;
        size_t take = total - done;
        if (take > room) take = room;
        memcpy(s->buf + s->buf_pos, src + done, take);
        s->buf_pos += take;
        done += take;
        if (s->buf_pos == s->buf_size) {
            size_t n = s->buf_pos;
            s->buf_pos = 0;
            if (raw_write(s->fd, s->buf, n) != n) { s->err = 1; return done / size; }
        }
    }

    if (s->bufmode == __CBUF_LINE && memchr(src, '\n', total))
        __cervus_fflush(s);

    return nmemb;
}

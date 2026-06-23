#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libcervus.h>

#define STDIO_BUFSZ 4096

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

int __cervus_fflush(FILE *s)
{
    if (!s) return 0;
    if (s->buf && s->buf_pos > 0) {
        size_t n = s->buf_pos;
        s->buf_pos = 0;
        if (raw_write(s->fd, s->buf, n) != n) { s->err = 1; return EOF; }
    }
    return 0;
}

size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *s)
{
    if (!s || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    const char *src = (const char *)buf;

    if (s->fd != 1) {
        size_t sent = raw_write(s->fd, src, total);
        if (sent < total) s->err = 1;
        return sent / size;
    }

    if (!s->buf) {
        s->buf = (char *)malloc(STDIO_BUFSZ);
        if (s->buf) s->buf_size = STDIO_BUFSZ;
    }
    if (!s->buf) {
        size_t sent = raw_write(s->fd, src, total);
        if (sent < total) s->err = 1;
        return sent / size;
    }

    int has_nl = memchr(src, '\n', total) != NULL;

    for (size_t i = 0; i < total; i++) {
        s->buf[s->buf_pos++] = src[i];
        if (s->buf_pos >= s->buf_size) {
            size_t n = s->buf_pos;
            s->buf_pos = 0;
            if (raw_write(s->fd, s->buf, n) != n) { s->err = 1; return i / size; }
        }
    }

    if (has_nl)
        __cervus_fflush(s);

    return nmemb;
}

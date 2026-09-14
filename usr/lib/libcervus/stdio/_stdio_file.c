#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libcervus.h>

static struct __cervus_FILE __stdin_s  = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, CERVUS_LOCK_INIT };
static struct __cervus_FILE __stdout_s = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, CERVUS_LOCK_INIT };
static struct __cervus_FILE __stderr_s = { 2, 0, 0, 0, 0, 0, 0, 0, __CBUF_NONE, 0, 0, CERVUS_LOCK_INIT };

FILE *stdin  = &__stdin_s;
FILE *stdout = &__stdout_s;
FILE *stderr = &__stderr_s;

#define STREAM_MAX 64
static struct __cervus_FILE *g_streams[STREAM_MAX];
static int g_stream_count;

void __cervus_stream_register(struct __cervus_FILE *s)
{
    if (!s) return;
    for (int i = 0; i < g_stream_count; i++)
        if (g_streams[i] == s) return;
    if (g_stream_count < STREAM_MAX) g_streams[g_stream_count++] = s;
}

void __cervus_stream_forget(struct __cervus_FILE *s)
{
    for (int i = 0; i < g_stream_count; i++) {
        if (g_streams[i] == s) {
            g_streams[i] = g_streams[--g_stream_count];
            return;
        }
    }
}

void __cervus_setup_buf(struct __cervus_FILE *s)
{
    if (!s || s->bufmode != __CBUF_UNSET) return;
    s->bufmode = isatty(s->fd) ? __CBUF_LINE : __CBUF_FULL;
}

void __cervus_flush_all(void)
{
    __cervus_fflush(stdout);
    __cervus_fflush(stderr);
    for (int i = 0; i < g_stream_count; i++)
        __cervus_fflush(g_streams[i]);
}

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

    if (s->dir == __CDIR_READ) {
        if (s->buf_len > s->buf_pos)
            lseek(s->fd, -(off_t)(s->buf_len - s->buf_pos), SEEK_CUR);
        s->buf_pos = 0;
        s->buf_len = 0;
        s->dir = __CDIR_IDLE;
        s->unget = 0;
        return 0;
    }

    if (s->buf && s->buf_pos > 0) {
        size_t n = s->buf_pos;
        s->buf_pos = 0;
        if (raw_write(s->fd, s->buf, n) != n) { s->err = 1; return EOF; }
    }
    s->dir = __CDIR_IDLE;
    return 0;
}

int __cervus_fill(FILE *s)
{
    if (!s) return -1;
    if (s->dir == __CDIR_WRITE) __cervus_fflush(s);

    __cervus_setup_buf(s);
    if (s->bufmode == __CBUF_NONE) return 0;

    if (!s->buf) {
        s->buf = (char *)malloc(BUFSIZ);
        if (!s->buf) return 0;
        s->buf_size = BUFSIZ;
        __cervus_stream_register(s);
    }

    s->dir = __CDIR_READ;
    s->buf_pos = 0;
    s->buf_len = 0;

    ssize_t r = read(s->fd, s->buf, s->buf_size);
    if (r < 0) { s->err = 1; return -1; }
    if (r == 0) { s->eof = 1; return 0; }
    s->buf_len = (size_t)r;
    return (int)r;
}

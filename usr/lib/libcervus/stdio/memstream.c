#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

typedef struct {
    char   **bufp;
    size_t  *sizep;
    char    *data;
    size_t   len;
    size_t   pos;
    size_t   cap;
} memstream_t;

static int grow(memstream_t *m, size_t need)
{
    if (need <= m->cap) return 0;
    size_t cap = m->cap ? m->cap : 128;
    while (cap < need) cap *= 2;
    char *nd = realloc(m->data, cap);
    if (!nd) return -1;
    memset(nd + m->cap, 0, cap - m->cap);
    m->data = nd;
    m->cap  = cap;
    return 0;
}

static void publish(memstream_t *m)
{
    *m->bufp  = m->data;
    *m->sizep = m->pos < m->len ? m->pos : m->len;
}

static ssize_t ms_write(void *c, const char *buf, size_t n)
{
    memstream_t *m = c;
    if (grow(m, m->pos + n + 1) < 0) {
        __cervus_errno = ENOMEM;
        return -1;
    }
    memcpy(m->data + m->pos, buf, n);
    m->pos += n;
    if (m->pos > m->len) m->len = m->pos;
    m->data[m->len] = '\0';
    publish(m);
    return (ssize_t)n;
}

static int ms_seek(void *c, off64_t *off, int whence)
{
    memstream_t *m = c;
    off64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (off64_t)m->pos
                 : whence == SEEK_END ? (off64_t)m->len : -1;
    off64_t np = base + *off;
    if (base < 0 || np < 0) {
        __cervus_errno = EINVAL;
        return -1;
    }
    if (grow(m, (size_t)np + 1) < 0) {
        __cervus_errno = ENOMEM;
        return -1;
    }
    m->pos = (size_t)np;
    publish(m);
    *off = np;
    return 0;
}

static int ms_close(void *c)
{
    memstream_t *m = c;
    publish(m);
    free(m);
    return 0;
}

FILE *open_memstream(char **bufp, size_t *sizep)
{
    if (!bufp || !sizep) {
        __cervus_errno = EINVAL;
        return NULL;
    }
    memstream_t *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->bufp  = bufp;
    m->sizep = sizep;
    if (grow(m, 1) < 0) {
        free(m);
        __cervus_errno = ENOMEM;
        return NULL;
    }
    publish(m);
    cookie_io_functions_t io = { .write = ms_write, .seek = ms_seek, .close = ms_close };
    FILE *f = fopencookie(m, "w", io);
    if (!f) {
        free(m->data);
        free(m);
        return NULL;
    }
    f->bufmode = __CBUF_NONE;
    return f;
}

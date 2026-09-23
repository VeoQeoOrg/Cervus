#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

FILE *fopencookie(void *cookie, const char *mode, cookie_io_functions_t funcs)
{
    if (!mode) {
        __cervus_errno = EINVAL;
        return NULL;
    }
    FILE *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->fd       = -1;
    f->flags    = __CF_OWNED;
    f->has_io   = 1;
    f->cookie   = cookie;
    f->io.read  = funcs.read;
    f->io.write = funcs.write;
    f->io.seek  = funcs.seek;
    f->io.close = funcs.close;
    __cervus_stream_register(f);
    return f;
}

typedef struct {
    char  *buf;
    size_t size;
    size_t len;
    size_t pos;
    int    owned;
    int    append;
} memfile_t;

static ssize_t mem_read(void *c, char *out, size_t n)
{
    memfile_t *m = c;
    if (m->pos >= m->len) return 0;
    size_t take = m->len - m->pos;
    if (take > n) take = n;
    memcpy(out, m->buf + m->pos, take);
    m->pos += take;
    return (ssize_t)take;
}

static ssize_t mem_write(void *c, const char *in, size_t n)
{
    memfile_t *m = c;
    if (m->append) m->pos = m->len;
    if (m->pos >= m->size) {
        __cervus_errno = ENOSPC;
        return n ? -1 : 0;
    }
    size_t room = m->size - m->pos;
    size_t take = n < room ? n : room;
    memcpy(m->buf + m->pos, in, take);
    m->pos += take;
    if (m->pos > m->len) {
        m->len = m->pos;
        if (m->len < m->size) m->buf[m->len] = '\0';
    }
    if (take < n) {
        __cervus_errno = ENOSPC;
        return take ? (ssize_t)take : -1;
    }
    return (ssize_t)take;
}

static int mem_seek(void *c, off64_t *off, int whence)
{
    memfile_t *m = c;
    off64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (off64_t)m->pos
                 : whence == SEEK_END ? (off64_t)m->len : -1;
    if (base < 0) {
        __cervus_errno = EINVAL;
        return -1;
    }
    off64_t np = base + *off;
    if (np < 0 || np > (off64_t)m->size) {
        __cervus_errno = EINVAL;
        return -1;
    }
    m->pos = (size_t)np;
    *off = np;
    return 0;
}

static int mem_close(void *c)
{
    memfile_t *m = c;
    if (m->owned) free(m->buf);
    free(m);
    return 0;
}

FILE *fmemopen(void *buf, size_t size, const char *mode)
{
    if (!mode || !size || (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a')) {
        __cervus_errno = EINVAL;
        return NULL;
    }
    int plus = strchr(mode, '+') != NULL;
    memfile_t *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->size = size;
    if (buf) {
        m->buf = buf;
    } else {
        m->buf = calloc(1, size);
        m->owned = 1;
        if (!m->buf) {
            free(m);
            return NULL;
        }
    }
    if (mode[0] == 'r') {
        m->len = m->owned ? 0 : size;
    } else if (mode[0] == 'w') {
        m->buf[0] = '\0';
    } else {
        m->append = 1;
        m->len = strnlen(m->buf, size);
        m->pos = m->len;
    }
    cookie_io_functions_t io = {
        .read  = (mode[0] == 'r' || plus) ? mem_read : NULL,
        .write = (mode[0] != 'r' || plus) ? mem_write : NULL,
        .seek  = mem_seek,
        .close = mem_close,
    };
    FILE *f = fopencookie(m, mode, io);
    if (!f) {
        mem_close(m);
        return NULL;
    }
    return f;
}

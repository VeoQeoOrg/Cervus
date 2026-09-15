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
    size_t   cap;
} memstream_t;

#define MEMSTREAM_MAX 16
static memstream_t *g_streams[MEMSTREAM_MAX];

static memstream_t *stream_of(FILE *f)
{
    if (!f) return 0;
    int slot = f->fd;
    if (slot < -MEMSTREAM_MAX || slot > -1) return 0;
    return g_streams[-slot - 1];
}

static int grow(memstream_t *m, size_t need)
{
    if (need <= m->cap) return 0;
    size_t cap = m->cap ? m->cap : 128;
    while (cap < need) cap *= 2;
    char *nd = realloc(m->data, cap);
    if (!nd) return -1;
    memset(nd + m->len, 0, cap - m->len);
    m->data = nd;
    m->cap  = cap;
    return 0;
}

int __cervus_memstream_write(FILE *f, const char *buf, size_t len)
{
    memstream_t *m = stream_of(f);
    if (!m) return -1;
    if (grow(m, m->len + len + 1) < 0) return -1;
    memcpy(m->data + m->len, buf, len);
    m->len += len;
    m->data[m->len] = '\0';
    *m->bufp  = m->data;
    *m->sizep = m->len;
    return (int)len;
}

FILE *open_memstream(char **bufp, size_t *sizep)
{
    if (!bufp || !sizep) { __cervus_errno = EINVAL; return 0; }

    int slot = -1;
    for (int i = 0; i < MEMSTREAM_MAX; i++) if (!g_streams[i]) { slot = i; break; }
    if (slot < 0) { __cervus_errno = EMFILE; return 0; }

    memstream_t *m = calloc(1, sizeof *m);
    if (!m) { __cervus_errno = ENOMEM; return 0; }
    m->bufp  = bufp;
    m->sizep = sizep;
    if (grow(m, 1) < 0) { free(m); __cervus_errno = ENOMEM; return 0; }
    m->data[0] = '\0';
    *bufp  = m->data;
    *sizep = 0;

    FILE *f = calloc(1, sizeof *f);
    if (!f) { free(m->data); free(m); __cervus_errno = ENOMEM; return 0; }
    f->fd      = -(slot + 1);
    f->bufmode = __CBUF_NONE;
    g_streams[slot] = m;
    return f;
}

int __cervus_memstream_close(FILE *f)
{
    memstream_t *m = stream_of(f);
    if (!m) return -1;
    int slot = -f->fd - 1;
    *m->bufp  = m->data;
    *m->sizep = m->len;
    g_streams[slot] = 0;
    free(m);
    free(f);
    return 0;
}

int __cervus_is_memstream(FILE *f)
{
    return stream_of(f) != 0;
}

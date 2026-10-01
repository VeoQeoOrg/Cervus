#include <stdio_ext.h>
#include <fcntl.h>
#include <libcervus.h>

static int access_mode(FILE *f)
{
    int fl = fcntl(f->fd, F_GETFL);
    return fl < 0 ? O_RDWR : (fl & O_ACCMODE);
}

size_t __fbufsize(FILE *f) { return f->buf_size; }
size_t __fpending(FILE *f) { return f->dir == __CDIR_WRITE ? f->buf_pos : 0; }
int __flbf(FILE *f) { return f->bufmode == __CBUF_LINE; }
int __freadable(FILE *f) { return access_mode(f) != O_WRONLY; }
int __fwritable(FILE *f) { return access_mode(f) != O_RDONLY; }
int __freading(FILE *f) { return f->dir == __CDIR_READ || access_mode(f) == O_RDONLY; }
int __fwriting(FILE *f) { return f->dir == __CDIR_WRITE || access_mode(f) == O_WRONLY; }
int __fsetlocking(FILE *f, int type) { (void)f; (void)type; return FSETLOCKING_INTERNAL; }
void __fseterr(FILE *f) { f->err = 1; }
void _flushlbf(void) { __cervus_flush_all(); }

void __fpurge(FILE *f)
{
    f->buf_pos = 0;
    f->buf_len = 0;
    f->unget = 0;
    f->dir = __CDIR_IDLE;
}

int fpurge(FILE *f)
{
    __fpurge(f);
    return 0;
}

size_t __freadahead(FILE *f)
{
    size_t n = f->dir == __CDIR_READ ? f->buf_len - f->buf_pos : 0;
    return n + (f->unget ? 1 : 0);
}

const char *__freadptr(FILE *f, size_t *sizep)
{
    if (f->unget || f->dir != __CDIR_READ || f->buf_pos >= f->buf_len) return NULL;
    *sizep = f->buf_len - f->buf_pos;
    return f->buf + f->buf_pos;
}

void __freadptrinc(FILE *f, size_t inc)
{
    f->buf_pos += inc;
}

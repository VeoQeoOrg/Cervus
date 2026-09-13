#include <stdio.h>
#include <libcervus.h>

int fgetc(FILE *s)
{
    if (!s) return EOF;

    if (s->unget) {
        int c = s->unget - 1;
        s->unget = 0;
        return c;
    }

    if (s->dir == __CDIR_READ && s->buf_pos < s->buf_len)
        return (unsigned char)s->buf[s->buf_pos++];

    __cervus_setup_buf(s);
    if (s->bufmode != __CBUF_NONE) {
        if (__cervus_fill(s) <= 0) return EOF;
        if (s->buf_pos < s->buf_len)
            return (unsigned char)s->buf[s->buf_pos++];
        return EOF;
    }

    unsigned char ch;
    if (fread(&ch, 1, 1, s) != 1) return EOF;
    return (int)ch;
}

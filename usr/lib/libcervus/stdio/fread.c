#include <stdio.h>
#include <unistd.h>
#include <libcervus.h>

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
    while (got < total) {
        ssize_t r = read(s->fd, out + got, total - got);
        if (r < 0) { s->err = 1; break; }
        if (r == 0) { s->eof = 1; break; }
        got += (size_t)r;
    }
    return got / size;
}

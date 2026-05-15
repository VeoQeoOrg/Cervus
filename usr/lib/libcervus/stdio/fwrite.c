#include <stdio.h>
#include <unistd.h>
#include <libcervus.h>

size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *s)
{
    if (!s || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t sent = 0;
    while (sent < total) {
        ssize_t w = write(s->fd, (const char *)buf + sent, total - sent);
        if (w < 0) { s->err = 1; break; }
        if (w == 0) break;
        sent += (size_t)w;
    }
    return sent / size;
}

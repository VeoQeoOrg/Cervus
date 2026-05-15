#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <libcervus.h>

extern int mkstemp(char *template);

FILE *tmpfile(void)
{
    char tmpl[64];
    strcpy(tmpl, "/mnt/tmp/tmpXXXXXX");
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        strcpy(tmpl, "/tmp/tmpXXXXXX");
        fd = mkstemp(tmpl);
        if (fd < 0) return NULL;
    }
    unlink(tmpl);
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }
    f->fd       = fd;
    f->eof      = 0;
    f->err      = 0;
    f->flags    = 1;
    f->buf      = NULL;
    f->buf_size = 0;
    f->buf_pos  = 0;
    return f;
}

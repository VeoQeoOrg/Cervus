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
    memset(f, 0, sizeof(*f));
    f->fd    = fd;
    f->flags = 1;
    __cervus_stream_register(f);
    return f;
}

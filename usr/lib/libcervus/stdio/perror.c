#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <libcervus.h>

void perror(const char *msg)
{
    if (msg && *msg) { fputs(msg, stderr); fputs(": ", stderr); }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

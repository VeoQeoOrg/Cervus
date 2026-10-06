#include <stdio.h>
#include <string.h>

char *ctermid(char *s)
{
    static char buf[L_ctermid];
    char *d = s ? s : buf;
    strcpy(d, "/dev/tty");
    return d;
}

#include <stdio.h>
#include <libcervus.h>

int fileno(FILE *s) { return s ? s->fd : -1; }

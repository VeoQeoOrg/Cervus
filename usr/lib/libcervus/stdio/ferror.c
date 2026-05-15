#include <stdio.h>
#include <libcervus.h>

int ferror(FILE *s) { return s ? s->err : 1; }

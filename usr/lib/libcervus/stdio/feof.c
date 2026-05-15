#include <stdio.h>
#include <libcervus.h>

int feof(FILE *s) { return s ? s->eof : 1; }

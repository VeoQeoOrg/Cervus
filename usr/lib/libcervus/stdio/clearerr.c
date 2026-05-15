#include <stdio.h>
#include <libcervus.h>

void clearerr(FILE *s) { if (s) { s->eof = 0; s->err = 0; } }

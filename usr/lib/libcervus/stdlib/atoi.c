#include <stdlib.h>
#include <stddef.h>

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }

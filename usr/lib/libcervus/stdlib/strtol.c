#include <stdlib.h>
#include <libcervus.h>

long strtol(const char *s, char **e, int b) { return (long)__cervus_parse_signed(s, e, b, 0); }

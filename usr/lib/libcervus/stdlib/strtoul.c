#include <stdlib.h>
#include <libcervus.h>

unsigned long strtoul(const char *s, char **e, int b) { return (unsigned long)__cervus_parse_signed(s, e, b, 1); }

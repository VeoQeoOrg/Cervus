#include <stdlib.h>
#include <libcervus.h>

unsigned long long strtoull(const char *s, char **e, int b) { return (unsigned long long)__cervus_parse_signed(s, e, b, 1); }

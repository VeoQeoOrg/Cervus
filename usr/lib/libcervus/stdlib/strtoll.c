#include <stdlib.h>
#include <libcervus.h>

long long strtoll(const char *s, char **e, int b) { return __cervus_parse_signed(s, e, b, 0); }

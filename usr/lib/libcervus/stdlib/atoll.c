#include <stdlib.h>
#include <stddef.h>

long long atoll(const char *s) { return strtoll(s, NULL, 10); }

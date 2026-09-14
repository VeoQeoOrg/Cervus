#include <inttypes.h>
#include <stdlib.h>

intmax_t strtoimax(const char *nptr, char **endptr, int base)
{
    return (intmax_t)strtoll(nptr, endptr, base);
}

uintmax_t strtoumax(const char *nptr, char **endptr, int base)
{
    return (uintmax_t)strtoull(nptr, endptr, base);
}

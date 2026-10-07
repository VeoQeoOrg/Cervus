#include <inttypes.h>
#include <stdlib.h>
#include <wchar.h>

intmax_t imaxabs(intmax_t j)
{
    return j < 0 ? -j : j;
}

imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom)
{
    imaxdiv_t r = { numer / denom, numer % denom };
    return r;
}

intmax_t strtoimax(const char *nptr, char **endptr, int base)
{
    return (intmax_t)strtoll(nptr, endptr, base);
}

uintmax_t strtoumax(const char *nptr, char **endptr, int base)
{
    return (uintmax_t)strtoull(nptr, endptr, base);
}

intmax_t wcstoimax(const wchar_t *nptr, wchar_t **endptr, int base)
{
    return (intmax_t)wcstoll(nptr, endptr, base);
}

uintmax_t wcstoumax(const wchar_t *nptr, wchar_t **endptr, int base)
{
    return (uintmax_t)wcstoull(nptr, endptr, base);
}

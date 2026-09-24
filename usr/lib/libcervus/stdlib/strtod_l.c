#include <stdlib.h>

double strtod_l(const char *s, char **end, locale_t loc)
{
    (void)loc;
    return strtod(s, end);
}

float strtof_l(const char *s, char **end, locale_t loc)
{
    (void)loc;
    return strtof(s, end);
}

long double strtold_l(const char *s, char **end, locale_t loc)
{
    (void)loc;
    return strtold(s, end);
}

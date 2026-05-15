#include <string.h>
#include <stddef.h>

static char *__strtok_save = NULL;

char *strtok(char *str, const char *delim)
{
    return strtok_r(str, delim, &__strtok_save);
}

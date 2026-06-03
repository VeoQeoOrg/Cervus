#include <stdlib.h>
#include <string.h>
#include <stddef.h>

extern char **environ;

char *getenv(const char *name)
{
    if (!name || !*name || !environ) return NULL;
    size_t nl = strlen(name);
    for (char **e = environ; *e; e++) {
        if (strncmp(*e, name, nl) == 0 && (*e)[nl] == '=')
            return (char *)(*e + nl + 1);
    }
    return NULL;
}

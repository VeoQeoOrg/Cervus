#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

extern char **environ;

int unsetenv(const char *name)
{
    if (!name || !*name || strchr(name, '=')) { __cervus_errno = EINVAL; return -1; }
    if (!environ) return 0;
    size_t nl = strlen(name);
    int rd = 0, wr = 0;
    while (environ[rd]) {
        if (strncmp(environ[rd], name, nl) == 0 && environ[rd][nl] == '=') {
            rd++;
            continue;
        }
        environ[wr++] = environ[rd++];
    }
    environ[wr] = NULL;
    return 0;
}

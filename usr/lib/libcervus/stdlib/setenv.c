#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

extern char **environ;

int setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !*name || !value || strchr(name, '=')) { __cervus_errno = EINVAL; return -1; }
    size_t nl = strlen(name);
    size_t vl = strlen(value);
    if (environ) {
        for (char **e = environ; *e; e++) {
            if (strncmp(*e, name, nl) == 0 && (*e)[nl] == '=') {
                if (!overwrite) return 0;
                break;
            }
        }
    }
    char *nv = (char *)malloc(nl + vl + 2);
    if (!nv) { __cervus_errno = ENOMEM; return -1; }
    memcpy(nv, name, nl);
    nv[nl] = '=';
    memcpy(nv + nl + 1, value, vl + 1);
    return putenv(nv);
}

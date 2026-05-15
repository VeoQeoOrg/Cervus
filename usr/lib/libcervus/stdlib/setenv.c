#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

extern char **__cervus_env_table;
extern int    __cervus_env_count;
extern int    __cervus_env_cap;

int setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !value) { __cervus_errno = EINVAL; return -1; }
    size_t nl = strlen(name);
    size_t vl = strlen(value);
    for (int i = 0; i < __cervus_env_count; i++) {
        if (strncmp(__cervus_env_table[i], name, nl) == 0 && __cervus_env_table[i][nl] == '=') {
            if (!overwrite) return 0;
            char *nv = (char *)malloc(nl + vl + 2);
            if (!nv) return -1;
            memcpy(nv, name, nl);
            nv[nl] = '=';
            memcpy(nv + nl + 1, value, vl + 1);
            __cervus_env_table[i] = nv;
            return 0;
        }
    }
    char *nv = (char *)malloc(nl + vl + 2);
    if (!nv) return -1;
    memcpy(nv, name, nl);
    nv[nl] = '=';
    memcpy(nv + nl + 1, value, vl + 1);
    return putenv(nv);
}

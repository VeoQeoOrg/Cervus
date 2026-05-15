#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

extern char **__cervus_env_table;
extern int    __cervus_env_count;
extern int    __cervus_env_cap;

int unsetenv(const char *name)
{
    if (!name) { __cervus_errno = EINVAL; return -1; }
    size_t nl = strlen(name);
    for (int i = 0; i < __cervus_env_count; i++) {
        if (strncmp(__cervus_env_table[i], name, nl) == 0 && __cervus_env_table[i][nl] == '=') {
            __cervus_env_table[i] = __cervus_env_table[--__cervus_env_count];
            return 0;
        }
    }
    return 0;
}

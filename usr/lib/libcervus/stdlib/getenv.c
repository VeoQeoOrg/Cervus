#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <libcervus.h>

char *getenv(const char *name)
{
    if (!name) return NULL;
    size_t nl = strlen(name);
    for (int i = 1; i < __cervus_argc; i++) {
        const char *a = __cervus_argv[i];
        if (a && a[0] == '-' && a[1] == '-' &&
            a[2] == 'e' && a[3] == 'n' && a[4] == 'v' && a[5] == ':') {
            const char *kv = a + 6;
            if (strncmp(kv, name, nl) == 0 && kv[nl] == '=')
                return (char *)(kv + nl + 1);
        }
    }
    return NULL;
}

#include <stdlib.h>
#include <string.h>
#include <stddef.h>

extern char **__cervus_env_table;
extern int    __cervus_env_count;
extern int    __cervus_env_cap;

int putenv(char *str)
{
    if (!str) return -1;
    char *eq = strchr(str, '=');
    if (!eq) return -1;
    size_t nl = (size_t)(eq - str);
    for (int i = 0; i < __cervus_env_count; i++) {
        if (strncmp(__cervus_env_table[i], str, nl) == 0 && __cervus_env_table[i][nl] == '=') {
            __cervus_env_table[i] = str;
            return 0;
        }
    }
    if (__cervus_env_count >= __cervus_env_cap) {
        int nc = __cervus_env_cap ? __cervus_env_cap * 2 : 16;
        char **nt = (char **)realloc(__cervus_env_table, (size_t)nc * sizeof(char *));
        if (!nt) return -1;
        __cervus_env_table = nt;
        __cervus_env_cap = nc;
    }
    __cervus_env_table[__cervus_env_count++] = str;
    return 0;
}

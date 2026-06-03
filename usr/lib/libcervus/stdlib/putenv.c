#include <stdlib.h>
#include <string.h>
#include <stddef.h>

extern char **environ;
static char **owned_env  = NULL;
static int    owned_cap  = 0;
static int    owned_used = 0;

static int env_take_ownership(void)
{
    if (owned_env && environ == owned_env) return 0;
    int n = 0;
    if (environ) while (environ[n]) n++;
    int cap = n + 16;
    char **nt = (char **)malloc((size_t)cap * sizeof(char *));
    if (!nt) return -1;
    for (int i = 0; i < n; i++) nt[i] = environ[i];
    nt[n] = NULL;
    owned_env = nt;
    owned_cap = cap;
    owned_used = n;
    environ = owned_env;
    return 0;
}

int putenv(char *str)
{
    if (!str) return -1;
    char *eq = strchr(str, '=');
    if (!eq) return -1;
    size_t nl = (size_t)(eq - str);
    if (env_take_ownership() < 0) return -1;
    for (int i = 0; i < owned_used; i++) {
        if (strncmp(owned_env[i], str, nl) == 0 && owned_env[i][nl] == '=') {
            owned_env[i] = str;
            return 0;
        }
    }
    if (owned_used + 1 >= owned_cap) {
        int nc = owned_cap * 2;
        char **nt = (char **)realloc(owned_env, (size_t)nc * sizeof(char *));
        if (!nt) return -1;
        owned_env = nt;
        owned_cap = nc;
        environ = owned_env;
    }
    owned_env[owned_used++] = str;
    owned_env[owned_used]   = NULL;
    return 0;
}

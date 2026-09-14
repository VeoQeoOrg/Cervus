#include <unistd.h>
#include <pwutil.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static char g_login[64];

char *getlogin(void)
{
    if (pw_lookup_uid((uint32_t)getuid(), g_login, (int)sizeof g_login,
                      0, 0, 0, 0) == 0 && g_login[0])
        return g_login;

    const char *env = getenv("LOGNAME");
    if (!env) env = getenv("USER");
    if (!env) return 0;

    strncpy(g_login, env, sizeof g_login - 1);
    g_login[sizeof g_login - 1] = '\0';
    return g_login;
}

int getlogin_r(char *buf, size_t len)
{
    char *name = getlogin();
    if (!name) return ENXIO;
    if (strlen(name) + 1 > len) return ERANGE;
    strcpy(buf, name);
    return 0;
}

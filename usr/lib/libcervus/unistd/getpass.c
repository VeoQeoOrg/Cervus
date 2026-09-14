#include <unistd.h>
#include <pwutil.h>

static char g_pw[256];

char *getpass(const char *prompt)
{
    if (pw_getpass(prompt ? prompt : "Password: ", g_pw, (int)sizeof g_pw) < 0)
        return 0;
    return g_pw;
}

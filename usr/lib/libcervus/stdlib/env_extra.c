#include <stdlib.h>
#include <sys/auxv.h>

extern char **environ;

char *secure_getenv(const char *name)
{
    if (getauxval(AT_SECURE)) return NULL;
    return getenv(name);
}

int clearenv(void)
{
    static char *empty[1];
    environ = empty;
    return 0;
}

#include <dlfcn.h>
#include <stddef.h>

static const char *g_err;

void *dlopen(const char *file, int mode)
{
    (void)file; (void)mode;
    g_err = "Cervus links statically; there is no dynamic loader yet";
    return NULL;
}

int dlclose(void *handle)
{
    (void)handle;
    g_err = "Cervus links statically; there is no dynamic loader yet";
    return -1;
}

void *dlsym(void *handle, const char *name)
{
    (void)handle; (void)name;
    g_err = "Cervus links statically; there is no dynamic loader yet";
    return NULL;
}

char *dlerror(void)
{
    char *e = (char *)g_err;
    g_err = NULL;
    return e;
}

#include <dlfcn.h>
#include <stddef.h>
#include <libcervus.h>

__cervus_dl_ops_t *__cervus_dl_ops;

static const char *g_err;

void *dlopen(const char *file, int mode)
{
    if (!__cervus_dl_ops) {
        g_err = "this program is linked statically; there is no loader to call";
        return NULL;
    }
    void *h = __cervus_dl_ops->open(file, mode);
    if (!h) g_err = "cannot load that library";
    return h;
}

int dlclose(void *handle)
{
    if (!__cervus_dl_ops) {
        g_err = "this program is linked statically; there is no loader to call";
        return -1;
    }
    return __cervus_dl_ops->close(handle);
}

void *dlsym(void *handle, const char *name)
{
    if (!__cervus_dl_ops) {
        g_err = "this program is linked statically; there is no loader to call";
        return NULL;
    }
    void *p = __cervus_dl_ops->sym(handle, name);
    if (!p) g_err = "no such symbol";
    return p;
}

char *dlerror(void)
{
    char *e = (char *)g_err;
    g_err = NULL;
    return e;
}

#include <dlfcn.h>
#include <link.h>
#include <stddef.h>
#include <string.h>
#include <sys/auxv.h>
#include <libcervus.h>

__cervus_dl_ops_t *__cervus_dl_ops;

static const char *g_err;
static __cervus_lock_t g_dl_lock = CERVUS_LOCK_INIT;
static void *g_dl_owner;
static int   g_dl_depth;

static const char g_static_msg[] = "this program is linked statically; there is no loader to call";

void __cervus_dl_lock(void)
{
    void *me = __atomic_load_n(&__cervus_tls_live, __ATOMIC_ACQUIRE) ? (void *)__cervus_tcb() : (void *)1;
    if (__atomic_load_n(&g_dl_owner, __ATOMIC_RELAXED) == me) {
        g_dl_depth++;
        return;
    }
    __cervus_lock(&g_dl_lock);
    __atomic_store_n(&g_dl_owner, me, __ATOMIC_RELAXED);
    g_dl_depth = 1;
}

void __cervus_dl_unlock(void)
{
    if (--g_dl_depth) return;
    __atomic_store_n(&g_dl_owner, NULL, __ATOMIC_RELAXED);
    __cervus_unlock(&g_dl_lock);
}

void *dlopen(const char *file, int mode)
{
    if (!__cervus_dl_ops) {
        g_err = g_static_msg;
        return NULL;
    }
    __cervus_dl_lock();
    void *h = __cervus_dl_ops->open(file, mode);
    __cervus_dl_unlock();
    if (!h) g_err = "cannot load that library";
    return h;
}

int dlclose(void *handle)
{
    if (!__cervus_dl_ops) {
        g_err = g_static_msg;
        return -1;
    }
    __cervus_dl_lock();
    int r = __cervus_dl_ops->close(handle);
    __cervus_dl_unlock();
    return r;
}

void *dlsym(void *handle, const char *name)
{
    if (!__cervus_dl_ops) {
        g_err = g_static_msg;
        return NULL;
    }
    __cervus_dl_lock();
    void *p = __cervus_dl_ops->sym(handle, name, __builtin_return_address(0));
    __cervus_dl_unlock();
    if (!p) g_err = "no such symbol";
    return p;
}

char *dlerror(void)
{
    char *e = (char *)g_err;
    g_err = NULL;
    return e;
}

int dladdr(const void *addr, Dl_info *info)
{
    if (!__cervus_dl_ops || !__cervus_dl_ops->addr || !info) return 0;
    __cervus_dl_lock();
    int r = __cervus_dl_ops->addr(addr, info);
    __cervus_dl_unlock();
    return r;
}

typedef int (*phdr_cb_t)(struct dl_phdr_info *, size_t, void *);

struct iter_ctx {
    phdr_cb_t cb;
    void     *data;
};

static int iter_tramp(struct dl_phdr_info *info, size_t size, void *p)
{
    struct iter_ctx *c = p;
    info->dlpi_tls_data = __cervus_tls_block(info->dlpi_tls_modid);
    return c->cb(info, size, c->data);
}

static int iterate_static(phdr_cb_t cb, void *data)
{
    struct dl_phdr_info info;
    memset(&info, 0, sizeof info);
    const ElfW(Phdr) *ph = (const ElfW(Phdr) *)getauxval(AT_PHDR);
    size_t n = getauxval(AT_PHNUM);
    if (!ph) return 0;
    info.dlpi_name = "";
    info.dlpi_phdr = ph;
    info.dlpi_phnum = (ElfW(Half))n;
    info.dlpi_adds = 1;
    for (size_t i = 0; i < n; i++) {
        if (ph[i].p_type == PT_PHDR) info.dlpi_addr = (uintptr_t)ph - ph[i].p_vaddr;
        if (ph[i].p_type == PT_TLS && ph[i].p_memsz) info.dlpi_tls_modid = 1;
    }
    info.dlpi_tls_data = __cervus_tls_block(info.dlpi_tls_modid);
    return cb(&info, sizeof info, data);
}

int dl_iterate_phdr(int (*cb)(struct dl_phdr_info *, size_t, void *), void *data)
{
    if (!cb) return 0;
    if (!__cervus_dl_ops || !__cervus_dl_ops->iterate) return iterate_static(cb, data);
    struct iter_ctx c = { cb, data };
    __cervus_dl_lock();
    int r = __cervus_dl_ops->iterate(iter_tramp, &c);
    __cervus_dl_unlock();
    return r;
}

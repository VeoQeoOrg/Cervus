#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <libcervus.h>

typedef struct {
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} tls_info_t;

static tls_info_t g_tls;
static int        g_tls_ready;

static size_t align_up(size_t v, size_t a)
{
    if (a < 1) a = 1;
    return (v + a - 1) & ~(a - 1);
}

size_t __cervus_tls_size(void)
{
    if (!g_tls_ready) return 0;
    return align_up((size_t)g_tls.memsz, (size_t)g_tls.align);
}

void *__cervus_tls_alloc(void)
{
    if (!g_tls_ready) {
        if (syscall1(SYS_TLS_INFO, (uint64_t)(uintptr_t)&g_tls) != 0) return NULL;
        if (g_tls.align < 1) g_tls.align = 1;
        g_tls_ready = 1;
    }

    size_t tsize = align_up((size_t)g_tls.memsz, (size_t)g_tls.align);
    size_t total = tsize + sizeof(void *) * 4;

    size_t a = (size_t)g_tls.align;
    if (a < 16) a = 16;

    char *raw = malloc(total + a);
    if (!raw) return NULL;
    memset(raw, 0, total + a);

    uintptr_t block = ((uintptr_t)raw + a - 1) & ~(uintptr_t)(a - 1);
    char *tp = (char *)(block + tsize);

    if (g_tls.filesz)
        memcpy(tp - tsize, (const void *)(uintptr_t)g_tls.vaddr, (size_t)g_tls.filesz);

    *(void **)tp = tp;
    ((void **)tp)[1] = raw;
    return tp;
}

int __cervus_tls_set(void *tp)
{
    return (int)syscall1(SYS_SET_FSBASE, (uint64_t)(uintptr_t)tp);
}

void __cervus_tls_free(void *tp)
{
    if (tp) free(((void **)tp)[1]);
}

void __cervus_tls_init(void)
{
    void *tp = __cervus_tls_alloc();
    if (tp) __cervus_tls_set(tp);
}

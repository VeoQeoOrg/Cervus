#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <elf.h>
#include <sys/auxv.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <libcervus.h>

extern char **environ;
extern uintptr_t __stack_chk_guard;

typedef struct {
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} tls_info_t;

typedef struct {
    size_t mod;
    size_t off;
} tls_index_t;

typedef struct {
    void *p;
    void *raw;
} dtv_slot_t;

typedef struct {
    size_t     cap;
    dtv_slot_t s[];
} dtv_t;

static __cervus_tls_mod_t    g_exec_mod[2];
static __cervus_tls_layout_t g_exec_layout = { 0, 0, 16, 0, 1, g_exec_mod };
static const __cervus_tls_layout_t *g_layout;
static __cervus_lock_t g_tls_lock = CERVUS_LOCK_INIT;
static void          **g_threads;
static uintptr_t       g_guard, g_pguard;

static size_t align_up(size_t v, size_t a)
{
    if (a < 1) a = 1;
    return (v + a - 1) & ~(a - 1);
}

static size_t place(size_t used, const __cervus_tls_mod_t *m)
{
    size_t first = (size_t)(-m->image) & (m->align - 1);
    size_t need = used + m->memsz;
    return align_up(need > first ? need - first : 0, m->align) + first;
}

static const __cervus_tls_layout_t *layout(void)
{
    if (g_layout) return g_layout;
    if (__cervus_dl_ops && __cervus_dl_ops->tls) return g_layout = __cervus_dl_ops->tls();

    tls_info_t ti;
    if (syscall1(SYS_TLS_INFO, (uint64_t)(uintptr_t)&ti) == 0 && ti.memsz) {
        __cervus_tls_mod_t *m = &g_exec_mod[1];
        m->image = (uintptr_t)ti.vaddr;
        m->filesz = (size_t)ti.filesz;
        m->memsz = (size_t)ti.memsz;
        m->align = ti.align ? (size_t)ti.align : 1;
        m->offset = place(0, m);
        m->is_static = 1;
        g_exec_layout.nmods = 1;
        g_exec_layout.static_used = m->offset;
        g_exec_layout.static_size = m->offset;
        if (m->align > g_exec_layout.static_align) g_exec_layout.static_align = m->align;
    }
    return g_layout = &g_exec_layout;
}

static void init_block(char *blk, const __cervus_tls_mod_t *m)
{
    if (m->filesz) memcpy(blk, (const void *)m->image, m->filesz);
    if (m->memsz > m->filesz) memset(blk + m->filesz, 0, m->memsz - m->filesz);
}

static dtv_t *dtv_new(size_t cap)
{
    dtv_t *d = calloc(1, sizeof(dtv_t) + (cap + 1) * sizeof(dtv_slot_t));
    if (d) d->cap = cap;
    return d;
}

size_t __cervus_tls_size(void)
{
    return layout()->static_size;
}

void *__cervus_tls_alloc(void)
{
    __cervus_lock(&g_tls_lock);
    const __cervus_tls_layout_t *L = layout();
    size_t a = L->static_align < 16 ? 16 : L->static_align;
    size_t tcb = sizeof(void *) * __CERVUS_TCB_SLOTS;
    char *raw = calloc(1, L->static_size + a + tcb);
    dtv_t *d = dtv_new(L->nmods + 8);
    if (!raw || !d) {
        __cervus_unlock(&g_tls_lock);
        free(raw);
        free(d);
        return NULL;
    }

    char *tp = (char *)align_up((uintptr_t)raw + L->static_size, a);
    for (size_t i = 1; i <= L->nmods; i++) {
        const __cervus_tls_mod_t *m = &L->mods[i];
        if (__atomic_load_n(&m->is_static, __ATOMIC_ACQUIRE) != 1) continue;
        d->s[i].p = tp - m->offset;
        init_block(tp - m->offset, m);
    }

    void **t = (void **)tp;
    t[0] = t;
    t[__CERVUS_TCB_DTV] = d;
    t[__CERVUS_TCB_ERRNO] = &t[__CERVUS_TCB_ERRVAL];
    t[__CERVUS_TCB_HERRNO] = &t[__CERVUS_TCB_HERRVAL];
    t[__CERVUS_TCB_GUARD] = (void *)g_guard;
    t[__CERVUS_TCB_PGUARD] = (void *)g_pguard;
    t[__CERVUS_TCB_RAW] = raw;
    t[__CERVUS_TCB_LINK] = g_threads;
    g_threads = t;
    __cervus_unlock(&g_tls_lock);
    return tp;
}

int __cervus_tls_set(void *tp)
{
    int r = (int)syscall1(SYS_SET_FSBASE, (uint64_t)(uintptr_t)tp);
    if (r == 0) __atomic_store_n(&__cervus_tls_live, 1, __ATOMIC_RELEASE);
    return r;
}

void __cervus_tls_free(void *tp)
{
    if (!tp) return;
    void **t = tp;

    __cervus_lock(&g_tls_lock);
    void **prev = NULL, **cur = g_threads;
    while (cur && cur != t) {
        prev = cur;
        cur = cur[__CERVUS_TCB_LINK];
    }
    if (cur) {
        if (prev) prev[__CERVUS_TCB_LINK] = cur[__CERVUS_TCB_LINK];
        else g_threads = cur[__CERVUS_TCB_LINK];
    }
    __cervus_unlock(&g_tls_lock);

    dtv_t *d = t[__CERVUS_TCB_DTV];
    if (d) {
        for (size_t i = 1; i <= d->cap; i++) free(d->s[i].raw);
        free(d);
    }
    free(t[__CERVUS_TCB_RAW]);
}

void __cervus_tls_static_init(size_t modid)
{
    __cervus_lock(&g_tls_lock);
    const __cervus_tls_layout_t *L = layout();
    if (modid && modid <= L->nmods) {
        const __cervus_tls_mod_t *m = &L->mods[modid];
        for (void **t = g_threads; t; t = t[__CERVUS_TCB_LINK])
            init_block((char *)t - m->offset, m);
    }
    __cervus_unlock(&g_tls_lock);
}

__attribute__((force_align_arg_pointer, noinline))
static void *tls_slow(size_t mod)
{
    void **t = __cervus_tcb();
    __cervus_lock(&g_tls_lock);
    const __cervus_tls_layout_t *L = layout();
    if (!mod || mod > L->nmods) abort();

    dtv_t *d = t[__CERVUS_TCB_DTV];
    if (mod > d->cap) {
        size_t cap = L->nmods + 8;
        dtv_t *nd = realloc(d, sizeof(dtv_t) + (cap + 1) * sizeof(dtv_slot_t));
        if (!nd) abort();
        memset(&nd->s[nd->cap + 1], 0, (cap - nd->cap) * sizeof(dtv_slot_t));
        nd->cap = cap;
        t[__CERVUS_TCB_DTV] = d = nd;
    }

    if (!d->s[mod].p) {
        __cervus_tls_mod_t *m = &L->mods[mod];
        int state = 0;
        __atomic_compare_exchange_n(&m->is_static, &state, -1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        if (state == 1) {
            d->s[mod].p = (char *)t - m->offset;
        } else {
            size_t a = m->align < 16 ? 16 : m->align;
            char *raw = malloc(m->memsz + 2 * a);
            if (!raw) abort();
            char *blk = (char *)align_up((uintptr_t)raw, a) + (m->image & (m->align - 1));
            init_block(blk, m);
            d->s[mod].raw = raw;
            d->s[mod].p = blk;
        }
    }
    void *p = d->s[mod].p;
    __cervus_unlock(&g_tls_lock);
    return p;
}

void *__tls_get_addr(tls_index_t *ti)
{
    dtv_t *d = __cervus_tcb()[__CERVUS_TCB_DTV];
    if (__builtin_expect(ti->mod <= d->cap && d->s[ti->mod].p != NULL, 1))
        return (char *)d->s[ti->mod].p + ti->off;
    return (char *)tls_slow(ti->mod) + ti->off;
}

static void init_guards(void)
{
    const unsigned char *rnd = (const unsigned char *)getauxval(AT_RANDOM);
    uintptr_t g[2] = { 0, 0 };
    if (rnd) {
        memcpy(g, rnd, sizeof g);
    } else {
        int saved = __cervus_errno;
        if (getrandom(g, sizeof g, 0) != (ssize_t)sizeof g) g[0] = g[1] = 0;
        __cervus_errno = saved;
    }
    g[0] &= ~(uintptr_t)0xff;
    if (g[0]) __stack_chk_guard = g[0];
    g_guard = __stack_chk_guard;
    g_pguard = g[1];
}

void __cervus_tls_init(void)
{
    __cervus_auxv_init(environ);
    init_guards();
    void *tp = __cervus_tls_alloc();
    if (tp) __cervus_tls_set(tp);
}

void *__cervus_tls_block(size_t mod)
{
    if (!mod || !__atomic_load_n(&__cervus_tls_live, __ATOMIC_ACQUIRE)) return NULL;
    void **t = __cervus_tcb();
    dtv_t *d = t[__CERVUS_TCB_DTV];
    if (d && mod <= d->cap && d->s[mod].p) return d->s[mod].p;
    const __cervus_tls_layout_t *L = layout();
    if (mod <= L->nmods && __atomic_load_n(&L->mods[mod].is_static, __ATOMIC_ACQUIRE) == 1)
        return (char *)t - L->mods[mod].offset;
    return NULL;
}

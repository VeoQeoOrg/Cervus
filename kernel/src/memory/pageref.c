#include "../../include/memory/pageref.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <string.h>

static uint32_t *g_refs;
static size_t    g_frames;
static size_t  (*g_shrink)(size_t want);

void pageref_init(void) {
    g_frames = pmm_get_total_pages();
    size_t bytes = g_frames * sizeof(uint32_t);
    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    g_refs = (uint32_t *)pmm_alloc_zero(pages);
    if (!g_refs) {
        serial_printf("[pageref] cannot allocate %zu pages for frame reference counts\n", pages);
        g_frames = 0;
        return;
    }
    LOG_I("[pageref] tracking %zu frames (%zu KiB of counters)\n", g_frames, bytes / 1024);
}

static inline uint32_t *slot(uintptr_t phys) {
    size_t f = phys >> PAGE_SHIFT;
    return f < g_frames ? &g_refs[f] : NULL;
}

void page_ref(uintptr_t phys) {
    uint32_t *s = slot(phys);
    if (s) __atomic_fetch_add(s, 1, __ATOMIC_ACQ_REL);
}

bool page_unref(uintptr_t phys) {
    uint32_t *s = slot(phys);
    if (!s) return false;
    uint32_t old = __atomic_fetch_sub(s, 1, __ATOMIC_ACQ_REL);
    if (old == 1) {
        if (phys >= PMM_FREE_MIN_PHYS) pmm_free(pmm_phys_to_virt(phys & ~(uintptr_t)(PAGE_SIZE - 1)), 1);
        return true;
    }
    if (old == 0) {
        __atomic_fetch_add(s, 1, __ATOMIC_ACQ_REL);
        serial_printf("[pageref] unref of an unreferenced frame 0x%llx\n", (unsigned long long)phys);
    }
    return false;
}

uint32_t page_refcount(uintptr_t phys) {
    uint32_t *s = slot(phys);
    return s ? __atomic_load_n(s, __ATOMIC_ACQUIRE) : 0;
}

void page_set_shrinker(size_t (*fn)(size_t want)) { g_shrink = fn; }

uintptr_t page_alloc_user(void) {
    void *p = pmm_alloc(1);
    if (!p && g_shrink) {
        g_shrink(256);
        p = pmm_alloc(1);
    }
    if (!p) return 0;
    memset(p, 0, PAGE_SIZE);
    uintptr_t phys = pmm_virt_to_phys(p);
    uint32_t *s = slot(phys);
    if (s) __atomic_store_n(s, 1, __ATOMIC_RELEASE);
    return phys;
}

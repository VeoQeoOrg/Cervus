#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include "../../include/sched/spinlock.h"
#include <string.h>
#include <stdio.h>

static pmm_buddy_state_t g_buddy;
static spinlock_t g_pmm_lock = SPINLOCK_INIT;

static inline uintptr_t _align_up(uintptr_t v, uintptr_t a) {
    return (v + a - 1) & ~(a - 1);
}

static inline int _pages_to_order(size_t pages) {
    int order = 0;
    size_t cap = 1;
    while (cap < pages) { cap <<= 1; order++; }
    return (order > PMM_MAX_ORDER) ? -1 : order;
}

static inline uintptr_t _block_phys(pmm_block_t *b) {
    return (uintptr_t)b - g_buddy.hhdm_offset;
}

static inline pmm_block_t *_phys_to_block(uintptr_t phys) {
    return (pmm_block_t *)(phys + g_buddy.hhdm_offset);
}

static inline void _fl_init(pmm_free_list_t *fl) {
    fl->head.next  = fl->head.prev = &fl->head;
    fl->head.order = -1;
    fl->count      = 0;
}

static inline void _fl_push(pmm_free_list_t *fl, pmm_block_t *b, int order) {
    b->order        = order;
    b->next         = fl->head.next;
    b->prev         = &fl->head;
    fl->head.next->prev = b;
    fl->head.next   = b;
    fl->count++;
}

static inline void _fl_del(pmm_free_list_t *fl, pmm_block_t *b) {
    b->prev->next = b->next;
    b->next->prev = b->prev;
    b->next = b->prev = NULL;
    b->order = -1;
    fl->count--;
}

static inline pmm_block_t *_fl_first(pmm_free_list_t *fl) {
    return (fl->head.next == &fl->head) ? NULL : fl->head.next;
}

static pmm_block_t *_fl_find(pmm_free_list_t *fl, uintptr_t phys) {
    size_t limit = g_buddy.total_pages + 16;
    for (pmm_block_t *b = fl->head.next; b != &fl->head; b = b->next) {
        if (_block_phys(b) == phys) return b;
        if (limit-- == 0) return NULL;
    }
    return NULL;
}

static uintptr_t _buddy_alloc_order(int order) {
    int found = -1;
    for (int o = order; o <= PMM_MAX_ORDER; o++)
        if (_fl_first(&g_buddy.orders[o])) { found = o; break; }
    if (found < 0) return 0;

    pmm_block_t *b = _fl_first(&g_buddy.orders[found]);
    _fl_del(&g_buddy.orders[found], b);
    uintptr_t phys = _block_phys(b);

    while (found > order) {
        found--;
        uintptr_t buddy_phys = phys + ((uintptr_t)PAGE_SIZE << found);
        _fl_push(&g_buddy.orders[found], _phys_to_block(buddy_phys), found);
    }

    g_buddy.free_pages -= (size_t)1 << order;
    return phys;
}

static void _buddy_free_order(uintptr_t phys, int order) {
    if (phys < PMM_FREE_MIN_PHYS) return;
    while (order < PMM_MAX_ORDER) {
        uintptr_t buddy_phys = phys ^ ((uintptr_t)PAGE_SIZE << order);

        if (buddy_phys < PMM_FREE_MIN_PHYS) break;
        if (buddy_phys < g_buddy.mem_start || buddy_phys >= g_buddy.mem_end)
            break;

        pmm_block_t *buddy_b = _fl_find(&g_buddy.orders[order], buddy_phys);
        if (!buddy_b) break;

        if (buddy_b->order != order) break;

        _fl_del(&g_buddy.orders[order], buddy_b);
        if (buddy_phys < phys) phys = buddy_phys;
        order++;
    }
    _fl_push(&g_buddy.orders[order], _phys_to_block(phys), order);
    g_buddy.free_pages += (size_t)1 << order;
}

static void _buddy_free_nocoalesce(uintptr_t phys) {
    _fl_push(&g_buddy.orders[0], _phys_to_block(phys), 0);
    g_buddy.free_pages += 1;
}

void pmm_init(struct limine_memmap_response *memmap,
              struct limine_hhdm_response   *hhdm) {
    g_buddy.hhdm_offset = hhdm->offset;
    g_buddy.free_pages  = 0;

    uintptr_t max_phys  = 0;
    size_t usable_pages = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE) {
            uintptr_t end = e->base + e->length;
            if (end > max_phys) max_phys = end;
            usable_pages += e->length / PAGE_SIZE;
        }
    }
    max_phys = _align_up(max_phys, PAGE_SIZE);

    g_buddy.mem_start    = 0;
    g_buddy.mem_end      = max_phys;
    g_buddy.total_pages  = max_phys >> PAGE_SHIFT;
    g_buddy.usable_pages = usable_pages;

    for (int o = 0; o < PMM_MAX_ORDER_NR; o++) _fl_init(&g_buddy.orders[o]);

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;

        uintptr_t base = _align_up(e->base, PAGE_SIZE);
        uintptr_t end  = (e->base + e->length) & ~(PAGE_SIZE - 1);

        if (base < PMM_FREE_MIN_PHYS) {
            base = PMM_FREE_MIN_PHYS;
            if (base >= end) continue;
        }

        while (base < end) {
            size_t rem = (end - base) >> PAGE_SHIFT;
            int order  = PMM_MAX_ORDER;
            while (order > 0) {
                size_t bpages = (size_t)1 << order;
                if ((base & (bpages * PAGE_SIZE - 1)) == 0 && rem >= bpages)
                    break;
                order--;
            }
            _fl_push(&g_buddy.orders[order], _phys_to_block(base), order);
            g_buddy.free_pages += (size_t)1 << order;
            base += (uintptr_t)(PAGE_SIZE << order);
        }
    }

    serial_printf("[PMM] buddy init: total=%zu free=%zu\n",
                  g_buddy.total_pages, g_buddy.free_pages);
}

static volatile uint64_t g_pmm_alloc_count = 0;
static volatile uint64_t g_pmm_free_count  = 0;

void *pmm_alloc(size_t pages) {
    if (!pages) return NULL;
    int order = _pages_to_order(pages);
    if (order < 0) return NULL;
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    spinlock_acquire(&g_pmm_lock);
    uintptr_t phys = _buddy_alloc_order(order);
    size_t free_after = g_buddy.free_pages;
    spinlock_release(&g_pmm_lock);
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
    if (phys) {
        if (phys < PMM_FREE_MIN_PHYS) {
            serial_printf("[PMM_BUG] alloc returned low phys=0x%llx — discarding (should not happen after pmm_init fix)\n",
                          (unsigned long long)phys);
            phys = 0;
        } else {
            __atomic_fetch_add(&g_pmm_alloc_count, (size_t)1 << order, __ATOMIC_RELAXED);
        }
    }
    if (!phys) {
        serial_printf("[PMM_OOM] alloc FAILED pages=%zu order=%d free=%zu allocs=%llu frees=%llu\n",
                      pages, order, free_after,
                      (unsigned long long)g_pmm_alloc_count,
                      (unsigned long long)g_pmm_free_count);
    }
    return phys ? (void *)(phys + g_buddy.hhdm_offset) : NULL;
}

void *pmm_alloc_zero(size_t pages) {
    if (!pages) return NULL;
    int order = _pages_to_order(pages);
    if (order < 0) return NULL;
    void *p = pmm_alloc(pages);
    if (p) memset(p, 0, pages * PAGE_SIZE);
    return p;
}

void *pmm_alloc_aligned(size_t pages, size_t alignment) {
    if (!pages) return NULL;
    if (alignment < PAGE_SIZE) alignment = PAGE_SIZE;

    if (alignment & (alignment - 1)) {
        size_t a = 1;
        while (a < alignment) a <<= 1;
        alignment = a;
    }

    size_t align_pages = alignment / PAGE_SIZE;
    size_t req = pages > align_pages ? pages : align_pages;
    int order = _pages_to_order(req);
    if (order < 0) return NULL;

    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    spinlock_acquire(&g_pmm_lock);
    uintptr_t phys = _buddy_alloc_order(order);
    if (!phys) { spinlock_release(&g_pmm_lock); asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc"); return NULL; }

    if (phys & (alignment - 1)) {
        _buddy_free_order(phys, order);
        spinlock_release(&g_pmm_lock);
        asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
        return NULL;
    }
    spinlock_release(&g_pmm_lock);
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");

    return (void *)(phys + g_buddy.hhdm_offset);
}

void pmm_free(void *addr, size_t pages) {
    if (!addr || !pages) return;
    uintptr_t phys = (uintptr_t)addr - g_buddy.hhdm_offset;
    if (phys < g_buddy.mem_start || phys >= g_buddy.mem_end) {
        serial_printf("[PMM_FREE_BUG] out-of-range addr=%p phys=0x%llx pages=%zu\n",
                      addr, (unsigned long long)phys, pages);
        return;
    }
    if (phys < 0x100000ULL) {
        serial_printf("[PMM_FREE_BUG] low-phys addr=%p phys=0x%llx pages=%zu\n",
                      addr, (unsigned long long)phys, pages);
        return;
    }
    int order = _pages_to_order(pages);
    if (order < 0) { serial_printf("[PMM_FREE] bad order pages=%zu\n", pages); return; }

    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    spinlock_acquire(&g_pmm_lock);

    pmm_block_t *existing = _fl_find(&g_buddy.orders[order], phys);
    if (existing) {
        serial_printf("[PMM_DOUBLE_FREE] addr=%p phys=0x%llx pages=%zu order=%d ALREADY FREE!\n",
                      addr, (unsigned long long)phys, pages, order);
        spinlock_release(&g_pmm_lock);
        asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
        return;
    }

    size_t free_before = g_buddy.free_pages;
    _buddy_free_order(phys, order);
    size_t free_after = g_buddy.free_pages;
    __atomic_fetch_add(&g_pmm_free_count, (size_t)1 << order, __ATOMIC_RELAXED);

    if (free_after <= free_before) {
        serial_printf("[PMM_FREE_BUG] free_pages didn't grow: before=%zu after=%zu phys=0x%llx\n",
                      free_before, free_after, (unsigned long long)phys);
    }

    spinlock_release(&g_pmm_lock);
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}

void pmm_free_single(void *addr) {
    if (!addr) return;
    uintptr_t phys = (uintptr_t)addr - g_buddy.hhdm_offset;
    if (phys < g_buddy.mem_start || phys >= g_buddy.mem_end) return;
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    spinlock_acquire(&g_pmm_lock);
    _buddy_free_nocoalesce(phys);
    spinlock_release(&g_pmm_lock);
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}

void     *pmm_phys_to_virt(uintptr_t phys)  { return (void *)(phys + g_buddy.hhdm_offset); }
uintptr_t pmm_virt_to_phys(void *vaddr)      { return (uintptr_t)vaddr - g_buddy.hhdm_offset; }
uint64_t  pmm_get_hhdm_offset(void)          { return g_buddy.hhdm_offset; }
size_t    pmm_get_total_pages(void)           { return g_buddy.total_pages; }
size_t    pmm_get_free_pages(void)            { return g_buddy.free_pages; }
size_t    pmm_get_used_pages(void)            { return g_buddy.total_pages - g_buddy.free_pages; }

static void _print_size(size_t bytes, const char *label) {
    uint64_t v = (uint64_t)bytes;
    const char *unit;
    uint64_t whole, frac;

    if (v >= 1024ULL * 1024 * 1024) {
        whole = v / (1024ULL * 1024 * 1024);
        frac  = (v % (1024ULL * 1024 * 1024)) * 100 / (1024ULL * 1024 * 1024);
        unit  = "GiB";
    } else if (v >= 1024ULL * 1024) {
        whole = v / (1024ULL * 1024);
        frac  = (v % (1024ULL * 1024)) * 100 / (1024ULL * 1024);
        unit  = "MiB";
    } else if (v >= 1024ULL) {
        whole = v / 1024;
        frac  = (v % 1024) * 100 / 1024;
        unit  = "KiB";
    } else {
        whole = v; frac = 0; unit = "B";
    }
    printf("  %-16s %llu.%02llu %s\n", label,
           (unsigned long long)whole, (unsigned long long)frac, unit);
    serial_printf("  %-16s %llu.%02llu %s\n", label,
           (unsigned long long)whole, (unsigned long long)frac, unit);
}

void pmm_print_stats(void) {
    size_t usable_bytes = g_buddy.usable_pages * PAGE_SIZE;
    size_t free_bytes   = g_buddy.free_pages   * PAGE_SIZE;
    size_t total_bytes  = g_buddy.total_pages  * PAGE_SIZE;
    size_t used_bytes   = usable_bytes > free_bytes   ? usable_bytes - free_bytes   : 0;
    size_t reserved     = total_bytes  > usable_bytes ? total_bytes  - usable_bytes : 0;

    serial_printf("\n=== Physical Memory ===\n");
    printf("\n=== Physical Memory ===\n");
    _print_size(usable_bytes, "Usable RAM:");
    _print_size(free_bytes,   "Free RAM:");
    _print_size(used_bytes,   "Used (kernel):");
    _print_size(reserved,     "Reserved/MMIO:");
    serial_printf("  %-16s %u bytes\n", "Page size:", (unsigned)PAGE_SIZE);
    printf("  %-16s %u bytes\n", "Page size:", (unsigned)PAGE_SIZE);
    serial_printf("=======================\n");
    printf("=======================\n");

    serial_printf("[PMM] buddy free-list:\n");
    for (int o = 0; o < PMM_MAX_ORDER_NR; o++) {
        if (g_buddy.orders[o].count)
            serial_printf("  order %2d (%4zu KiB): %zu blocks\n",
                          o, (PAGE_SIZE << o) / 1024,
                          g_buddy.orders[o].count);
    }
}

#define SLAB_PAGE_ALLOC(n)   pmm_alloc_zero(n)
#define SLAB_PAGE_FREE(p, n) pmm_free(p, n)

static const size_t g_size_classes[SLAB_NUM_CACHES] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

slab_cache_t g_caches[SLAB_NUM_CACHES];

static inline uintptr_t _slab_obj_start(slab_t *s) {
    return _align_up((uintptr_t)s + sizeof(slab_t), 8);
}

static inline size_t _slab_pages(size_t obj_size) {
    uintptr_t hdr_end = _align_up(sizeof(slab_t), 8);
    if (obj_size <= PAGE_SIZE - hdr_end)
        return 1;
    if (obj_size <= PAGE_SIZE)
        return 2;
    return 0;
}

static inline uint16_t _slab_capacity(size_t obj_size) {
    size_t pages = _slab_pages(obj_size);
    if (pages == 0) return 0;
    uintptr_t obj_start = _align_up(sizeof(slab_t), 8);
    size_t avail = pages * PAGE_SIZE - obj_start;
    uint16_t cap = (uint16_t)(avail / obj_size);
    return cap > 0 ? cap : 0;
}

static void _slab_list_push(slab_t **head, slab_t *s) {
    s->next = *head; s->prev = NULL;
    if (*head) (*head)->prev = s;
    *head = s;
}

static void _slab_list_remove(slab_t **head, slab_t *s) {
    if (s->prev) s->prev->next = s->next;
    else         *head         = s->next;
    if (s->next) s->next->prev = s->prev;
    s->next = s->prev = NULL;
}

static slab_t *_slab_new(slab_cache_t *cache) {
    uint16_t cap = _slab_capacity(cache->obj_size);
    if (!cap) return NULL;

    size_t pages = _slab_pages(cache->obj_size);

    slab_t *s = (slab_t *)SLAB_PAGE_ALLOC(pages);
    if (!s) return NULL;

    s->obj_size = (uint16_t)cache->obj_size;
    s->total    = cap;
    s->used     = 0;
    s->next     = s->prev = NULL;

    uintptr_t start = _slab_obj_start(s);
    s->freelist = (void *)start;
    for (uint16_t i = 0; i < s->total; i++) {
        void **slot = (void **)(start + (uintptr_t)i * cache->obj_size);
        *slot = (i + 1 < s->total)
                ? (void *)(start + (uintptr_t)(i + 1) * cache->obj_size)
                : NULL;
    }
    return s;
}

#define LARGE_ALLOC_MAGIC 0xDEADBEEFCAFEBABEULL

typedef struct {
    uint64_t magic;
    uint64_t pages;
} large_hdr_t;

static inline bool _is_large_alloc(void *ptr) {
    large_hdr_t *hdr = (large_hdr_t *)ptr - 1;
    return hdr->magic == LARGE_ALLOC_MAGIC;
}

static slab_cache_t *_cache_for(size_t size) {
    for (int i = 0; i < SLAB_NUM_CACHES; i++)
        if (g_caches[i].obj_size >= size) return &g_caches[i];
    return NULL;
}

void slab_init(void) {
    for (int i = 0; i < SLAB_NUM_CACHES; i++) {
        g_caches[i].obj_size     = g_size_classes[i];
        g_caches[i].partial      = NULL;
        g_caches[i].full         = NULL;
        g_caches[i].total_allocs = 0;
        g_caches[i].total_frees  = 0;
    }
    serial_printf("[PMM] slab init: %d caches, sizes 8..4096 bytes\n",
                  SLAB_NUM_CACHES);
}

static spinlock_t g_slab_lock = SPINLOCK_INIT;

void *kmalloc(size_t size) {
    if (!size) return NULL;
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    spinlock_acquire(&g_slab_lock);

    void *result;
    if (size > SLAB_MAX_SIZE) {
        size_t pages = (size + sizeof(large_hdr_t) + PAGE_SIZE - 1) / PAGE_SIZE;
        spinlock_release(&g_slab_lock);
        asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
        large_hdr_t *hdr = (large_hdr_t *)SLAB_PAGE_ALLOC(pages);
        if (!hdr) return NULL;
        hdr->magic = LARGE_ALLOC_MAGIC;
        hdr->pages = (uint64_t)pages;
        return (void *)(hdr + 1);
    }

    slab_cache_t *cache = _cache_for(size);
    if (!cache) { spinlock_release(&g_slab_lock); asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc"); return NULL; }

    if (!cache->partial) {
        spinlock_release(&g_slab_lock);
        asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
        slab_t *s = _slab_new(cache);
        if (!s) return NULL;
        asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
        spinlock_acquire(&g_slab_lock);
        _slab_list_push(&cache->partial, s);
    }
    slab_t *s = cache->partial;

    void *obj   = s->freelist;
    s->freelist = *(void **)obj;
    s->used++;
    cache->total_allocs++;

    if (s->used == s->total) {
        _slab_list_remove(&cache->partial, s);
        _slab_list_push(&cache->full, s);
    }
    result = obj;
    spinlock_release(&g_slab_lock);
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
    return result;
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void *ptr) {
    if (!ptr) return;

    if (_is_large_alloc(ptr)) {
        large_hdr_t *hdr = (large_hdr_t *)ptr - 1;
        size_t pages = (size_t)hdr->pages;
        hdr->magic = 0;
        SLAB_PAGE_FREE(hdr, pages);
        return;
    }

    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    spinlock_acquire(&g_slab_lock);

    slab_t *s = (slab_t *)((uintptr_t)ptr & ~((uintptr_t)PAGE_SIZE - 1));

    slab_cache_t *cache = NULL;
    for (int i = 0; i < SLAB_NUM_CACHES; i++) {
        if (g_caches[i].obj_size == s->obj_size) {
            cache = &g_caches[i];
            break;
        }
    }
    if (!cache) { spinlock_release(&g_slab_lock); asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc"); return; }

    bool was_full = (s->used == s->total);
    *(void **)ptr = s->freelist;
    s->freelist   = ptr;
    s->used--;
    cache->total_frees++;

    if (was_full) {
        _slab_list_remove(&cache->full, s);
        _slab_list_push(&cache->partial, s);
    }
    if (s->used == 0) {
        _slab_list_remove(&cache->partial, s);
        size_t pages = _slab_pages(s->obj_size);
        spinlock_release(&g_slab_lock);
        asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
        SLAB_PAGE_FREE(s, pages > 0 ? pages : 1);
        return;
    }
    spinlock_release(&g_slab_lock);
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr)      return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }

    size_t old_size;
    if (_is_large_alloc(ptr)) {
        large_hdr_t *hdr = (large_hdr_t *)ptr - 1;
        old_size = (size_t)hdr->pages * PAGE_SIZE - sizeof(large_hdr_t);
    } else {
        slab_t *s = (slab_t *)((uintptr_t)ptr & ~((uintptr_t)PAGE_SIZE - 1));
        old_size = s->obj_size;
    }

    void *np = kmalloc(new_size);
    if (!np) return NULL;
    memcpy(np, ptr, old_size < new_size ? old_size : new_size);
    kfree(ptr);
    return np;
}

void slab_print_stats(void) {
    serial_printf("[PMM] slab stats:\n");
    for (int i = 0; i < SLAB_NUM_CACHES; i++) {
        slab_cache_t *c = &g_caches[i];
        size_t np = 0, nf = 0;
        for (slab_t *s = c->partial; s && np < 10000; s = s->next) np++;
        for (slab_t *s = c->full;    s && nf < 10000; s = s->next) nf++;
        serial_printf("  [%4zu B] partial=%zu full=%zu allocs=%zu frees=%zu\n",
                      c->obj_size, np, nf, c->total_allocs, c->total_frees);
    }
}
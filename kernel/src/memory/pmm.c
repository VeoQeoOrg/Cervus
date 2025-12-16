#include "../../include/memory/pmm.h"
#include <string.h>
#include <stdio.h>

static pmm_state_t pmm;

static inline bool bitmap_test(size_t bit) {
    return (pmm.bitmap[bit >> 3] >> (bit & 7)) & 1;
}

static inline void bitmap_set(size_t bit) {
    pmm.bitmap[bit >> 3] |= (1 << (bit & 7));
}

static inline void bitmap_clear(size_t bit) {
    pmm.bitmap[bit >> 3] &= ~(1 << (bit & 7));
}

void pmm_init(struct limine_memmap_response *memmap,
              struct limine_hhdm_response *hhdm) {

    pmm.hhdm_offset = hhdm->offset;

    uint64_t usable_pages = 0;
    uint64_t max_addr = 0;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        usable_pages += e->length / PMM_PAGE_SIZE;

        uint64_t end = e->base + e->length;
        if (end > max_addr)
            max_addr = end;
    }

    pmm.total_pages = usable_pages;
    pmm.free_pages  = usable_pages;
    pmm.mem_start   = 0;
    pmm.mem_end     = max_addr;

    size_t bitmap_bytes = (usable_pages + 7) / 8;
    size_t bitmap_pages = PMM_PAGE_ALIGN(bitmap_bytes) / PMM_PAGE_SIZE;

    pmm.bitmap_size = bitmap_bytes;

    uintptr_t bitmap_phys = 0;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE &&
            e->length >= bitmap_pages * PMM_PAGE_SIZE) {
            bitmap_phys = PMM_PAGE_ALIGN(e->base);
            break;
        }
    }

    pmm.bitmap = (uint8_t *)(bitmap_phys + pmm.hhdm_offset);

    memset(pmm.bitmap, 0xFF, bitmap_bytes);

    size_t page = 0;
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        size_t pages = e->length / PMM_PAGE_SIZE;
        for (size_t j = 0; j < pages; j++)
            bitmap_clear(page++);
    }

    size_t bitmap_page = bitmap_phys / PMM_PAGE_SIZE;
    for (size_t i = 0; i < bitmap_pages; i++) {
        bitmap_set(bitmap_page + i);
        pmm.free_pages--;
    }

    bitmap_set(0);
    pmm.free_pages--;
}

static uintptr_t find_free(size_t pages) {
    size_t run = 0;

    for (size_t i = 1; i < pmm.total_pages; i++) {
        if (!bitmap_test(i)) {
            run++;
            if (run == pages)
                return i - pages + 1;
        } else {
            run = 0;
        }
    }
    return (uintptr_t)-1;
}

void* pmm_alloc(size_t pages) {
    uintptr_t start = find_free(pages);
    if (start == (uintptr_t)-1)
        return NULL;

    for (size_t i = 0; i < pages; i++)
        bitmap_set(start + i);

    pmm.free_pages -= pages;
    return pmm_phys_to_virt(start * PMM_PAGE_SIZE);
}

void* pmm_alloc_zero(size_t pages) {
    void *p = pmm_alloc(pages);
    if (p)
        memset(p, 0, pages * PMM_PAGE_SIZE);
    return p;
}

void pmm_free(void *addr, size_t pages) {
    uintptr_t phys = pmm_virt_to_phys(addr);
    size_t page = phys / PMM_PAGE_SIZE;

    for (size_t i = 0; i < pages; i++) {
        if (bitmap_test(page + i)) {
            bitmap_clear(page + i);
            pmm.free_pages++;
        }
    }
}

void* pmm_alloc_aligned(size_t pages, size_t alignment) {
    size_t align_pages = alignment / PMM_PAGE_SIZE;
    if (!align_pages) align_pages = 1;

    size_t run = 0, start = 0;

    for (size_t i = 1; i < pmm.total_pages; i++) {
        if (i % align_pages == 0)
            run = 0;

        if (!bitmap_test(i)) {
            if (!run) start = i;
            if (++run == pages)
                break;
        } else run = 0;
    }

    if (run != pages)
        return NULL;

    for (size_t i = 0; i < pages; i++)
        bitmap_set(start + i);

    pmm.free_pages -= pages;
    return pmm_phys_to_virt(start * PMM_PAGE_SIZE);
}

uintptr_t pmm_virt_to_phys(void *addr) {
    return (uintptr_t)addr - pmm.hhdm_offset;
}

void* pmm_phys_to_virt(uintptr_t addr) {
    return (void *)(addr + pmm.hhdm_offset);
}

size_t pmm_get_total_pages(void) { return pmm.total_pages; }
size_t pmm_get_free_pages(void)  { return pmm.free_pages; }
size_t pmm_get_used_pages(void)  { return pmm.total_pages - pmm.free_pages; }

void pmm_print_stats(void) {
    size_t total_mb = (pmm.total_pages * PMM_PAGE_SIZE) / (1024 * 1024);
    size_t free_mb  = (pmm.free_pages  * PMM_PAGE_SIZE) / (1024 * 1024);
    size_t used_mb  = total_mb - free_mb;

    printf("\n=== Physical Memory ===\n");
    printf("Total RAM : %zu MB\n", total_mb);
    printf("Free RAM  : %zu MB\n", free_mb);
    printf("Used RAM  : %zu MB\n", used_mb);
    printf("Page size : %u bytes\n", PMM_PAGE_SIZE);
    printf("=======================\n");
}

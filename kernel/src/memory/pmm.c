#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdio.h>

static pmm_state_t pmm;

#define PAGE_SIZE 0x1000

static inline bool bitmap_test(size_t bit) {
    return (pmm.bitmap[bit >> 3] >> (bit & 7)) & 1;
}

static inline void bitmap_set(size_t bit) {
    pmm.bitmap[bit >> 3] |= (1 << (bit & 7));
}

static inline void bitmap_clear(size_t bit) {
    pmm.bitmap[bit >> 3] &= ~(1 << (bit & 7));
}

void pmm_init(struct limine_memmap_response* memmap,
              struct limine_hhdm_response* hhdm) {

    pmm.hhdm_offset = hhdm->offset;

    uintptr_t max_phys = 0;
    pmm.usable_pages = 0;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* e = memmap->entries[i];

        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        uintptr_t end = e->base + e->length;
        if (end > max_phys)
            max_phys = end;

        pmm.usable_pages += e->length / PAGE_SIZE;
    }

    pmm.mem_start = 0;
    pmm.mem_end   = max_phys;
    pmm.total_pages = max_phys / PAGE_SIZE;
    pmm.free_pages  = 0;

    pmm.bitmap_size = (pmm.total_pages + 7) / 8;
    size_t bitmap_pages = PMM_PAGE_ALIGN(pmm.bitmap_size) / PAGE_SIZE;

    uintptr_t bitmap_phys = 0;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* e = memmap->entries[i];

        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        if (e->length >= bitmap_pages * PAGE_SIZE) {
            bitmap_phys = PMM_PAGE_ALIGN(e->base);
            break;
        }
    }

    if (!bitmap_phys) {
        printf("PMM ERROR: cannot allocate bitmap\n");
        for (;;) asm volatile ("hlt");
    }

    pmm.bitmap = (uint8_t*)(bitmap_phys + pmm.hhdm_offset);

    memset(pmm.bitmap, 0xFF, pmm.bitmap_size);

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* e = memmap->entries[i];

        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        size_t first = e->base / PAGE_SIZE;
        size_t count = e->length / PAGE_SIZE;

        for (size_t j = 0; j < count; j++) {
            bitmap_clear(first + j);
            pmm.free_pages++;
        }
    }

    size_t bitmap_page = bitmap_phys / PAGE_SIZE;
    for (size_t i = 0; i < bitmap_pages; i++) {
        if (!bitmap_test(bitmap_page + i)) {
            bitmap_set(bitmap_page + i);
            pmm.free_pages--;
        }
    }

    if (!bitmap_test(0)) {
        bitmap_set(0);
        pmm.free_pages--;
    }
}

static size_t find_free_pages(size_t pages) {
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
    return (size_t)-1;
}

void* pmm_alloc(size_t pages) {
    size_t start = find_free_pages(pages);
    if (start == (size_t)-1)
        return NULL;

    for (size_t i = 0; i < pages; i++)
        bitmap_set(start + i);

    pmm.free_pages -= pages;
    return pmm_phys_to_virt(start * PAGE_SIZE);
}

void* pmm_alloc_zero(size_t pages) {
    void* p = pmm_alloc(pages);
    if (p)
        memset(p, 0, pages * PAGE_SIZE);
    return p;
}

void pmm_free(void* addr, size_t pages) {
    uintptr_t phys = pmm_virt_to_phys(addr);
    size_t page = phys / PAGE_SIZE;

    for (size_t i = 0; i < pages; i++) {
        if (bitmap_test(page + i)) {
            bitmap_clear(page + i);
            pmm.free_pages++;
        }
    }
}

uintptr_t pmm_virt_to_phys(void* addr) {
    return (uintptr_t)addr - pmm.hhdm_offset;
}

void* pmm_phys_to_virt(uintptr_t addr) {
    return (void*)(addr + pmm.hhdm_offset);
}


static void print_size(size_t bytes, const char* label) {
    double value = (double)bytes;
    const char* unit = "B";

    if (bytes >= 1024ULL * 1024 * 1024) {
        value = value / (1024 * 1024 * 1024);
        unit = "GB";
    } else if (bytes >= 1024 * 1024) {
        value = value / (1024 * 1024);
        unit = "MB";
    } else if (bytes >= 1024) {
        value = value / 1024;
        unit = "KB";
    }

    printf("%s: %.2f %s\n", label, value, unit);
    serial_printf(0x3F8, "%s: %.2f %s\n", label, value, unit);
}

void pmm_print_stats(void) {
    size_t usable_bytes = pmm.usable_pages * PAGE_SIZE;
    size_t free_bytes   = pmm.free_pages   * PAGE_SIZE;
    size_t used_bytes   = usable_bytes - free_bytes;
    size_t total_bytes  = pmm.total_pages * PAGE_SIZE;
    size_t reserved     = total_bytes - usable_bytes;

    printf("\n=== Physical Memory ===\n");
    serial_printf(0x3F8, "\n=== Physical Memory ===\n");

    print_size(usable_bytes, "Usable RAM");
    print_size(free_bytes,   "Free RAM");
    print_size(used_bytes,   "Used (kernel)");
    print_size(reserved,     "Reserved/MMIO");

    printf("Page size    : %u bytes\n", PAGE_SIZE);
    serial_printf(0x3F8, "Page size    : %u bytes\n", PAGE_SIZE);

    printf("=======================\n");
    serial_printf(0x3F8, "=======================\n");
}


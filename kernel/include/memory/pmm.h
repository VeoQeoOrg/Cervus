#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

typedef struct {
    uintptr_t mem_start;
    uintptr_t mem_end;
    size_t total_pages;
    size_t usable_pages;
    size_t free_pages;
    size_t bitmap_size;
    uint8_t* bitmap;
    uint64_t hhdm_offset;
} pmm_state_t;

#define PMM_PAGE_SIZE 0x1000
#define PMM_PAGE_ALIGN(x) (((x) + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1))

void pmm_init(struct limine_memmap_response*, struct limine_hhdm_response*);

void* pmm_alloc(size_t pages);
void* pmm_alloc_zero(size_t pages);
void* pmm_alloc_aligned(size_t pages, size_t alignment);
void  pmm_free(void* addr, size_t pages);

uintptr_t pmm_virt_to_phys(void* addr);
void*     pmm_phys_to_virt(uintptr_t addr);

size_t pmm_get_total_pages(void);
size_t pmm_get_free_pages(void);
size_t pmm_get_used_pages(void);

void pmm_print_stats(void);

#endif

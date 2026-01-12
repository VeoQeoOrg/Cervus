#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "vmm.h"
#include "../include/interrupts/interrupts.h"

#define PAGING_PRESENT    VMM_PRESENT
#define PAGING_WRITE      VMM_WRITE
#define PAGING_USER       VMM_USER
#define PAGING_NOEXEC     VMM_NOEXEC

#define PAGING_KERNEL     (PAGING_PRESENT | PAGING_WRITE)
#define PAGING_USER_RW    (PAGING_PRESENT | PAGING_WRITE | PAGING_USER)
#define PAGING_USER_RO    (PAGING_PRESENT | PAGING_USER)
#define PAGING_USER_NOEXEC (PAGING_PRESENT | PAGING_USER | PAGING_NOEXEC)

#define PAGING_LARGE_PAGE_SIZE    0x200000
#define PAGING_HUGE_PAGE_SIZE     0x40000000

typedef struct {
    uintptr_t virtual_start;
    uintptr_t virtual_end;
    uintptr_t physical_start;
    uint64_t flags;
    size_t page_count;
    bool allocated;
} paging_region_t;

void paging_init(void);
bool paging_map_range(vmm_pagemap_t* pagemap, uintptr_t virt_start, uintptr_t phys_start, size_t page_count, uint64_t flags);
bool paging_unmap_range(vmm_pagemap_t* pagemap, uintptr_t virt_start, size_t page_count);
bool paging_change_flags(vmm_pagemap_t* pagemap, uintptr_t virt_start, size_t page_count, uint64_t new_flags);
paging_region_t* paging_create_region(vmm_pagemap_t* pagemap, uintptr_t virt_start, size_t size, uint64_t flags);
bool paging_destroy_region(vmm_pagemap_t* pagemap, paging_region_t* region);
void* paging_alloc_pages(vmm_pagemap_t* pagemap, size_t page_count, uint64_t flags, uintptr_t preferred_virt);
void paging_free_pages(vmm_pagemap_t* pagemap, void* virt_addr, size_t page_count);
bool paging_reserve_range(vmm_pagemap_t* pagemap, uintptr_t virt_start, uintptr_t virt_end);
bool paging_is_range_free(vmm_pagemap_t* pagemap, uintptr_t virt_start, uintptr_t virt_end);
void paging_print_stats(vmm_pagemap_t* pagemap);
void paging_dump_range(vmm_pagemap_t* pagemap, uintptr_t virt_start, uintptr_t virt_end);
#endif
#include "../../include/memory/paging.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdio.h>

#define MAX_REGIONS 256
#define DEFAULT_ALLOC_BASE 0xFFFFFE0000000000ULL

static paging_region_t regions[MAX_REGIONS];
static size_t region_count = 0;
static uintptr_t next_alloc_virt = DEFAULT_ALLOC_BASE;

static uintptr_t align_up(uintptr_t addr, size_t alignment) {
    return (addr + alignment - 1) & ~(alignment - 1);
}

static bool is_aligned(uintptr_t addr, size_t alignment) {
    return (addr & (alignment - 1)) == 0;
}

static bool regions_overlap(uintptr_t start1, uintptr_t end1,
                           uintptr_t start2, uintptr_t end2) {
    return (start1 < end2) && (start2 < end1);
}

void paging_init(void) {
    memset(regions, 0, sizeof(regions));
    region_count = 0;
    
    serial_printf(COM1, "Paging: subsystem initialized\n");
}

bool paging_map_range(vmm_pagemap_t* pagemap, uintptr_t virt_start,
                     uintptr_t phys_start, size_t page_count,
                     uint64_t flags) {
    
    if (!pagemap || page_count == 0) {
        return false;
    }
    
    if (!is_aligned(virt_start, PAGING_PAGE_SIZE) ||
        !is_aligned(phys_start, PAGING_PAGE_SIZE)) {
        serial_printf(COM1, "PAGING ERROR: unaligned addresses\n");
        return false;
    }
    
    for (size_t i = 0; i < page_count; i++) {
        uintptr_t virt = virt_start + i * PAGING_PAGE_SIZE;
        uintptr_t phys = phys_start + i * PAGING_PAGE_SIZE;
        
        uintptr_t existing_phys;
        if (vmm_virt_to_phys(pagemap, virt, &existing_phys)) {
            if (existing_phys != phys) {
                serial_printf(COM1, "PAGING ERROR: page at 0x%llx already mapped to 0x%llx\n",
                       virt, existing_phys);
                return false;
            }
            uint64_t existing_flags;
            if (vmm_get_page_flags(pagemap, virt, &existing_flags)) {
                if (existing_flags != (flags & 0xFFF)) {
                    vmm_unmap_page(pagemap, virt);
                    vmm_map_page(pagemap, virt, phys, flags);
                }
            }
        } else {
            if (!vmm_map_page(pagemap, virt, phys, flags)) {
                serial_printf(COM1, "PAGING ERROR: failed to map 0x%llx -> 0x%llx\n",
                       virt, phys);
                for (size_t j = 0; j < i; j++) {
                    vmm_unmap_page(pagemap, virt_start + j * PAGING_PAGE_SIZE);
                }
                return false;
            }
        }
    }
    
    return true;
}

bool paging_unmap_range(vmm_pagemap_t* pagemap, uintptr_t virt_start,
                       size_t page_count) {
    
    if (!pagemap || page_count == 0) {
        return false;
    }
    
    for (size_t i = 0; i < page_count; i++) {
        uintptr_t virt = virt_start + i * PAGING_PAGE_SIZE;
        vmm_unmap_page(pagemap, virt);
    }
    
    return true;
}

bool paging_change_flags(vmm_pagemap_t* pagemap, uintptr_t virt_start,
                        size_t page_count, uint64_t new_flags) {
    if (!pagemap || page_count == 0) {
        return false;
    }

    if (!is_aligned(virt_start, PAGING_PAGE_SIZE)) {
        serial_printf(COM1, "PAGING ERROR: unaligned address 0x%llx\n", virt_start);
        return false;
    }

    for (size_t i = 0; i < page_count; i++) {
        uintptr_t virt = virt_start + (i * PAGING_PAGE_SIZE);
        uintptr_t phys;

        if (!vmm_virt_to_phys(pagemap, virt, &phys)) {
            serial_printf(COM1, "PAGING ERROR: page at 0x%llx not mapped\n", virt);
            return false;
        }

        vmm_unmap_page(pagemap, virt);
        if (!vmm_map_page(pagemap, virt, phys, new_flags)) {
            serial_printf(COM1, "PAGING ERROR: failed to remap 0x%llx\n", virt);
            return false;
        }
    }

    return true;
}

paging_region_t* paging_create_region(vmm_pagemap_t* pagemap,
                                     uintptr_t virt_start, size_t size,
                                     uint64_t flags) {
    
    if (region_count >= MAX_REGIONS) {
        serial_printf(COM1, "PAGING ERROR: too many regions\n");
        return NULL;
    }
    
    size_t page_count = align_up(size, PAGING_PAGE_SIZE) / PAGING_PAGE_SIZE;
    uintptr_t virt_end = virt_start + page_count * PAGING_PAGE_SIZE;
    
    for (size_t i = 0; i < region_count; i++) {
        if (regions[i].allocated &&
            regions_overlap(virt_start, virt_end,
                          regions[i].virtual_start,
                          regions[i].virtual_end)) {
            serial_printf(COM1, "PAGING ERROR: region overlaps with existing region\n");
            return NULL;
        }
    }
    
    void* phys_mem = pmm_alloc_zero(page_count);
    if (!phys_mem) {
        serial_printf(COM1, "PAGING ERROR: out of physical memory\n");
        return NULL;
    }
    
    uintptr_t phys_start = pmm_virt_to_phys(phys_mem);
    
    if (!paging_map_range(pagemap, virt_start, phys_start, page_count, flags)) {
        pmm_free(phys_mem, page_count);
        serial_printf(COM1, "PAGING ERROR: failed to map region\n");
        return NULL;
    }
    
    paging_region_t* region = &regions[region_count++];
    region->virtual_start = virt_start;
    region->virtual_end = virt_end;
    region->physical_start = phys_start;
    region->flags = flags;
    region->page_count = page_count;
    region->allocated = true;
    
    serial_printf(COM1, "Paging: created region 0x%llx-0x%llx (%zu pages)\n",
                 virt_start, virt_end, page_count);
    
    return region;
}

bool paging_destroy_region(vmm_pagemap_t* pagemap, paging_region_t* region) {
    
    if (!pagemap || !region || !region->allocated) {
        return false;
    }
    
    paging_unmap_range(pagemap, region->virtual_start, region->page_count);
    void* phys_virt = pmm_phys_to_virt(region->physical_start);
    pmm_free(phys_virt, region->page_count);
    memset(region, 0, sizeof(paging_region_t));
    
    return true;
}

void* paging_alloc_pages(vmm_pagemap_t* pagemap, size_t page_count,
                        uint64_t flags, uintptr_t preferred_virt) {
    
    if (!pagemap || page_count == 0) {
        return NULL;
    }
    
    uintptr_t virt_start;
    
    if (preferred_virt != 0) {
        virt_start = align_up(preferred_virt, PAGING_PAGE_SIZE);
        
        if (!paging_is_range_free(pagemap, virt_start,
                                 virt_start + page_count * PAGING_PAGE_SIZE)) {
            return NULL;
        }
    } else {
        virt_start = next_alloc_virt;
        
        while (!paging_is_range_free(pagemap, virt_start,
                                    virt_start + page_count * PAGING_PAGE_SIZE)) {
            virt_start += PAGING_PAGE_SIZE;
        }
        
        next_alloc_virt = virt_start + page_count * PAGING_PAGE_SIZE;
    }
    
    void* phys_mem = pmm_alloc_zero(page_count);
    if (!phys_mem) {
        return NULL;
    }
    
    uintptr_t phys_start = pmm_virt_to_phys(phys_mem);
    
    if (!paging_map_range(pagemap, virt_start, phys_start, page_count, flags)) {
        pmm_free(phys_mem, page_count);
        return NULL;
    }
    
    serial_printf(COM1, "Paging: allocated %zu pages at virt 0x%llx\n",
                 page_count, virt_start);
    
    return (void*)virt_start;
}

void paging_free_pages(vmm_pagemap_t* pagemap, void* virt_addr,
                      size_t page_count) {
    
    if (!pagemap || !virt_addr || page_count == 0) {
        return;
    }
    
    uintptr_t virt_start = (uintptr_t)virt_addr;
    
    paging_unmap_range(pagemap, virt_start, page_count);
    
    serial_printf(COM1, "Paging: freed %zu pages at virt 0x%llx\n",
                 page_count, virt_start);
}

bool paging_reserve_range(vmm_pagemap_t* pagemap, uintptr_t virt_start,
                         uintptr_t virt_end) {
    
    (void)pagemap;
    
    serial_printf(COM1, "Paging: reserved range 0x%llx-0x%llx\n",
                 virt_start, virt_end);
    return true;
}

bool paging_is_range_free(vmm_pagemap_t* pagemap, uintptr_t virt_start,
                         uintptr_t virt_end) {
    
    for (uintptr_t virt = virt_start; virt < virt_end; virt += PAGING_PAGE_SIZE) {
        uintptr_t phys;
        if (vmm_virt_to_phys(pagemap, virt, &phys)) {
            return false;
        }
    }
    
    return true;
}

void paging_print_stats(vmm_pagemap_t* pagemap) {
    
    (void)pagemap;
    
    serial_printf(COM1, "\n=== Paging Statistics ===\n");
    serial_printf(COM1, "Regions allocated: %zu\n", region_count);
    
    size_t active_regions = 0;
    size_t total_pages = 0;
    size_t total_bytes = 0;
    
    for (size_t i = 0; i < region_count; i++) {
        if (regions[i].allocated) {
            active_regions++;
            total_pages += regions[i].page_count;
            total_bytes += regions[i].page_count * PAGING_PAGE_SIZE;
            
            serial_printf(COM1, "Region %zu:\n", i);
            serial_printf(COM1, "  Virt: 0x%llx-0x%llx\n", 
                   regions[i].virtual_start, regions[i].virtual_end);
            serial_printf(COM1, "  Phys: 0x%llx\n", regions[i].physical_start);
            serial_printf(COM1, "  Size: %zu pages (%zu bytes)\n", 
                   regions[i].page_count, regions[i].page_count * PAGING_PAGE_SIZE);
            serial_printf(COM1, "  Flags: 0x%llx\n", regions[i].flags);
            
            serial_printf(COM1, "  [");
            if (regions[i].flags & PAGING_PRESENT) serial_printf(COM1, "P");
            if (regions[i].flags & PAGING_WRITE) serial_printf(COM1, "W");
            if (regions[i].flags & PAGING_USER) serial_printf(COM1, "U");
            if (regions[i].flags & PAGING_NOEXEC) serial_printf(COM1, "NX");
            serial_printf(COM1, "]\n");
        }
    }
    
    serial_printf(COM1, "\nSummary:\n");
    serial_printf(COM1, "  Active regions: %zu\n", active_regions);
    serial_printf(COM1, "  Total pages: %zu\n", total_pages);
    serial_printf(COM1, "  Total bytes: %zu bytes", total_bytes);
    
    if (total_bytes >= 1024*1024*1024) {
        serial_printf(COM1, " (%.2f GB)\n", (double)total_bytes / (1024*1024*1024));
    } else if (total_bytes >= 1024*1024) {
        serial_printf(COM1, " (%.2f MB)\n", (double)total_bytes / (1024*1024));
    } else if (total_bytes >= 1024) {
        serial_printf(COM1, " (%.2f KB)\n", (double)total_bytes / 1024);
    } else {
        serial_printf(COM1, "\n");
    }
    
    serial_printf(COM1, "  Next allocation address: 0x%llx\n", next_alloc_virt);
    serial_printf(COM1, "=========================\n");
}

void paging_dump_range(vmm_pagemap_t* pagemap, uintptr_t virt_start,
                      uintptr_t virt_end) {
    
    serial_printf(COM1, "\n=== Page Table Dump (0x%llx - 0x%llx) ===\n",
           virt_start, virt_end);
    
    size_t mapped = 0;
    size_t total = 0;
    
    for (uintptr_t virt = virt_start; virt < virt_end; virt += PAGING_PAGE_SIZE) {
        total++;
        uintptr_t phys;
        uint64_t flags;
        
        if (vmm_virt_to_phys(pagemap, virt, &phys)) {
            mapped++;
            vmm_get_page_flags(pagemap, virt, &flags);
            
            serial_printf(COM1, "0x%llx -> 0x%llx [", virt, phys);
            
            if (flags & PAGING_PRESENT) serial_printf(COM1, "P");
            if (flags & PAGING_WRITE) serial_printf(COM1, "W");
            if (flags & PAGING_USER) serial_printf(COM1, "U");
            if (flags & PAGING_NOEXEC) serial_printf(COM1, "NX");
            
            serial_printf(COM1, "]");
            
            if (phys >= pmm_get_hhdm_offset() && 
                phys < pmm_get_hhdm_offset() + 0x100000000) {
                serial_printf(COM1, " (HHDM)");
            }
            
            serial_printf(COM1, "\n");
        }
    }
    
    serial_printf(COM1, "Mapped: %zu/%zu pages (%.1f%%)\n", 
           mapped, total, (float)mapped/total * 100);
}
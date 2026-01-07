#include "../../include/memory/paging.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdio.h>

#define MAX_REGIONS 256
#define MAX_RESERVED 64
#define DEFAULT_ALLOC_BASE 0xFFFFFE0000000000ULL

static paging_region_t regions[MAX_REGIONS];
static size_t region_count = 0;

static struct {
    uintptr_t start;
    uintptr_t end;
} reserved_ranges[MAX_RESERVED];
static size_t reserved_count = 0;

static uintptr_t next_alloc_virt = DEFAULT_ALLOC_BASE;

static inline uintptr_t align_up(uintptr_t addr, size_t alignment) {
    return (addr + alignment - 1) & ~(alignment - 1);
}

static inline bool is_aligned(uintptr_t addr, size_t alignment) {
    return (addr & (alignment - 1)) == 0;
}

static inline bool ranges_overlap(uintptr_t s1, uintptr_t e1, uintptr_t s2, uintptr_t e2) {
    return s1 < e2 && s2 < e1;
}

void paging_init(void) {
    memset(regions, 0, sizeof(regions));
    memset(reserved_ranges, 0, sizeof(reserved_ranges));
    region_count = 0;
    reserved_count = 0;
    next_alloc_virt = DEFAULT_ALLOC_BASE;

    paging_reserve_range(vmm_get_kernel_pagemap(),
                         0xFFFFFFFF80000000ULL,
                         0xFFFFFFFFC0000000ULL);

    serial_printf(COM1, "Paging: subsystem initialized\n");
}

bool paging_reserve_range(vmm_pagemap_t* pagemap, uintptr_t virt_start, uintptr_t virt_end) {
    (void)pagemap;

    if (reserved_count >= MAX_RESERVED) {
        serial_printf(COM1, "PAGING ERROR: too many reserved ranges\n");
        return false;
    }

    if (virt_start >= virt_end) {
        serial_printf(COM1, "PAGING ERROR: invalid reserved range\n");
        return false;
    }

    for (size_t i = 0; i < reserved_count; i++) {
        if (ranges_overlap(virt_start, virt_end,
                          reserved_ranges[i].start,
                          reserved_ranges[i].end)) {
            serial_printf(COM1, "PAGING ERROR: reserved range overlaps\n");
            return false;
        }
    }

    reserved_ranges[reserved_count].start = virt_start;
    reserved_ranges[reserved_count].end   = virt_end;
    reserved_count++;

    serial_printf(COM1, "Paging: reserved range 0x%llx-0x%llx\n", virt_start, virt_end);
    return true;
}

static bool is_range_reserved(uintptr_t start, uintptr_t end) {
    for (size_t i = 0; i < reserved_count; i++) {
        if (ranges_overlap(start, end, reserved_ranges[i].start, reserved_ranges[i].end))
            return true;
    }
    return false;
}

bool paging_is_range_free(vmm_pagemap_t* pagemap, uintptr_t virt_start, uintptr_t virt_end) {
    if (is_range_reserved(virt_start, virt_end))
        return false;

    for (uintptr_t virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
        uintptr_t phys;
        if (vmm_virt_to_phys(pagemap, virt, &phys))
            return false;
    }
    return true;
}

bool paging_map_range(vmm_pagemap_t* pagemap, uintptr_t virt_start,
                     uintptr_t phys_start, size_t page_count, uint64_t flags) {
    if (!pagemap || page_count == 0)
        return false;

    if (!is_aligned(virt_start, PAGE_SIZE) || !is_aligned(phys_start, PAGE_SIZE)) {
        serial_printf(COM1, "PAGING ERROR: unaligned addresses\n");
        return false;
    }

    uintptr_t virt_end = virt_start + page_count * PAGE_SIZE;
    if (is_range_reserved(virt_start, virt_end)) {
        serial_printf(COM1, "PAGING ERROR: range overlaps reserved area\n");
        return false;
    }

    for (size_t i = 0; i < page_count; i++) {
        uintptr_t virt = virt_start + i * PAGE_SIZE;
        uintptr_t phys = phys_start + i * PAGE_SIZE;

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
                serial_printf(COM1, "PAGING ERROR: failed to map 0x%llx -> 0x%llx\n", virt, phys);
                for (size_t j = 0; j < i; j++)
                    vmm_unmap_page(pagemap, virt_start + j * PAGE_SIZE);
                return false;
            }
        }
    }
    return true;
}

bool paging_unmap_range(vmm_pagemap_t* pagemap, uintptr_t virt_start, size_t page_count) {
    if (!pagemap || page_count == 0)
        return false;

    for (size_t i = 0; i < page_count; i++)
        vmm_unmap_page(pagemap, virt_start + i * PAGE_SIZE);

    return true;
}

bool paging_change_flags(vmm_pagemap_t* pagemap, uintptr_t virt_start,
                        size_t page_count, uint64_t new_flags) {
    if (!pagemap || page_count == 0)
        return false;

    if (!is_aligned(virt_start, PAGE_SIZE)) {
        serial_printf(COM1, "PAGING ERROR: unaligned address 0x%llx\n", virt_start);
        return false;
    }

    for (size_t i = 0; i < page_count; i++) {
        uintptr_t virt = virt_start + i * PAGE_SIZE;
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

    size_t page_count = align_up(size, PAGE_SIZE) / PAGE_SIZE;
    uintptr_t virt_end = virt_start + page_count * PAGE_SIZE;

    if (!paging_is_range_free(pagemap, virt_start, virt_end)) {
        serial_printf(COM1, "PAGING ERROR: region overlaps existing mapping\n");
        return NULL;
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
    if (!pagemap || !region || !region->allocated)
        return false;

    paging_unmap_range(pagemap, region->virtual_start, region->page_count);
    void* phys_virt = pmm_phys_to_virt(region->physical_start);
    pmm_free(phys_virt, region->page_count);
    memset(region, 0, sizeof(paging_region_t));
    return true;
}

void* paging_alloc_pages(vmm_pagemap_t* pagemap, size_t page_count,
                        uint64_t flags, uintptr_t preferred_virt) {
    if (!pagemap || page_count == 0)
        return NULL;

    uintptr_t virt_start;
    if (preferred_virt != 0) {
        virt_start = align_up(preferred_virt, PAGE_SIZE);
        if (!paging_is_range_free(pagemap, virt_start, virt_start + page_count * PAGE_SIZE))
            return NULL;
    } else {
        virt_start = next_alloc_virt;
        while (!paging_is_range_free(pagemap, virt_start, virt_start + page_count * PAGE_SIZE))
            virt_start += PAGE_SIZE;
        next_alloc_virt = virt_start + page_count * PAGE_SIZE;
    }

    void* phys_mem = pmm_alloc_zero(page_count);
    if (!phys_mem)
        return NULL;

    uintptr_t phys_start = pmm_virt_to_phys(phys_mem);
    if (!paging_map_range(pagemap, virt_start, phys_start, page_count, flags)) {
        pmm_free(phys_mem, page_count);
        return NULL;
    }

    serial_printf(COM1, "Paging: allocated %zu pages at virt 0x%llx\n", page_count, virt_start);
    return (void*)virt_start;
}

void paging_free_pages(vmm_pagemap_t* pagemap, void* virt_addr, size_t page_count) {
    if (!pagemap || !virt_addr || page_count == 0)
        return;

    uintptr_t virt_start = (uintptr_t)virt_addr;
    paging_unmap_range(pagemap, virt_start, page_count);
    serial_printf(COM1, "Paging: freed %zu pages at virt 0x%llx\n", page_count, virt_start);
}

void paging_print_stats(vmm_pagemap_t* pagemap) {
    (void)pagemap;

    serial_printf(COM1, "\n=== Paging Statistics ===\n");
    serial_printf(COM1, "Regions: %zu, Reserved: %zu\n", region_count, reserved_count);

    size_t active = 0, total_pages = 0, total_bytes = 0;
    for (size_t i = 0; i < region_count; i++) {
        if (regions[i].allocated) {
            active++;
            total_pages += regions[i].page_count;
            total_bytes += regions[i].page_count * PAGE_SIZE;
        }
    }

    serial_printf(COM1, "Active regions: %zu\n", active);
    serial_printf(COM1, "Total pages: %zu (%.2f MB)\n", total_pages, total_bytes / (1024.0 * 1024.0));
    serial_printf(COM1, "Next alloc: 0x%llx\n", next_alloc_virt);

    serial_printf(COM1, "\nReserved ranges:\n");
    for (size_t i = 0; i < reserved_count; i++) {
        serial_printf(COM1, "  0x%llx - 0x%llx\n",
                      reserved_ranges[i].start, reserved_ranges[i].end);
    }
}

void paging_dump_range(vmm_pagemap_t* pagemap, uintptr_t virt_start, uintptr_t virt_end) {
    serial_printf(COM1, "\n=== Page Table Dump (0x%llx - 0x%llx) ===\n", virt_start, virt_end);

    size_t mapped = 0, total = 0;
    for (uintptr_t virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
        total++;
        uintptr_t phys;
        uint64_t flags;
        if (vmm_virt_to_phys(pagemap, virt, &phys) && vmm_get_page_flags(pagemap, virt, &flags)) {
            mapped++;
            serial_printf(COM1, "0x%llx -> 0x%llx [", virt, phys);
            if (flags & PAGING_PRESENT) serial_printf(COM1, "P");
            if (flags & PAGING_WRITE)  serial_printf(COM1, "W");
            if (flags & PAGING_USER)   serial_printf(COM1, "U");
            if (flags & PAGING_NOEXEC) serial_printf(COM1, "NX");
            serial_printf(COM1, "]\n");
        }
    }
    serial_printf(COM1, "Mapped: %zu/%zu pages (%.1f%%)\n", mapped, total, (float)mapped / total * 100);
}

void page_fault_handler(struct interrupt_frame* frame) {
    uint64_t fault_address;
    asm volatile("mov %%cr2, %0" : "=r" (fault_address));
    
    serial_printf(COM1, "\n[PAGE FAULT] Exception 0x%02x\n", frame->interrupt_number);
    serial_printf(COM1, "  Fault Address: 0x%016llx\n", fault_address);
    serial_printf(COM1, "  Error Code: 0x%016llx\n", frame->error_code);
    
    print_page_fault_info(frame->error_code, fault_address);
    
    uint64_t error = frame->error_code;
    const char* present = (error & 0x01) ? "protection violation" : "page not present";
    const char* write = (error & 0x02) ? "write" : "read";
    const char* user = (error & 0x04) ? "user-mode" : "supervisor-mode";
    const char* rsv = (error & 0x08) ? "reserved bit set" : "";
    const char* instr = (error & 0x10) ? "instruction fetch" : "";
    
    serial_printf(COM1, "  Details: %s, %s, %s, %s, %s\n", 
                  present, write, user, rsv, instr);
    
    dump_registers(frame);
    
    serial_writestring(COM1, "\n[PAGE FAULT] System halted\n");
    
    for(;;) {
        asm volatile("hlt");
    }
}

void print_page_fault_info(uint64_t error_code, uint64_t fault_address) {
    serial_writestring(COM1, "  Page Fault Analysis:\n");
    
    if (!(error_code & 0x1)) {
        serial_writestring(COM1, "    - Page not present\n");
    } else {
        serial_writestring(COM1, "    - Page protection violation\n");
    }
    
    if (error_code & 0x2) {
        serial_writestring(COM1, "    - Write access\n");
    } else {
        serial_writestring(COM1, "    - Read access\n");
    }
    
    if (error_code & 0x4) {
        serial_writestring(COM1, "    - User-mode access\n");
    } else {
        serial_writestring(COM1, "    - Supervisor-mode access\n");
    }
    
    if (error_code & 0x8) {
        serial_writestring(COM1, "    - Reserved bit violation\n");
    }
    
    if (error_code & 0x10) {
        serial_writestring(COM1, "    - Instruction fetch\n");
    }
    
    serial_printf(COM1, "    - Faulting address: 0x%016llx\n", fault_address);
}
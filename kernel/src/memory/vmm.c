#include "../../include/memory/vmm.h"
#include "../../include/memory/pmm.h"
#include "../../include/smp/smp.h"
#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include <stdio.h>
#include <string.h>

#define KERNEL_TEST_BASE 0xFFFF800000100000ULL
#define MASK 0x1FF

static vmm_pagemap_t kernel_pagemap;

static inline void invlpg(void* addr) {
    asm volatile ("invlpg (%0)" :: "r"(addr) : "memory");
}

static vmm_pte_t* alloc_table(void) {
    void* page = pmm_alloc_zero(1);
    if (!page) {
        printf("VMM ERROR: out of memory\n");
        for (;;) asm volatile ("hlt");
    }
    return (vmm_pte_t*)page;
}

static vmm_pte_t* get_table(vmm_pte_t* parent, size_t index, uint64_t flags) {
    if (!(parent[index] & VMM_PRESENT)) {
        vmm_pte_t* table = alloc_table();
        parent[index] = pmm_virt_to_phys(table)
                        | VMM_PRESENT
                        | VMM_WRITE
                        | (flags & VMM_USER);
    } else if (flags & VMM_USER) {
        parent[index] |= VMM_USER;
    }
    return (vmm_pte_t*)pmm_phys_to_virt(parent[index] & ~0xFFF);
}

bool vmm_map_page(vmm_pagemap_t* map, uintptr_t virt, uintptr_t phys, uint64_t flags) {
    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;

    vmm_pte_t* pdpt = get_table(map->pml4, pml4_i, flags);
    vmm_pte_t* pd   = get_table(pdpt,      pdpt_i, flags);
    vmm_pte_t* pt   = get_table(pd,        pd_i,   flags);

    pt[pt_i] = (phys & ~0xFFF) | flags | VMM_PRESENT | VMM_WRITE;

    asm volatile ("mfence" ::: "memory");
    invlpg((void*)virt);
    asm volatile ("mfence" ::: "memory");

    return true;
}

void vmm_unmap_page_noflush(vmm_pagemap_t* map, uintptr_t virt) {
    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;

    if (!(map->pml4[pml4_i] & VMM_PRESENT)) return;
    vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & ~0xFFF);
    if (!(pdpt[pdpt_i] & VMM_PRESENT)) return;
    vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & ~0xFFF);
    if (!(pd[pd_i] & VMM_PRESENT)) return;
    vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & ~0xFFF);
    pt[pt_i] = 0;
}

void vmm_unmap_page(vmm_pagemap_t* map, uintptr_t virt) {
    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;

    if (!(map->pml4[pml4_i] & VMM_PRESENT)) return;
    vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & ~0xFFF);
    if (!(pdpt[pdpt_i] & VMM_PRESENT)) return;
    vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & ~0xFFF);
    if (!(pd[pd_i] & VMM_PRESENT)) return;
    vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & ~0xFFF);

    pt[pt_i] = 0;
    asm volatile ("mfence" ::: "memory");
    invlpg((void*)virt);

    if (smp_get_cpu_count() > 1) {
        ipi_tlb_shootdown_broadcast(&virt, 1);
    }
}

vmm_pagemap_t* vmm_create_pagemap(void) {
    vmm_pagemap_t* map = pmm_alloc_zero(1);
    if (!map) {
        printf("VMM ERROR: cannot allocate pagemap\n");
        for (;;) asm volatile ("hlt");
    }

    map->pml4 = alloc_table();

    for (size_t i = 256; i < 512; i++) {
        map->pml4[i] = kernel_pagemap.pml4[i];
    }

    return map;
}

void vmm_switch_pagemap(vmm_pagemap_t* map) {
    uintptr_t phys = pmm_virt_to_phys(map->pml4);
    asm volatile ("mov %0, %%cr3" :: "r"(phys) : "memory");
}

bool vmm_virt_to_phys(vmm_pagemap_t* map, uintptr_t virt, uintptr_t* phys_out) {
    if (!map || !phys_out) {
        serial_printf("VMM_VIRT_TO_PHYS ERROR: null parameters\n");
        return false;
    }

    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;

    if (!(map->pml4[pml4_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & ~0xFFF);
    if (!(pdpt[pdpt_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & ~0xFFF);
    if (!(pd[pd_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & ~0xFFF);
    if (!(pt[pt_i] & VMM_PRESENT)) return false;

    *phys_out = (pt[pt_i] & ~0xFFF) | (virt & 0xFFF);
    return true;
}

bool vmm_get_page_flags(vmm_pagemap_t* map, uintptr_t virt, uint64_t* flags_out) {
    if (!map || !flags_out) return false;

    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;

    if (!(map->pml4[pml4_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & ~0xFFF);
    if (!(pdpt[pdpt_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & ~0xFFF);
    if (!(pd[pd_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & ~0xFFF);
    if (!(pt[pt_i] & VMM_PRESENT)) return false;

    *flags_out = pt[pt_i] & (0xFFF | (1ULL << 63));
    return true;
}

uintptr_t kernel_pml4_phys;

void vmm_init(void) {
    uintptr_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    kernel_pagemap.pml4 = (vmm_pte_t*)pmm_phys_to_virt(cr3);
    kernel_pml4_phys = cr3;
    serial_printf("VMM: kernel pagemap initialized\n");
    serial_printf("VMM: kernel PML4 phys = 0x%llx\n", kernel_pml4_phys);
}

vmm_pagemap_t* vmm_get_kernel_pagemap(void) {
    return &kernel_pagemap;
}

void vmm_test(void) {
    serial_printf("\n--- VMM EXTENDED 64-BIT TEST ---\n");

    void* phys1 = pmm_alloc_zero(1);
    void* phys2 = pmm_alloc_zero(1);

    uintptr_t paddr1 = pmm_virt_to_phys(phys1);
    uintptr_t paddr2 = pmm_virt_to_phys(phys2);

    uintptr_t vaddr1 = KERNEL_TEST_BASE;
    uintptr_t vaddr2 = KERNEL_TEST_BASE + 0x1000;

    vmm_map_page(&kernel_pagemap, vaddr1, paddr1, VMM_WRITE);
    vmm_map_page(&kernel_pagemap, vaddr2, paddr2, VMM_WRITE);

    uint64_t* ptr1 = (uint64_t*)vaddr1;
    uint64_t* ptr2 = (uint64_t*)vaddr2;

    *ptr1 = 0xDEADBEEFCAFEBABE;
    *ptr2 = 0xFEEDFACE12345678;

    vmm_pagemap_t* new_map = vmm_create_pagemap();

    void* phys3 = pmm_alloc_zero(1);
    uintptr_t paddr3 = pmm_virt_to_phys(phys3);
    uintptr_t vaddr3 = KERNEL_TEST_BASE + 0x2000;

    vmm_map_page(new_map, vaddr3, paddr3, VMM_WRITE);

    uint64_t* ptr3 = (uint64_t*)vaddr3;
    *ptr3 = 0xBAADF00DBAADF00D;

    vmm_switch_pagemap(new_map);
    serial_printf("Value (new): 0x%llx\n", *ptr3);

    vmm_switch_pagemap(&kernel_pagemap);
    serial_printf("Value (kernel): 0x%llx\n", *ptr1);

    serial_printf("--- VMM TEST DONE ---\n");

    serial_printf("\n--- VMM TRANSLATION TEST ---\n");

    void* phys_page = pmm_alloc_zero(1);
    uintptr_t paddr = pmm_virt_to_phys(phys_page);
    uintptr_t vaddr = KERNEL_TEST_BASE + 0x3000;

    vmm_map_page(&kernel_pagemap, vaddr, paddr, VMM_WRITE);

    uintptr_t translated_phys;
    if (vmm_virt_to_phys(&kernel_pagemap, vaddr, &translated_phys)) {
        serial_printf("Virt 0x%llx -> Phys 0x%llx\n", vaddr, translated_phys);
        serial_printf("Original phys: 0x%llx\n", paddr);
        serial_printf("Match: %s\n", translated_phys == paddr ? "YES" : "NO");
    } else {
        serial_printf("Translation failed!\n");
    }

    uint64_t flags;
    if (vmm_get_page_flags(&kernel_pagemap, vaddr, &flags)) {
        serial_printf("Page flags: 0x%llx\n", flags);
        serial_printf("Present: %s\n", (flags & VMM_PRESENT) ? "YES" : "NO");
        serial_printf("Writable: %s\n", (flags & VMM_WRITE) ? "YES" : "NO");
    }

    uint64_t hhdm = pmm_get_hhdm_offset();
    serial_printf("HHDM offset: 0x%llx\n", hhdm);

    serial_printf("\n--- Memory statistics after tests ---\n");
    size_t free_before = pmm_get_free_pages();

    serial_printf("\n--- Memory free test ---\n");
    void* test_alloc = pmm_alloc_aligned(2, 4096);
    if (test_alloc) {
        size_t free_after_alloc = pmm_get_free_pages();
        serial_printf("Allocated 2 pages. Free pages: %zu -> %zu\n",
                     free_before, free_after_alloc);

        pmm_free(test_alloc, 2);
        size_t free_after_free = pmm_get_free_pages();
        serial_printf("Freed 2 pages. Free pages: %zu -> %zu\n",
                     free_after_alloc, free_after_free);

        if (free_after_free == free_before) {
            serial_printf("Memory free test PASSED\n");
        } else {
            serial_printf("Memory free test FAILED (possible leak)\n");
        }
    }

    serial_printf("--- VMM TRANSLATION TEST DONE ---\n");
}
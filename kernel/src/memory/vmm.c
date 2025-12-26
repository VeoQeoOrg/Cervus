#include "../../include/memory/vmm.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdio.h>

#define PAGE_SIZE 0x1000
#define MASK 0x1FF

#define KERNEL_TEST_BASE 0xFFFFFFFF90000000ULL

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

static vmm_pte_t* get_table(vmm_pte_t* parent, size_t index) {
    if (!(parent[index] & VMM_PRESENT)) {
        vmm_pte_t* table = alloc_table();
        parent[index] = pmm_virt_to_phys(table) | VMM_PRESENT | VMM_WRITE;
    }
    return (vmm_pte_t*)pmm_phys_to_virt(parent[index] & ~0xFFF);
}

bool vmm_map_page(vmm_pagemap_t* map,
                  uintptr_t virt,
                  uintptr_t phys,
                  uint64_t flags) {

    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;

    vmm_pte_t* pdpt = get_table(map->pml4, pml4_i);
    vmm_pte_t* pd   = get_table(pdpt, pdpt_i);
    vmm_pte_t* pt   = get_table(pd, pd_i);

    pt[pt_i] = (phys & ~0xFFF) | flags | VMM_PRESENT;
    invlpg((void*)virt);
    return true;
}

void vmm_unmap_page(vmm_pagemap_t* map, uintptr_t virt) {
    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;

    if (!(map->pml4[pml4_i] & VMM_PRESENT))
        return;

    vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & ~0xFFF);
    if (!(pdpt[pdpt_i] & VMM_PRESENT))
        return;

    vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & ~0xFFF);
    if (!(pd[pd_i] & VMM_PRESENT))
        return;

    vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & ~0xFFF);
    pt[pt_i] = 0;

    invlpg((void*)virt);
}

vmm_pagemap_t* vmm_create_pagemap(void) {
    vmm_pagemap_t* map = pmm_alloc_zero(1);
    if (!map) {
        printf("VMM ERROR: cannot allocate pagemap\n");
        for (;;) asm volatile ("hlt");
    }

    map->pml4 = alloc_table();

    for (size_t i = 256; i < 512; i++)
        map->pml4[i] = kernel_pagemap.pml4[i];

    serial_printf(0x3F8, "VMM: new pagemap created\n");
    return map;
}

void vmm_switch_pagemap(vmm_pagemap_t* map) {
    uintptr_t phys = pmm_virt_to_phys(map->pml4);
    asm volatile ("mov %0, %%cr3" :: "r"(phys) : "memory");
}

void vmm_init(void) {
    uintptr_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    kernel_pagemap.pml4 = (vmm_pte_t*)pmm_phys_to_virt(cr3);

    serial_printf(0x3F8, "VMM: kernel pagemap initialized\n");
}

void vmm_test(void) {
    uint16_t port = 0x3F8;

    serial_printf(port, "\n--- VMM EXTENDED 64-BIT TEST ---\n");

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
    serial_printf(port, "Value (new): 0x%llx\n", *ptr3);

    vmm_switch_pagemap(&kernel_pagemap);
    serial_printf(port, "Value (kernel): 0x%llx\n", *ptr1);

    serial_printf(port, "--- VMM TEST DONE ---\n");
}

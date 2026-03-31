#include "../../include/memory/vmm.h"
#include "../../include/memory/pmm.h"
#include "../../include/smp/smp.h"
#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include <stdio.h>
#include <string.h>

#define KERNEL_TEST_BASE 0xFFFF800000100000ULL
#define PTE_PHYS_MASK  0x000FFFFFFFFFF000ULL
#define MASK 0x1FF

static vmm_pagemap_t kernel_pagemap;

static inline void invlpg(void* addr) {
    asm volatile ("invlpg (%0)" :: "r"(addr) : "memory");
}

static vmm_pte_t* alloc_table(void) {
    void* page = pmm_alloc_zero(1);
    if (!page) {
        serial_printf("[VMM] alloc_table: OUT OF MEMORY (pmm_alloc_zero(1) returned NULL)!\n");
        serial_printf("[VMM] free_pages=%zu\n", (size_t)pmm_get_free_pages());
        for (;;) asm volatile ("hlt");
    }
    return (vmm_pte_t*)page;
}

static vmm_pte_t* get_table(vmm_pte_t* parent, size_t index, uint64_t flags) {
    if (!(parent[index] & VMM_PRESENT)) {
        vmm_pte_t* table = alloc_table();
        uintptr_t table_phys = pmm_virt_to_phys(table);
        parent[index] = table_phys
                        | VMM_PRESENT
                        | VMM_WRITE
                        | (flags & VMM_USER);
    } else if (flags & VMM_USER) {
        parent[index] |= VMM_USER;
    }
    return (vmm_pte_t*)pmm_phys_to_virt(parent[index] & PTE_PHYS_MASK);
}

bool vmm_map_page(vmm_pagemap_t* map, uintptr_t virt, uintptr_t phys, uint64_t flags) {
    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;
    vmm_pte_t* pdpt = get_table(map->pml4, pml4_i, flags);
    vmm_pte_t* pd   = get_table(pdpt,      pdpt_i, flags);
    vmm_pte_t* pt   = get_table(pd,        pd_i,   flags);
    pt[pt_i] = (phys & PTE_PHYS_MASK) | (flags | VMM_PRESENT);

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
    vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & PTE_PHYS_MASK);
    if (!(pdpt[pdpt_i] & VMM_PRESENT)) return;
    vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & PTE_PHYS_MASK);
    if (!(pd[pd_i] & VMM_PRESENT)) return;
    vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & PTE_PHYS_MASK);
    pt[pt_i] = 0;
}

void vmm_unmap_page(vmm_pagemap_t* map, uintptr_t virt) {
    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;

    if (!(map->pml4[pml4_i] & VMM_PRESENT)) return;
    vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & PTE_PHYS_MASK);
    if (!(pdpt[pdpt_i] & VMM_PRESENT)) return;
    vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & PTE_PHYS_MASK);
    if (!(pd[pd_i] & VMM_PRESENT)) return;
    vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & PTE_PHYS_MASK);

    if (!(pt[pt_i] & VMM_PRESENT)) return;

    uintptr_t phys = pt[pt_i] & PTE_PHYS_MASK;

    pt[pt_i] = 0;
    asm volatile ("mfence" ::: "memory");
    invlpg((void*)virt);
    if (smp_get_cpu_count() > 1 && (virt >= 0xffff800000000000ULL)) {
        ipi_tlb_shootdown_broadcast(&virt, 1);
    }

    if (phys >= PMM_FREE_MIN_PHYS) {
        pmm_free(pmm_phys_to_virt(phys), 1);
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
    vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & PTE_PHYS_MASK);
    if (!(pdpt[pdpt_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & PTE_PHYS_MASK);
    if (!(pd[pd_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & PTE_PHYS_MASK);
    if (!(pt[pt_i] & VMM_PRESENT)) return false;

    *phys_out = (pt[pt_i] & PTE_PHYS_MASK) | (virt & 0xFFF);
    return true;
}

bool vmm_get_page_flags(vmm_pagemap_t* map, uintptr_t virt, uint64_t* flags_out) {
    if (!map || !flags_out) return false;

    size_t pml4_i = (virt >> 39) & MASK;
    size_t pdpt_i = (virt >> 30) & MASK;
    size_t pd_i   = (virt >> 21) & MASK;
    size_t pt_i   = (virt >> 12) & MASK;

    if (!(map->pml4[pml4_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & PTE_PHYS_MASK);
    if (!(pdpt[pdpt_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & PTE_PHYS_MASK);
    if (!(pd[pd_i] & VMM_PRESENT)) return false;
    vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & PTE_PHYS_MASK);
    if (!(pt[pt_i] & VMM_PRESENT)) return false;

    *flags_out = pt[pt_i] & (0xFFF | (1ULL << 63));
    return true;
}

vmm_pagemap_t* vmm_clone_pagemap(vmm_pagemap_t* src) {
    if (!src) return NULL;

    vmm_pagemap_t* dst = pmm_alloc_zero(1);
    if (!dst) return NULL;
    dst->pml4 = alloc_table();

    for (size_t i = 256; i < 512; i++)
        dst->pml4[i] = kernel_pagemap.pml4[i];

    for (size_t pml4_i = 0; pml4_i < 256; pml4_i++) {
        if (!(src->pml4[pml4_i] & VMM_PRESENT)) continue;

        vmm_pte_t* src_pdpt = (vmm_pte_t*)pmm_phys_to_virt(src->pml4[pml4_i] & PTE_PHYS_MASK);
        vmm_pte_t* dst_pdpt = alloc_table();
        dst->pml4[pml4_i] = pmm_virt_to_phys(dst_pdpt) | (src->pml4[pml4_i] & 0xFFF);

        for (size_t pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!(src_pdpt[pdpt_i] & VMM_PRESENT)) continue;

            vmm_pte_t* src_pd = (vmm_pte_t*)pmm_phys_to_virt(src_pdpt[pdpt_i] & PTE_PHYS_MASK);
            vmm_pte_t* dst_pd = alloc_table();
            dst_pdpt[pdpt_i] = pmm_virt_to_phys(dst_pd) | (src_pdpt[pdpt_i] & 0xFFF);

            for (size_t pd_i = 0; pd_i < 512; pd_i++) {
                if (!(src_pd[pd_i] & VMM_PRESENT)) continue;
                if (src_pd[pd_i] & VMM_PSE) {
                    void* new_hp = pmm_alloc(512);
                    if (!new_hp) continue;
                    void* old_hp = pmm_phys_to_virt(src_pd[pd_i] & PTE_PHYS_MASK & ~0x1FFFFFULL);
                    memcpy(new_hp, old_hp, 512 * 0x1000);
                    uint64_t hp_flags = src_pd[pd_i] & ~(PTE_PHYS_MASK & ~0x1FFFFFULL);
                    dst_pd[pd_i] = (pmm_virt_to_phys(new_hp) & (PTE_PHYS_MASK & ~0x1FFFFFULL))
                                 | hp_flags;
                    continue;
                }

                vmm_pte_t* src_pt = (vmm_pte_t*)pmm_phys_to_virt(src_pd[pd_i] & PTE_PHYS_MASK);
                vmm_pte_t* dst_pt = alloc_table();
                dst_pd[pd_i] = pmm_virt_to_phys(dst_pt) | (src_pd[pd_i] & 0xFFF);

                for (size_t pt_i = 0; pt_i < 512; pt_i++) {
                    if (!(src_pt[pt_i] & VMM_PRESENT)) continue;

                    uintptr_t src_phys = src_pt[pt_i] & PTE_PHYS_MASK;
                    if (src_phys < PMM_FREE_MIN_PHYS) continue;

                    void* new_page = pmm_alloc_zero(1);
                    if (!new_page) continue;
                    void* old_page = pmm_phys_to_virt(src_phys);
                    memcpy(new_page, old_page, 0x1000);
                    dst_pt[pt_i] = pmm_virt_to_phys(new_page)
                                 | (src_pt[pt_i] & (0xFFF | (1ULL << 63)));
                }
            }
        }
    }

    return dst;
}

void vmm_free_pagemap(vmm_pagemap_t* map)
{
    if (!map || !map->pml4) return;

    for (size_t pml4_i = 0; pml4_i < 256; pml4_i++) {
        if (!(map->pml4[pml4_i] & VMM_PRESENT)) continue;

        vmm_pte_t* pdpt = (vmm_pte_t*)pmm_phys_to_virt(map->pml4[pml4_i] & PTE_PHYS_MASK);

        for (size_t pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!(pdpt[pdpt_i] & VMM_PRESENT)) continue;
            vmm_pte_t* pd = (vmm_pte_t*)pmm_phys_to_virt(pdpt[pdpt_i] & PTE_PHYS_MASK);

            for (size_t pd_i = 0; pd_i < 512; pd_i++) {
                if (!(pd[pd_i] & VMM_PRESENT)) continue;

                if (pd[pd_i] & VMM_PSE) {
                    uintptr_t hp_phys = pd[pd_i] & PTE_PHYS_MASK & ~0x1FFFFFULL;
                    if (hp_phys >= PMM_FREE_MIN_PHYS)
                        pmm_free(pmm_phys_to_virt(hp_phys), 512);
                    continue;
                }

                vmm_pte_t* pt = (vmm_pte_t*)pmm_phys_to_virt(pd[pd_i] & PTE_PHYS_MASK);

                for (size_t pt_i = 0; pt_i < 512; pt_i++) {
                    if (!(pt[pt_i] & VMM_PRESENT)) continue;
                    uintptr_t phys = pt[pt_i] & PTE_PHYS_MASK;
                    if (phys >= PMM_FREE_MIN_PHYS)
                        pmm_free(pmm_phys_to_virt(phys), 1);
                }

                uintptr_t pt_phys = pd[pd_i] & PTE_PHYS_MASK;
                if (pt_phys >= PMM_FREE_MIN_PHYS)
                    pmm_free(pt, 1);
            }

            uintptr_t pd_phys = pdpt[pdpt_i] & PTE_PHYS_MASK;
            if (pd_phys >= PMM_FREE_MIN_PHYS)
                pmm_free(pd, 1);
        }

        uintptr_t pdpt_phys = map->pml4[pml4_i] & PTE_PHYS_MASK;
        if (pdpt_phys >= PMM_FREE_MIN_PHYS)
            pmm_free(pdpt, 1);
    }

    pmm_free(map->pml4, 1);
    pmm_free(map, 1);
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

void vmm_sync_kernel_mappings(vmm_pagemap_t* map) {
    if (!map) return;
    for (size_t i = 256; i < 512; i++) {
        map->pml4[i] = kernel_pagemap.pml4[i];
    }
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

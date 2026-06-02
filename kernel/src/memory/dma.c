#include "../../include/memory/dma.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/vmm.h"
#include "../../include/memory/paging.h"
#include "../../include/io/serial.h"
#include <string.h>

void *dma_alloc_coherent(size_t size, uintptr_t *phys_out) {
    if (size == 0) return NULL;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void *v = pmm_alloc_zero(pages);
    if (!v) {
        serial_printf("[dma] alloc_coherent failed: %zu pages\n", pages);
        return NULL;
    }
    if (phys_out) *phys_out = pmm_virt_to_phys(v);
    return v;
}

void dma_free_coherent(void *virt, size_t size) {
    if (!virt || size == 0) return;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    pmm_free(virt, pages);
}

#define DMA_LOW_LIMIT   0x100000000ULL

void *dma_alloc_coherent_low(size_t size, uintptr_t *phys_out) {
    if (size == 0) return NULL;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    void *v = pmm_alloc_below(pages, DMA_LOW_LIMIT);
    if (!v) {
        serial_printf("[dma] low alloc failed: %zu pages (<4GB exhausted)\n", pages);
        return NULL;
    }
    memset(v, 0, pages * PAGE_SIZE);
    if (phys_out) *phys_out = pmm_virt_to_phys(v);
    return v;
}

volatile void *mmio_map(uintptr_t phys, size_t size) {
    if (size == 0) return NULL;

    uintptr_t phys_base = phys & ~(uintptr_t)0xFFFULL;
    uintptr_t phys_end  = (phys + size + 0xFFF) & ~(uintptr_t)0xFFFULL;
    size_t    pages     = (phys_end - phys_base) >> 12;

    uintptr_t hhdm = pmm_get_hhdm_offset();
    uintptr_t virt_base = phys_base + hhdm;

    vmm_pagemap_t *km = vmm_get_kernel_pagemap();
    if (!km) return NULL;

    for (size_t i = 0; i < pages; i++) {
        uintptr_t va = virt_base + (i << 12);
        uintptr_t pa = phys_base + (i << 12);

        uintptr_t already;
        if (vmm_virt_to_phys(km, va, &already)) {
            if (already != pa) {
                serial_printf("[mmio] phys 0x%llx already mapped to 0x%llx via HHDM\n",
                              (unsigned long long)pa, (unsigned long long)already);
                return NULL;
            }
            continue;
        }
        if (!vmm_map_page(km, va, pa,
                          VMM_PRESENT | VMM_WRITE | VMM_NOEXEC | VMM_PCD | VMM_PWT)) {
            serial_printf("[mmio] map_page failed for phys 0x%llx\n",
                          (unsigned long long)pa);
            return NULL;
        }
    }

    return (volatile void *)(uintptr_t)(phys + hhdm);
}

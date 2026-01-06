#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define VMM_PRESENT    (1ULL << 0)
#define VMM_WRITE      (1ULL << 1)
#define VMM_USER       (1ULL << 2)
#define VMM_NOEXEC     (1ULL << 63)

typedef uint64_t vmm_pte_t;

typedef struct {
    vmm_pte_t* pml4;
} vmm_pagemap_t;

void vmm_init(void);
vmm_pagemap_t* vmm_create_pagemap(void);
void vmm_switch_pagemap(vmm_pagemap_t* map);

bool vmm_map_page(vmm_pagemap_t* map, uintptr_t virt, uintptr_t phys, uint64_t flags);
void vmm_unmap_page(vmm_pagemap_t* map, uintptr_t virt);

bool vmm_virt_to_phys(vmm_pagemap_t* map, uintptr_t virt, uintptr_t* phys_out);
bool vmm_get_page_flags(vmm_pagemap_t* map, uintptr_t virt, uint64_t* flags_out);

vmm_pagemap_t* vmm_get_kernel_pagemap(void);

void vmm_test(void);

#endif
#include "../../include/elf/elf.h"
#include "../../include/memory/vmm.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/paging.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>

#define ELF_PIE_BASE        0x0000000000400000ULL
#define ELF_USER_STACK_TOP  0x00007FFFFFFFE000ULL
#define ELF_DEFAULT_STACK   (64 * 1024)

#define PML4_KERNEL_START   256
#define PML4_ENTRIES        512

static inline uintptr_t page_align_down(uintptr_t addr) {
    return addr & ~(uintptr_t)(PAGE_SIZE - 1);
}
static inline uintptr_t page_align_up(uintptr_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
}
static uint64_t phdr_flags_to_vmm(uint32_t pf) {
    uint64_t flags = VMM_PRESENT | VMM_USER;
    if (pf & PF_W)    flags |= VMM_WRITE;
    if (!(pf & PF_X)) flags |= VMM_NOEXEC;
    return flags;
}

static void inherit_kernel_pml4(vmm_pagemap_t* dst) {
    vmm_pagemap_t* kmap     = vmm_get_kernel_pagemap();
    vmm_pte_t*     src_pml4 = kmap->pml4;
    vmm_pte_t*     dst_pml4 = dst->pml4;

    for (int i = PML4_KERNEL_START; i < PML4_ENTRIES; i++) {
        dst_pml4[i] = src_pml4[i];
    }

    serial_printf("[ELF] Kernel PML4[%d..%d] inherited\n",
                  PML4_KERNEL_START, PML4_ENTRIES - 1);
}

static elf_error_t elf_validate(const elf64_ehdr_t* hdr, size_t size) {
    if (size < sizeof(elf64_ehdr_t))          return ELF_ERR_TOO_SMALL;

    uint32_t magic;
    memcpy(&magic, hdr->e_ident, 4);
    if (magic != ELF_MAGIC)                   return ELF_ERR_BAD_MAGIC;
    if (hdr->e_ident[EI_CLASS]   != ELFCLASS64)  return ELF_ERR_NOT_64;
    if (hdr->e_ident[EI_DATA]    != ELFDATA2LSB) return ELF_ERR_NOT_LE;
    if (hdr->e_ident[EI_VERSION] != EV_CURRENT ||
        hdr->e_version           != EV_CURRENT)  return ELF_ERR_BAD_VERSION;
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) return ELF_ERR_NOT_EXEC;
    if (hdr->e_machine != EM_X86_64)          return ELF_ERR_WRONG_ARCH;
    if (hdr->e_phnum == 0)                    return ELF_ERR_NO_LOAD;

    uint64_t pht_end = (uint64_t)hdr->e_phoff +
                       (uint64_t)hdr->e_phentsize * hdr->e_phnum;
    if (pht_end > size)                       return ELF_ERR_TOO_SMALL;
    return ELF_OK;
}

static elf_error_t load_segment(vmm_pagemap_t*      map,
                                const uint8_t*      data,
                                size_t              file_size,
                                const elf64_phdr_t* phdr,
                                uintptr_t           load_bias)
{
    if (phdr->p_memsz == 0) return ELF_OK;

    uintptr_t virt_start = phdr->p_vaddr + load_bias;
    uintptr_t virt_end   = virt_start + phdr->p_memsz;
    uintptr_t page_start = page_align_down(virt_start);
    uintptr_t page_end   = page_align_up(virt_end);
    size_t    page_count = (page_end - page_start) / PAGE_SIZE;

    void* phys_pages = pmm_alloc_zero(page_count);
    if (!phys_pages) {
        serial_printf("[ELF] pmm_alloc_zero(%zu) failed for vaddr 0x%llx\n",
                      page_count, virt_start);
        return ELF_ERR_NO_MEM;
    }

    uint8_t*  mapped_virt = (uint8_t*)pmm_phys_to_virt(pmm_virt_to_phys(phys_pages));
    size_t    page_offset = virt_start - page_start;

    if (phdr->p_filesz > 0) {
        if (phdr->p_offset + phdr->p_filesz > file_size) {
            serial_printf("[ELF] Segment data out of file bounds\n");
            pmm_free(phys_pages, page_count);
            return ELF_ERR_TOO_SMALL;
        }
        memcpy(mapped_virt + page_offset, data + phdr->p_offset, phdr->p_filesz);
    }

    uint64_t  vmm_flags = phdr_flags_to_vmm(phdr->p_flags);
    uintptr_t phys_base = pmm_virt_to_phys(phys_pages);

    for (size_t i = 0; i < page_count; i++) {
        uintptr_t virt = page_start + i * PAGE_SIZE;
        uintptr_t phys = phys_base  + i * PAGE_SIZE;
        if (!vmm_map_page(map, virt, phys, vmm_flags)) {
            serial_printf("[ELF] vmm_map_page failed: virt=0x%llx\n", virt);
            for (size_t j = 0; j < i; j++)
                vmm_unmap_page(map, page_start + j * PAGE_SIZE);
            pmm_free(phys_pages, page_count);
            return ELF_ERR_MAP_FAIL;
        }
    }

    serial_printf("[ELF] Segment loaded: virt=0x%llx-0x%llx flags=%s%s%s "
                 "phys=0x%llx pages=%zu\n",
                 virt_start, virt_end,
                 (phdr->p_flags & PF_R) ? "R" : "-",
                 (phdr->p_flags & PF_W) ? "W" : "-",
                 (phdr->p_flags & PF_X) ? "X" : "-",
                 phys_base, page_count);
    return ELF_OK;
}

static uintptr_t alloc_user_stack(vmm_pagemap_t* map, size_t stack_size) {
    size_t    page_count = page_align_up(stack_size) / PAGE_SIZE;
    void*     phys       = pmm_alloc_zero(page_count);
    if (!phys) {
        serial_printf("[ELF] Stack alloc failed (%zu pages)\n", page_count);
        return 0;
    }

    uintptr_t stack_bottom = ELF_USER_STACK_TOP - page_count * PAGE_SIZE;
    uintptr_t phys_base    = pmm_virt_to_phys(phys);
    uint64_t  flags        = VMM_PRESENT | VMM_WRITE | VMM_USER | VMM_NOEXEC;

    for (size_t i = 0; i < page_count; i++) {
        if (!vmm_map_page(map, stack_bottom + i * PAGE_SIZE,
                               phys_base    + i * PAGE_SIZE, flags)) {
            serial_printf("[ELF] Stack map failed at page %zu\n", i);
            for (size_t j = 0; j < i; j++)
                vmm_unmap_page(map, stack_bottom + j * PAGE_SIZE);
            pmm_free(phys, page_count);
            return 0;
        }
    }

    serial_printf("[ELF] Stack: virt=0x%llx-0x%llx (%zu KiB)\n",
                 stack_bottom, ELF_USER_STACK_TOP,
                 (page_count * PAGE_SIZE) / 1024);

    return ELF_USER_STACK_TOP & ~(uintptr_t)0xF;
}

elf_load_result_t elf_load(const void* data, size_t size, size_t stack_sz) {
    elf_load_result_t result = {0};

    if (!data) { result.error = ELF_ERR_NULL; return result; }

    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*)data;
    result.error = elf_validate(ehdr, size);
    if (result.error != ELF_OK) {
        serial_printf("[ELF] Validation failed: %s\n", elf_strerror(result.error));
        return result;
    }

    uintptr_t load_bias = (ehdr->e_type == ET_DYN) ? ELF_PIE_BASE : 0;
    if (load_bias) serial_printf("[ELF] PIE binary, bias=0x%llx\n", load_bias);

    vmm_pagemap_t* map = vmm_create_pagemap();
    if (!map) {
        serial_printf("[ELF] vmm_create_pagemap() failed\n");
        result.error = ELF_ERR_NO_MEM;
        return result;
    }
    result.pagemap = map;

    inherit_kernel_pml4(map);

    const uint8_t*      bytes = (const uint8_t*)data;
    const elf64_phdr_t* phdrs = (const elf64_phdr_t*)(bytes + ehdr->e_phoff);
    bool has_load = false;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        has_load = true;

        serial_printf("[ELF] PT_LOAD[%u]: off=0x%llx vaddr=0x%llx "
                     "filesz=0x%llx memsz=0x%llx flags=0x%x\n",
                     i, ph->p_offset, ph->p_vaddr,
                     ph->p_filesz, ph->p_memsz, ph->p_flags);

        elf_error_t err = load_segment(map, bytes, size, ph, load_bias);
        if (err != ELF_OK) { result.error = err; return result; }
    }

    if (!has_load) { result.error = ELF_ERR_NO_LOAD; return result; }

    result.entry     = ehdr->e_entry + load_bias;
    result.load_base = load_bias;
    serial_printf("[ELF] Entry point: 0x%llx\n", result.entry);

    if (stack_sz == 0) stack_sz = ELF_DEFAULT_STACK;
    result.stack_size = stack_sz;
    result.stack_top  = alloc_user_stack(map, stack_sz);
    if (result.stack_top == 0) { result.error = ELF_ERR_NO_MEM; return result; }

    serial_printf("[ELF] Load complete. entry=0x%llx stack_top=0x%llx\n",
                 result.entry, result.stack_top);
    return result;
}

void elf_unload(elf_load_result_t* result) {
    if (!result) return;
    if (result->pagemap) {
        free(result->pagemap);
        result->pagemap = NULL;
    }
    serial_printf("[ELF] Process unloaded.\n");
}

const char* elf_strerror(elf_error_t err) {
    switch (err) {
        case ELF_OK:              return "OK";
        case ELF_ERR_NULL:        return "NULL pointer";
        case ELF_ERR_TOO_SMALL:   return "file too small / data out of bounds";
        case ELF_ERR_BAD_MAGIC:   return "not an ELF file (bad magic)";
        case ELF_ERR_NOT_64:      return "not ELF64";
        case ELF_ERR_NOT_LE:      return "not little-endian";
        case ELF_ERR_BAD_VERSION: return "unsupported ELF version";
        case ELF_ERR_NOT_EXEC:    return "not an executable or shared object";
        case ELF_ERR_WRONG_ARCH:  return "not x86_64";
        case ELF_ERR_NO_LOAD:     return "no PT_LOAD segments";
        case ELF_ERR_MAP_FAIL:    return "vmm_map_page failed";
        case ELF_ERR_NO_MEM:      return "out of memory";
        default:                  return "unknown error";
    }
}
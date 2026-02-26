#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../memory/vmm.h"
#include "../memory/paging.h"

#define ELF_MAGIC       0x464C457F

#define EI_CLASS        4
#define EI_DATA         5
#define EI_VERSION      6
#define EI_OSABI        7
#define EI_NIDENT       16

#define ELFCLASS64      2
#define ELFDATA2LSB     1
#define EV_CURRENT      1

#define ET_EXEC         2
#define ET_DYN          3

#define EM_X86_64       62

#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_PHDR         6
#define PT_TLS          7

#define PF_X            (1 << 0)
#define PF_W            (1 << 1)
#define PF_R            (1 << 2)

#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_NOBITS      8

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} __attribute__((packed)) elf64_shdr_t;

typedef enum {
    ELF_OK = 0,
    ELF_ERR_NULL,
    ELF_ERR_TOO_SMALL,
    ELF_ERR_BAD_MAGIC,
    ELF_ERR_NOT_64,
    ELF_ERR_NOT_LE,
    ELF_ERR_BAD_VERSION,
    ELF_ERR_NOT_EXEC,
    ELF_ERR_WRONG_ARCH,
    ELF_ERR_NO_LOAD,
    ELF_ERR_MAP_FAIL,
    ELF_ERR_NO_MEM,
} elf_error_t;

typedef struct {
    uintptr_t       entry;
    vmm_pagemap_t*  pagemap;
    uintptr_t       load_base;
    uintptr_t       stack_top;
    size_t          stack_size;
    elf_error_t     error;
} elf_load_result_t;

elf_load_result_t elf_load(const void* data, size_t size, size_t stack_sz);

void elf_unload(elf_load_result_t* result);

const char* elf_strerror(elf_error_t err);

#endif
#ifndef _KERNEL_BOOT_BOOT_INFO_H
#define _KERNEL_BOOT_BOOT_INFO_H

#include <stdint.h>

#define BOOT_MMAP_MAX 160
#define BOOT_MOD_MAX  8
#define BOOT_CPU_MAX  64

typedef enum {
    BOOT_MEM_USABLE = 0,
    BOOT_MEM_RESERVED,
    BOOT_MEM_ACPI_RECLAIMABLE,
    BOOT_MEM_ACPI_NVS,
    BOOT_MEM_BAD,
    BOOT_MEM_BOOTLOADER_RECLAIMABLE,
    BOOT_MEM_KERNEL,
    BOOT_MEM_FRAMEBUFFER,
} boot_mem_type_t;

typedef struct {
    uint64_t         base;
    uint64_t         length;
    boot_mem_type_t  type;
} boot_mmap_entry_t;

typedef struct {
    uint64_t     addr;
    uint64_t     size;
    const char  *path;
} boot_module_t;

typedef struct {
    uint32_t lapic_id;
    uint8_t  is_bsp;
} boot_cpu_t;

typedef struct {
    uint64_t hhdm_offset;

    struct {
        uint64_t addr;
        uint32_t width;
        uint32_t height;
        uint32_t pitch;
        uint16_t bpp;
    } fb;

    uint64_t rsdp_addr;

    int               mmap_count;
    boot_mmap_entry_t mmap[BOOT_MMAP_MAX];

    int               module_count;
    boot_module_t     modules[BOOT_MOD_MAX];

    uint32_t          bsp_lapic_id;
    int               cpu_count;
    boot_cpu_t        cpus[BOOT_CPU_MAX];
} boot_info_t;

const boot_info_t *boot_info(void);

#endif

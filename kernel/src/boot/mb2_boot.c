#include "../../include/boot/boot_info.h"
#include "../../include/io/serial.h"
#include <stdint.h>
#include <string.h>

#define MB2_LOADER_MAGIC 0x36d76289u
#define HHDM             0xffff800000000000ULL

extern void kmain(void);

static uint32_t rd32(const void *p) { return *(const uint32_t *)p; }
static uint64_t rd64(const void *p) { return *(const uint64_t *)p; }

void mb2_main(uint32_t magic, uint32_t info_phys) {
    serial_initialize(0x3F8, 115200);
    if (magic != MB2_LOADER_MAGIC) {
        serial_writestring("[mb2] bad multiboot2 magic, halting\n");
        for (;;) asm volatile("cli; hlt");
    }

    boot_info_t *bi = boot_info_mut();
    memset(bi, 0, sizeof *bi);
    bi->hhdm_offset = HHDM;

    const uint8_t *info = (const uint8_t *)(uintptr_t)info_phys;
    uint32_t total = rd32(info);
    const uint8_t *end = info + total;
    const uint8_t *p = info + 8;

    while (p + 8 <= end) {
        uint32_t type = rd32(p);
        uint32_t size = rd32(p + 4);
        if (type == 0) break;
        const uint8_t *d = p + 8;

        switch (type) {
        case 6: {
            uint32_t esz = rd32(d);
            const uint8_t *e = d + 8;
            int n = 0;
            while (e + esz <= p + size && n < BOOT_MMAP_MAX) {
                uint64_t base = rd64(e);
                uint64_t len  = rd64(e + 8);
                uint32_t mt   = rd32(e + 16);
                boot_mem_type_t t = (mt == 1) ? BOOT_MEM_USABLE
                                  : (mt == 3) ? BOOT_MEM_ACPI_RECLAIMABLE
                                  : (mt == 4) ? BOOT_MEM_ACPI_NVS
                                  : (mt == 5) ? BOOT_MEM_BAD
                                  :             BOOT_MEM_RESERVED;
                bi->mmap[n].base = base;
                bi->mmap[n].length = len;
                bi->mmap[n].type = t;
                n++;
                e += esz;
            }
            bi->mmap_count = n;
            break;
        }
        case 8: {
            uint64_t fba = rd64(d);
            bi->fb.pitch  = rd32(d + 8);
            bi->fb.width  = rd32(d + 12);
            bi->fb.height = rd32(d + 16);
            bi->fb.bpp    = d[20];
            bi->fb.addr   = fba + HHDM;
            break;
        }
        case 3: {
            if (bi->module_count < BOOT_MOD_MAX) {
                uint32_t ms = rd32(d), me = rd32(d + 4);
                int k = bi->module_count++;
                bi->modules[k].addr = (uint64_t)ms + HHDM;
                bi->modules[k].size = (uint64_t)(me - ms);
                bi->modules[k].path = (const char *)((uintptr_t)(d + 8) + HHDM);
            }
            break;
        }
        case 14:
        case 15: {
            if (!bi->rsdp_addr) bi->rsdp_addr = (uint64_t)(uintptr_t)d + HHDM;
            break;
        }
        }

        p += (size + 7u) & ~7u;
    }

    bi->cpu_count = 1;
    bi->cpus[0].lapic_id = 0;
    bi->cpus[0].is_bsp = 1;
    bi->bsp_lapic_id = 0;

    serial_printf("[mb2] fb=%ux%u pitch=%u mmap=%d mods=%d rsdp=0x%llx hhdm=0x%llx\n",
                  bi->fb.width, bi->fb.height, bi->fb.pitch, bi->mmap_count,
                  bi->module_count, (unsigned long long)bi->rsdp_addr,
                  (unsigned long long)bi->hhdm_offset);

    kmain();
}

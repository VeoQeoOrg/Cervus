#include "../../include/boot/boot_info.h"
#include "../../include/io/serial.h"
#include <stdint.h>
#include <string.h>

#define MB2_LOADER_MAGIC 0x36d76289u
#define HHDM             0xffff800000000000ULL
#define RSV_MAX          16

extern void    kmain(void);
extern uint8_t mb_pml4[];
extern char    kernel_lma_end[];

__attribute__((aligned(64))) char mb_boot_stack[65536];

static uint32_t rd32(const void *p) { return *(const uint32_t *)p; }
static uint64_t rd64(const void *p) { return *(const uint64_t *)p; }

static uint64_t g_rsv_lo[RSV_MAX], g_rsv_hi[RSV_MAX];
static int      g_nrsv;

static void rsv_add(uint64_t base, uint64_t len) {
    if (len == 0 || g_nrsv >= RSV_MAX) return;
    uint64_t lo = base & ~0xFFFULL;
    uint64_t hi = (base + len + 0xFFF) & ~0xFFFULL;
    int i = g_nrsv;
    while (i > 0 && g_rsv_lo[i - 1] > lo) {
        g_rsv_lo[i] = g_rsv_lo[i - 1];
        g_rsv_hi[i] = g_rsv_hi[i - 1];
        i--;
    }
    g_rsv_lo[i] = lo;
    g_rsv_hi[i] = hi;
    g_nrsv++;
}

static void mmap_add_usable(boot_info_t *bi, uint64_t b, uint64_t e) {
    if (e <= b || bi->mmap_count >= BOOT_MMAP_MAX) return;
    bi->mmap[bi->mmap_count].base   = b;
    bi->mmap[bi->mmap_count].length = e - b;
    bi->mmap[bi->mmap_count].type   = BOOT_MEM_USABLE;
    bi->mmap_count++;
}

static void mmap_add_carved(boot_info_t *bi, uint64_t b, uint64_t e) {
    uint64_t cur = b;
    for (int i = 0; i < g_nrsv; i++) {
        uint64_t rb = g_rsv_lo[i] > b ? g_rsv_lo[i] : b;
        uint64_t re = g_rsv_hi[i] < e ? g_rsv_hi[i] : e;
        if (rb >= re) continue;
        if (cur < rb) mmap_add_usable(bi, cur, rb);
        if (re > cur) cur = re;
    }
    if (cur < e) mmap_add_usable(bi, cur, e);
}

void mb2_main(uint32_t magic, uint32_t info_phys) {
    serial_initialize(0x3F8, 115200);
    if (magic != MB2_LOADER_MAGIC) {
        serial_writestring("[mb2] bad multiboot2 magic, halting\n");
        for (;;) asm volatile("cli; hlt");
    }

    boot_info_t *bi = boot_info_mut();
    memset(bi, 0, sizeof *bi);
    bi->hhdm_offset = HHDM;

    const uint8_t *info = (const uint8_t *)((uintptr_t)info_phys + HHDM);
    uint32_t total = rd32(info);
    const uint8_t *end = info + total;
    const uint8_t *mmap_tag = 0;

    rsv_add(0x100000, (uint64_t)(uintptr_t)kernel_lma_end - 0x100000);
    rsv_add((uint64_t)info_phys, total);

    for (const uint8_t *p = info + 8; p + 8 <= end; p += (rd32(p + 4) + 7u) & ~7u) {
        uint32_t type = rd32(p);
        if (type == 0) break;
        const uint8_t *d = p + 8;
        switch (type) {
        case 6:
            mmap_tag = p;
            break;
        case 8:
            bi->fb.addr   = rd64(d) + HHDM;
            bi->fb.pitch  = rd32(d + 8);
            bi->fb.width  = rd32(d + 12);
            bi->fb.height = rd32(d + 16);
            bi->fb.bpp    = d[20];
            break;
        case 3:
            if (bi->module_count < BOOT_MOD_MAX) {
                uint32_t ms = rd32(d), me = rd32(d + 4);
                int k = bi->module_count++;
                bi->modules[k].addr = (uint64_t)ms + HHDM;
                bi->modules[k].size = (uint64_t)(me - ms);
                bi->modules[k].path = (const char *)(d + 8);
                rsv_add(ms, (uint64_t)(me - ms));
            }
            break;
        case 14:
        case 15:
            if (!bi->rsdp_addr) bi->rsdp_addr = (uint64_t)(uintptr_t)d;
            break;
        }
    }

    if (mmap_tag) {
        uint32_t size = rd32(mmap_tag + 4);
        uint32_t esz  = rd32(mmap_tag + 8);
        const uint8_t *e = mmap_tag + 16;
        while (e + esz <= mmap_tag + size && bi->mmap_count < BOOT_MMAP_MAX) {
            uint64_t base = rd64(e);
            uint64_t len  = rd64(e + 8);
            uint32_t mt   = rd32(e + 16);
            if (mt == 1) {
                mmap_add_carved(bi, base, base + len);
            } else {
                boot_mem_type_t t = (mt == 3) ? BOOT_MEM_ACPI_RECLAIMABLE
                                  : (mt == 4) ? BOOT_MEM_ACPI_NVS
                                  : (mt == 5) ? BOOT_MEM_BAD
                                  :             BOOT_MEM_RESERVED;
                bi->mmap[bi->mmap_count].base   = base;
                bi->mmap[bi->mmap_count].length = len;
                bi->mmap[bi->mmap_count].type   = t;
                bi->mmap_count++;
            }
            e += esz;
        }
    }

    bi->cpu_count = 1;
    bi->cpus[0].lapic_id = 0;
    bi->cpus[0].is_bsp = 1;
    bi->bsp_lapic_id = 0;

    serial_printf("[mb2] fb=%ux%u mmap=%d mods=%d rsdp=0x%llx rsv=%d\n",
                  bi->fb.width, bi->fb.height, bi->mmap_count,
                  bi->module_count, (unsigned long long)bi->rsdp_addr, g_nrsv);

    kmain();
}

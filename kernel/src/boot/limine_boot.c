#include "../../include/boot/boot_info.h"
#include "../../include/boot/limine_boot.h"
#include "../../include/io/serial.h"
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID, .revision = 0, .flags = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0, .response = NULL
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0
};

extern void kernel_main(void);
__attribute__((used, section(".limine_requests")))
static volatile struct limine_entry_point_request entry_point_request = {
    .id = LIMINE_ENTRY_POINT_REQUEST_ID, .revision = 0, .entry = kernel_main
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

struct limine_mp_response *limine_mp(void) { return mp_request.response; }

static void bhcf(void) { for (;;) asm volatile("hlt"); }

static boot_mem_type_t map_type(uint64_t t) {
    switch (t) {
        case LIMINE_MEMMAP_USABLE:                 return BOOT_MEM_USABLE;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return BOOT_MEM_ACPI_RECLAIMABLE;
        case LIMINE_MEMMAP_ACPI_NVS:               return BOOT_MEM_ACPI_NVS;
        case LIMINE_MEMMAP_BAD_MEMORY:             return BOOT_MEM_BAD;
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return BOOT_MEM_BOOTLOADER_RECLAIMABLE;
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return BOOT_MEM_KERNEL;
        case LIMINE_MEMMAP_FRAMEBUFFER:            return BOOT_MEM_FRAMEBUFFER;
        default:                                   return BOOT_MEM_RESERVED;
    }
}

void boot_init(void) {
    boot_info_t *bi = boot_info_mut();
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        serial_writestring("ERROR: Unsupported Limine base revision\n");
        bhcf();
    }
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1) {
        serial_writestring("ERROR: No framebuffer available\n");
        bhcf();
    }
    if (!memmap_request.response) {
        serial_writestring("ERROR: No memory map available\n");
        bhcf();
    }
    if (!hhdm_request.response) {
        serial_writestring("ERROR: No HHDM available\n");
        bhcf();
    }

    bi->hhdm_offset = hhdm_request.response->offset;

    struct limine_framebuffer *f = framebuffer_request.response->framebuffers[0];
    bi->fb.addr   = (uint64_t)(uintptr_t)f->address;
    bi->fb.width  = (uint32_t)f->width;
    bi->fb.height = (uint32_t)f->height;
    bi->fb.pitch  = (uint32_t)f->pitch;
    bi->fb.bpp    = f->bpp;

    if (rsdp_request.response && rsdp_request.response->address)
        bi->rsdp_addr = (uint64_t)(uintptr_t)rsdp_request.response->address;

    struct limine_memmap_response *mm = memmap_request.response;
    int n = 0;
    for (uint64_t i = 0; i < mm->entry_count && n < BOOT_MMAP_MAX; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        bi->mmap[n].base   = e->base;
        bi->mmap[n].length = e->length;
        bi->mmap[n].type   = map_type(e->type);
        n++;
    }
    bi->mmap_count = n;

    struct limine_module_response *mods = module_request.response;
    if (mods) {
        int m = 0;
        for (uint64_t i = 0; i < mods->module_count && m < BOOT_MOD_MAX; i++) {
            struct limine_file *mf = mods->modules[i];
            bi->modules[m].addr = (uint64_t)(uintptr_t)mf->address;
            bi->modules[m].size = mf->size;
            bi->modules[m].path = mf->path;
            m++;
        }
        bi->module_count = m;
    }

    struct limine_mp_response *mp = mp_request.response;
    if (mp) {
        bi->bsp_lapic_id = mp->bsp_lapic_id;
        int c = 0;
        for (uint64_t i = 0; i < mp->cpu_count && c < BOOT_CPU_MAX; i++) {
            bi->cpus[c].lapic_id = mp->cpus[i]->lapic_id;
            bi->cpus[c].is_bsp   = (mp->cpus[i]->lapic_id == mp->bsp_lapic_id);
            c++;
        }
        bi->cpu_count = c;
    }

    serial_printf("[boot] boot_info: hhdm=0x%llx fb=%ux%u@%ubpp rsdp=0x%llx mmap=%d mods=%d cpus=%d\n",
                  (unsigned long long)bi->hhdm_offset,
                  bi->fb.width, bi->fb.height, bi->fb.bpp,
                  (unsigned long long)bi->rsdp_addr,
                  bi->mmap_count, bi->module_count, bi->cpu_count);
}

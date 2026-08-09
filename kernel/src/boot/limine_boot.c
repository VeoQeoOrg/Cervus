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

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static boot_info_t g_bi;

const boot_info_t *boot_info(void) { return &g_bi; }

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

    g_bi.hhdm_offset = hhdm_request.response->offset;

    struct limine_framebuffer *f = framebuffer_request.response->framebuffers[0];
    g_bi.fb.addr   = (uint64_t)(uintptr_t)f->address;
    g_bi.fb.width  = (uint32_t)f->width;
    g_bi.fb.height = (uint32_t)f->height;
    g_bi.fb.pitch  = (uint32_t)f->pitch;
    g_bi.fb.bpp    = f->bpp;

    if (rsdp_request.response && rsdp_request.response->address)
        g_bi.rsdp_addr = (uint64_t)(uintptr_t)rsdp_request.response->address;

    struct limine_memmap_response *mm = memmap_request.response;
    int n = 0;
    for (uint64_t i = 0; i < mm->entry_count && n < BOOT_MMAP_MAX; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        g_bi.mmap[n].base   = e->base;
        g_bi.mmap[n].length = e->length;
        g_bi.mmap[n].type   = map_type(e->type);
        n++;
    }
    g_bi.mmap_count = n;

    struct limine_module_response *mods = module_request.response;
    if (mods) {
        int m = 0;
        for (uint64_t i = 0; i < mods->module_count && m < BOOT_MOD_MAX; i++) {
            struct limine_file *mf = mods->modules[i];
            g_bi.modules[m].addr = (uint64_t)(uintptr_t)mf->address;
            g_bi.modules[m].size = mf->size;
            g_bi.modules[m].path = mf->path;
            m++;
        }
        g_bi.module_count = m;
    }

    serial_printf("[boot] boot_info: hhdm=0x%llx fb=%ux%u@%ubpp rsdp=0x%llx mmap=%d mods=%d\n",
                  (unsigned long long)g_bi.hhdm_offset,
                  g_bi.fb.width, g_bi.fb.height, g_bi.fb.bpp,
                  (unsigned long long)g_bi.rsdp_addr,
                  g_bi.mmap_count, g_bi.module_count);
}

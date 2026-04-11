#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limine.h>
#include "../include/graphics/fb/fb.h"
#include "../include/io/serial.h"
#include "../include/gdt/gdt.h"
#include "../include/interrupts/interrupts.h"
#include "../include/interrupts/idt.h"
#include "../include/sse/fpu.h"
#include "../include/sse/sse.h"
#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/memory/paging.h"
#include "../include/acpi/acpi.h"
#include "../include/apic/apic.h"
#include "../include/io/ports.h"
#include "../include/drivers/timer.h"
#include "../include/smp/smp.h"
#include "../include/smp/percpu.h"
#include "../include/sched/sched.h"
#include "../include/elf/elf.h"
#include "../include/syscall/syscall.h"
#include "../include/drivers/ps2.h"
#include "../include/fs/vfs.h"
#include "../include/fs/ramfs.h"
#include "../include/fs/devfs.h"
#include "../include/fs/initramfs.h"
#include "../include/drivers/ata.h"
#include "../include/drivers/blkdev.h"
#include "../include/drivers/disk.h"
#include "../include/fs/cervusfs.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .flags = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

struct limine_framebuffer *global_framebuffer = NULL;

static void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}

static void load_elf_module(void) {
    if (!module_request.response) {
        serial_writestring("[ELF] No module response from Limine\n");
        return;
    }
    if (module_request.response->module_count == 0) {
        serial_writestring("[ELF] No modules provided\n");
        return;
    }

    struct limine_file *mod = module_request.response->modules[0];
    serial_printf("[ELF] Module: path='%s' size=%llu addr=%p\n", mod->path, mod->size, mod->address);

    elf_load_result_t r = elf_load(mod->address, (size_t)mod->size, 0);
    if (r.error != ELF_OK) {
        serial_printf("[ELF] LOAD FAILED: %s\n", elf_strerror(r.error));
        return;
    }

    serial_printf("[ELF] Load OK! entry=0x%llx stack_top=0x%llx\n", r.entry, r.stack_top);

    uint64_t cr3 = (uint64_t)pmm_virt_to_phys(r.pagemap->pml4);

    task_t *t = task_create_user("init", r.entry, r.stack_top, cr3, 16, r.pagemap, 0, 0);

    if (!t) {
        serial_writestring("[ELF] task_create_user failed\n");
        elf_unload(&r);
        return;
    }

    t->brk_start   = r.load_end;
    t->brk_current = r.load_end;

    int sr = vfs_init_stdio(t);
    if (sr < 0)
        serial_printf("[VFS] vfs_init_stdio failed: %d\n", sr);
    else
        serial_writestring("[VFS] stdio assigned to task\n");

    serial_printf("[ELF] Task 'init' created: cr3=0x%llx\n", t->cr3);
}

void ps2_task(void* arg) {
    (void)arg;
    asm volatile ("sti");
    ps2_init();
}

void kernel_main(void) {
    serial_initialize(COM1, 115200);
    serial_writestring("\n=== SERIAL PORT INITIALIZED ===\n");

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        serial_writestring("ERROR: Unsupported Limine base revision\n");
        hcf();
    }

    gdt_init();
    init_interrupt_system();
    serial_writestring("GDT&IDT [OK]\n");
    fpu_init();
    sse_init();
    enable_fsgsbase();
    serial_writestring("FPU/SSE/FSGSBASE [OK]\n");

    if (!framebuffer_request.response ||
        framebuffer_request.response->framebuffer_count < 1) {
        serial_writestring("ERROR: No framebuffer available\n");
        hcf();
    }
    if (!memmap_request.response) {
        serial_writestring("ERROR: No memory map available\n");
        hcf();
    }
    if (!hhdm_request.response) {
        serial_writestring("ERROR: No HHDM available\n");
        hcf();
    }

    global_framebuffer = framebuffer_request.response->framebuffers[0];

    pmm_init(memmap_request.response, hhdm_request.response);
    slab_init();
    serial_writestring("PMM [OK]\n");
    paging_init();
    serial_writestring("Paging [OK]\n");
    vmm_init();
    serial_writestring("VMM [OK]\n");
    vfs_init();
    serial_writestring("VFS [OK]\n");

    vnode_t *rootfs = ramfs_create_root();
    vfs_mount("/", rootfs);
    vnode_unref(rootfs);
    serial_writestring("rootfs [OK]\n");

    vfs_mkdir("/dev",  0755);
    vfs_mkdir("/bin",  0755);
    vfs_mkdir("/etc",  0755);
    vfs_mkdir("/tmp",  0755);
    vfs_mkdir("/proc", 0755);

    vnode_t *devroot = devfs_create_root();
    vfs_mount("/dev", devroot);
    serial_writestring("devfs [OK]\n");
    acpi_init();
    acpi_print_tables();
    serial_writestring("ACPI [OK]\n");
    apic_init();
    serial_writestring("APIC [OK]\n");
    clear_screen();
    smp_init(mp_request.response);
    serial_writestring("SMP [OK]\n");

    serial_writestring("Waiting until all APs are fully ready...\n");
    while (smp_get_online_count() < (smp_get_cpu_count() - 1)) {
        timer_sleep_ms(100);
    }
    serial_writestring("All APs ready.\n");

    printf("Kernel initialized successfully!\n\n");
    printf("Framebuffer: %dx%d, %d bpp\n", global_framebuffer->width, global_framebuffer->height, global_framebuffer->bpp);
    printf("\nMemory Information:\n");
    printf("HHDM offset: 0x%llx\n", hhdm_request.response->offset);
    printf("Memory map entries: %llu\n", memmap_request.response->entry_count);
    pmm_print_stats();

    smp_print_info_fb();
    printf("\nSystem: %u CPU cores detected\n", smp_get_cpu_count());
    syscall_init();

    if (module_request.response &&
        module_request.response->module_count >= 2) {
        struct limine_file *tar = module_request.response->modules[1];
        serial_printf("[initramfs] module: '%s' size=%llu\n", tar->path, tar->size);
        int r = initramfs_mount(tar->address, (size_t)tar->size);
        if (r == 0)
            serial_writestring("[initramfs] mounted OK\n");
        else
            serial_printf("[initramfs] mount FAILED: %d\n", r);
    } else {
        serial_writestring("[initramfs] no TAR module (modules[1] missing)\n");
    }

    disk_init();

    if (ata_get_drive_count() > 0) {
        vfs_mkdir("/mnt", 0755);
        disk_mount("hda", "/mnt");

        vnode_t *hcheck = NULL;
        if (vfs_lookup("/mnt/home", &hcheck) == 0) {
            vnode_unref(hcheck);
        } else {
            int hr = vfs_mkdir("/mnt/home", 0755);
            if (hr == 0)
                serial_writestring("[disk] created /mnt/home\n");
            else if (hr != -EEXIST)
                serial_printf("[disk] WARNING: /mnt/home creation failed: %d\n", hr);
        }
    }
    serial_writestring("Disk subsystem [OK]\n");

    timer_init();
    sched_init();
    sched_notify_ready();
    timer_sleep_ms(10);
    clear_screen();
    load_elf_module();
    task_create("PS/2", ps2_task, NULL, 10);
    serial_writestring("Manually triggering first reschedule...\n");
    sched_reschedule();

    while (1) {
        hcf();
    }
}
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
#include "../include/fs/procfs.h"
#include "../include/fs/initramfs.h"
#include "../include/drivers/disk/ata.h"
#include "../include/drivers/disk/blkdev.h"
#include "../include/drivers/disk/disk.h"
#include "../include/drivers/disk/partition.h"
#include "../include/drivers/pci.h"
#include "../include/drivers/disk/nvme.h"
#include "../include/drivers/usb/xhci.h"
#include "../include/drivers/usb/ehci.h"
#include "../include/drivers/usb/uhci.h"
#include "../include/console/console.h"
#include "../include/fs/ext2.h"
#include "../include/fs/fat32.h"

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

    uintptr_t init_rsp = elf_build_init_stack(r.pagemap, r.stack_top);
    if (init_rsp == 0) {
        serial_writestring("[ELF] init stack build failed\n");
        return;
    }

    uint64_t cr3 = (uint64_t)pmm_virt_to_phys(r.pagemap->pml4);

    task_t *t = task_create_user("init", r.entry, init_rsp, cr3, 16, r.pagemap, 0, 0);

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

    xhci_start_worker();
    ehci_start_worker();
    uhci_start_worker();
    disk_start_media_worker();
    timer_start_recal_task();

    console_boot_logging_off();
}

void ps2_task(void* arg) {
    (void)arg;
    asm volatile ("sti");
    ps2_init();
}

static const char *const ROOT_DISK_PREFIXES[] = { "nvme0n1", "sda", "hda" };
#define ROOT_DISK_PREFIX_COUNT (sizeof(ROOT_DISK_PREFIXES) / sizeof(ROOT_DISK_PREFIXES[0]))

static void make_part_name(char *out, size_t out_size, const char *pfx, int part) {
    size_t nlen = strlen(pfx);
    const char *sep = "";
    if (nlen > 0 && pfx[nlen - 1] >= '0' && pfx[nlen - 1] <= '9') sep = "p";
    snprintf(out, out_size, "%s%s%d", pfx, sep, part);
}

static bool blkdev_has_ext2(blkdev_t *dev) {
    if (!dev) return false;
    uint16_t magic = 0;
    if (blkdev_read(dev, EXT2_SUPER_OFFSET + 56, &magic, sizeof(magic)) != 0) return false;
    return magic == EXT2_SUPER_MAGIC;
}

static bool find_installed_root(char *out, size_t out_cap) {
    for (size_t k = 0; k < ROOT_DISK_PREFIX_COUNT; k++) {
        const char *pfx = ROOT_DISK_PREFIXES[k];
        char name[16];

        make_part_name(name, sizeof(name), pfx, 2);
        if (blkdev_has_ext2(blkdev_get_by_name(name))) {
            serial_printf("[boot] ext2 on %s -> installed system\n", name);
            snprintf(out, out_cap, "%s", name);
            return true;
        }

        make_part_name(name, sizeof(name), pfx, 1);
        blkdev_t *p1 = blkdev_get_by_name(name);
        if (p1) {
            uint8_t sector[512];
            if (p1->ops->read_sectors(p1, 0, 1, sector) == 0
                && sector[510] == 0x55 && sector[511] == (uint8_t)0xAA
                && memcmp(sector + 82, "FAT32", 5) == 0) {
                serial_printf("[boot] FAT32 ESP on %s, looking for ext2 sibling\n", name);
                char rname[16];
                make_part_name(rname, sizeof(rname), pfx, 2);
                if (blkdev_has_ext2(blkdev_get_by_name(rname))) {
                    snprintf(out, out_cap, "%s", rname);
                    return true;
                }
            }
        }

        blkdev_t *whole = blkdev_get_by_name(pfx);
        if (whole && whole->sector_count > 0 && blkdev_has_ext2(whole)) {
            serial_printf("[boot] legacy ext2-on-whole-disk on %s detected\n", pfx);
            snprintf(out, out_cap, "%s", pfx);
            return true;
        }
    }
    return false;
}

void kernel_main(void) {
    serial_initialize(COM1, 115200);
    serial_writestring("\nCervus serial console ready\n");

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
    fb_init_backbuffer(global_framebuffer);
    vt_init();
    vfs_init();
    serial_writestring("VFS [OK]\n");

    vnode_t *rootfs = ramfs_create_root();
    vfs_mount("/", rootfs);
    vfs_set_mount_info("/", "ramfs", "ramfs");
    vnode_unref(rootfs);
    serial_writestring("rootfs [OK]\n");

    vfs_mkdir("/dev",  0755);
    vfs_mkdir("/bin",  0755);
    vfs_mkdir("/etc",  0755);
    vfs_mkdir("/tmp",  0755);
    vfs_mkdir("/proc", 0755);
    vfs_mkdir("/mnt",  0755);

    vnode_t *devroot = devfs_create_root();
    vfs_mount("/dev", devroot);
    vfs_set_mount_info("/dev", "devfs", "devfs");
    serial_writestring("devfs [OK]\n");

    vnode_t *procroot = procfs_create_root();
    vfs_mount("/proc", procroot);
    vfs_set_mount_info("/proc", "procfs", "procfs");
    serial_writestring("procfs [OK]\n");
    acpi_init();
    acpi_print_tables();
    serial_writestring("ACPI [OK]\n");
    apic_init();
    serial_writestring("APIC [OK]\n");
    pci_init();
    serial_writestring("PCI [OK]\n");
    clear_screen();
    smp_init(mp_request.response);
    serial_writestring("SMP [OK]\n");

    serial_writestring("Waiting until all APs are fully ready...\n");
    {
        int waited_ms = 0;
        uint32_t need = (smp_get_cpu_count() > 0) ? (smp_get_cpu_count() - 1) : 0;
        while (smp_get_online_count() < need && waited_ms < 5000) {
            timer_sleep_ms(100);
            waited_ms += 100;
        }
        if (smp_get_online_count() < need) {
            serial_printf("[SMP] Timeout waiting for APs: %u/%u online, continuing\n",
                          smp_get_online_count(), need);
        } else {
            serial_writestring("All APs ready.\n");
        }
    }

    printf("Cervus/x86_64 kernel booting\n");
    printf("framebuffer: %dx%d %d bpp\n",
           global_framebuffer->width, global_framebuffer->height,
           global_framebuffer->bpp);
    pmm_print_stats();
    printf("hhdm offset 0x%llx, %llu memory map entries\n",
           (unsigned long long)hhdm_request.response->offset,
           (unsigned long long)memmap_request.response->entry_count);

    smp_print_info_fb();
    printf("%u cpu%s configured\n", smp_get_cpu_count(),
           smp_get_cpu_count() == 1 ? "" : "s");

    syscall_init();
    disk_init();

    xhci_init();
    ehci_init();
    uhci_init();
    {
        int xn = xhci_controller_count();
        int en = ehci_controller_count();
        int un = uhci_controller_count();
        if (xn) printf("usb: xhci, %d controller%s\n", xn, xn == 1 ? "" : "s");
        if (en) printf("usb: ehci, %d controller%s\n", en, en == 1 ? "" : "s");
        if (un) printf("usb: uhci, %d controller%s\n", un, un == 1 ? "" : "s");
        if (!xn && !en && !un) printf("usb: no controllers detected\n");
    }

    bool has_initramfs_module = (module_request.response &&
                                  module_request.response->module_count >= 2);
    static char root_name[16] = {0};
    bool skip_initramfs = false;
    if (has_initramfs_module) {
        serial_writestring("[boot] initramfs module present -- ISO boot, not auto-mounting disk\n");
    } else {
        skip_initramfs = find_installed_root(root_name, sizeof(root_name));
        if (skip_initramfs) {
            printf("root: %s (installed system)\n", root_name);
        } else {
            serial_writestring("[boot] no installed system detected, using initramfs\n");
        }
    }

    if (skip_initramfs) {
        int ur = vfs_umount("/");
        serial_printf("[boot] umount(/) -> %d\n", ur);
        int mr = disk_mount(root_name, "/");
        serial_printf("[boot] disk_mount(%s,/) -> %d\n", root_name, mr);
        if (mr == 0) {
            vfs_mkdir("/dev",  0755);
            vfs_mkdir("/tmp",  0755);
            vfs_mkdir("/proc", 0755);
            vfs_mkdir("/mnt",  0755);
        } else {
            vnode_t *fallback = ramfs_create_root();
            vfs_mount("/", fallback);
            vfs_set_mount_info("/", "ramfs", "ramfs");
            vnode_unref(fallback);
            vfs_mkdir("/dev",  0755);
            vfs_mkdir("/bin",  0755);
            vfs_mkdir("/etc",  0755);
            vfs_mkdir("/tmp",  0755);
            vfs_mkdir("/proc", 0755);
            vfs_mkdir("/mnt",  0755);
        }
    }

    if (!skip_initramfs && module_request.response &&
        module_request.response->module_count >= 2) {
        struct limine_file *tar = module_request.response->modules[1];
        serial_printf("[initramfs] module: '%s' size=%llu\n", tar->path, tar->size);
        int r = initramfs_mount(tar->address, (size_t)tar->size);
        if (r == 0) {
            printf("root: initramfs (%llu KiB)\n",
                   (unsigned long long)(tar->size / 1024));
            serial_writestring("[initramfs] mounted OK\n");
        } else {
            printf("root: initramfs mount failed (%d)\n", r);
            serial_printf("[initramfs] mount FAILED: %d\n", r);
        }
    } else if (!skip_initramfs) {
        serial_writestring("[initramfs] no TAR module (modules[1] missing)\n");
    }

    timer_init();
    serial_writestring("Initializing PS/2 keyboard/mouse...\n");
    ps2_init();

    sched_init();
    sched_notify_ready();
    timer_sleep_ms(10);
    printf("starting init...\n\n");
    load_elf_module();
    serial_writestring("Manually triggering first reschedule...\n");
    sched_reschedule();

    while (1) {
        hcf();
    }
}
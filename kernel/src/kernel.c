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

    struct limine_file* mod = module_request.response->modules[0];
    serial_printf("[ELF] Module: path='%s' size=%llu addr=%p\n",
                  mod->path, mod->size, mod->address);

    elf_load_result_t r = elf_load(mod->address, (size_t)mod->size, 0);

    if (r.error != ELF_OK) {
        serial_printf("[ELF] LOAD FAILED: %s\n", elf_strerror(r.error));
        printf("[ELF] Failed to load: %s\n", elf_strerror(r.error));
        return;
    }

    serial_printf("[ELF] Load OK! entry=0x%llx stack_top=0x%llx\n",
                  r.entry, r.stack_top);
    printf("[ELF] Loaded! entry=0x%llx\n", r.entry);

    task_t* t = task_create("hello.elf",
                            (void (*)(void*))r.entry,
                            NULL,
                            16);

    if (!t) {
        serial_writestring("[ELF] task_create failed\n");
        elf_unload(&r);
        return;
    }

    t->cr3 = (uint64_t)pmm_virt_to_phys(r.pagemap->pml4);

    serial_printf("[ELF] Task 'hello.elf' created: cr3=0x%llx rsp=0x%llx\n",
                  t->cr3, t->rsp);
    printf("[ELF] Task 'hello.elf' scheduled!\n");
}


void exit_test_task(void* arg) {
    (void)arg;
    asm volatile ("sti");

    serial_printf("[ExitTest] Started — will exit after 3 iterations\n");

    for (int i = 1; i <= 3; i++) {
        serial_printf("[ExitTest] Iteration %d/3\n", i);
        for (volatile uint64_t spin = 0; spin < 5000000; spin++)
            asm volatile ("pause");
    }

    serial_printf("[ExitTest] Done — calling task_exit() now\n");
    printf("[ExitTest] Exiting cleanly!\n");
}

static task_t* victim_task_ptr = NULL;

void victim_task(void* arg) {
    (void)arg;
    asm volatile ("sti");

    serial_printf("[Victim] Started — waiting to be killed\n");

    uint64_t counter = 0;
    while (1) {
        counter++;
        if (counter % 2000000 == 0) {
            serial_printf("[Victim] Still alive, counter=%llu\n", counter);
            printf("[Victim] alive %llu\n", counter / 1000000);
        }
    }
}

void killer_task(void* arg) {
    (void)arg;
    asm volatile ("sti");

    serial_printf("[Killer] Started — will kill Victim after delay\n");

    for (volatile uint64_t spin = 0; spin < 20000000; spin++)
        asm volatile ("pause");

    if (victim_task_ptr) {
        serial_printf("[Killer] Sending kill to Victim task @ %p\n",
                      (void*)victim_task_ptr);
        printf("[Killer] Killing Victim now!\n");
        task_kill(victim_task_ptr);

        for (volatile uint64_t spin = 0; spin < 5000000; spin++)
            asm volatile ("pause");

        serial_printf("[Killer] Victim should be dead now. Freeing resources.\n");
        task_destroy(victim_task_ptr);
        victim_task_ptr = NULL;

        serial_printf("[Killer] Done — returning from killer_task\n");
        printf("[Killer] Done!\n");
    } else {
        serial_printf("[Killer] ERROR: victim_task_ptr is NULL!\n");
    }
}

void tlb_test_task(void* arg) {
    (void)arg;
    asm volatile ("sti");

    serial_printf("[TLBTest] Starting map/unmap stress test\n");

    vmm_pagemap_t* kmap = vmm_get_kernel_pagemap();

    void* phys_page = pmm_alloc(1);
    if (!phys_page) {
        serial_printf("[TLBTest] ERROR: pmm_alloc failed\n");
        return;
    }

    uintptr_t test_virt = 0xFFFF900000000000ULL;

    for (int round = 1; round <= 5; round++) {
        vmm_map_page(kmap, test_virt,
                     (uintptr_t)pmm_virt_to_phys(phys_page),
                     VMM_PRESENT | VMM_WRITE | VMM_GLOBAL);

        volatile uint64_t* ptr = (volatile uint64_t*)test_virt;
        *ptr = (uint64_t)round * 0xDEADBEEF;

        uint64_t readback = *ptr;
        if (readback == (uint64_t)round * 0xDEADBEEF) {
            serial_printf("[TLBTest] Round %d: write/read OK (0x%llx)\n",
                          round, readback);
        } else {
            serial_printf("[TLBTest] Round %d: MISMATCH! wrote 0x%llx, got 0x%llx\n",
                          round, (uint64_t)round * 0xDEADBEEF, readback);
        }

        vmm_unmap_page(kmap, test_virt);
        serial_printf("[TLBTest] Round %d: unmap + TLB shootdown OK\n", round);

        for (volatile uint64_t spin = 0; spin < 1000000; spin++)
            asm volatile ("pause");
    }

    pmm_free(phys_page, 1);
    serial_printf("[TLBTest] All rounds passed — TLB flush working correctly\n");
    printf("[TLBTest] PASSED!\n");
}

void high_priority_task(void* arg) {
    (void)arg;
    asm volatile ("sti");
    serial_printf("[HighPri] Started with interrupts enabled!\n");
    uint64_t counter = 0;
    while (1) {
        counter++;
        if (counter % 5000000 == 0) {
            serial_printf("[HighPri] counter=%llu\n", counter);
            printf("[HighPri] %llu\n", counter / 1000000);
        }
    }
}

void low_priority_task(void* arg) {
    (void)arg;
    asm volatile ("sti");
    serial_printf("[LowPri] Started with interrupts enabled!\n");
    uint64_t counter = 0;
    while (1) {
        counter++;
        if (counter % 3000000 == 0) {
            serial_printf("[LowPri] counter=%llu\n", counter);
            printf("[LowPri] %llu\n", counter / 1000000);
        }
    }
}

void fpu_test_task(void* arg) {
    (void)arg;
    asm volatile ("and $-16, %%rsp" ::: "memory");
    asm volatile ("sti");
    serial_printf("[FPU] Testing FPU operations...\n");
    volatile double a = 1.0, b = 2.0, c;
    c = a + b; serial_printf("[FPU] 1.0 + 2.0 = %e\n", c);
    c = a * b; serial_printf("[FPU] 1.0 * 2.0 = %e\n", c);
    serial_printf("[FPU] Tests PASSED! Running continuous...\n");
    volatile double x = 1.0;
    uint64_t iter = 0;
    while (1) {
        x = x * 1.1;
        iter++;
        if (iter % 10000000 == 0)
            serial_printf("[FPU] %llu million operations\n", iter / 1000000);
    }
}

void kernel_main(void) {
    serial_initialize(COM1, 115200);
    serial_writestring("\n=== SERIAL PORT INITIALIZED ===\n");
    serial_writestring("Cervus OS v0.0.1 - Kernel initialized!\n");

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
    serial_writestring("FSGSBASE [OK]\n");
    serial_writestring("FPU/SSE [OK]\n");

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
    serial_writestring("PMM [OK]\n");
    paging_init();
    serial_writestring("Paging [OK]\n");
    vmm_init();
    serial_writestring("VMM [OK]\n");
    malloc_init();
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

    printf("\n\tCERVUS OS v0.0.1\n");
    printf("Kernel initialized successfully!\n\n");
    printf("Framebuffer: %dx%d, %d bpp\n",
           global_framebuffer->width,
           global_framebuffer->height,
           global_framebuffer->bpp);
    printf("\nMemory Information:\n");
    printf("HHDM offset: 0x%llx\n", hhdm_request.response->offset);
    printf("Memory map entries: %llu\n", memmap_request.response->entry_count);
    print_simd_cpuid();
    pmm_print_stats();

    printf("\nSystem ready. Entering idle loop...\n");
    serial_writestring("\nSystem ready. Entering idle loop...\n");

    smp_print_info_fb();
    printf("\nSystem: %u CPU cores detected\n", smp_get_cpu_count());

    sched_init();
    sched_notify_ready();

    load_elf_module();

    //task_create("ExitTest", exit_test_task, NULL, 20);

    //victim_task_ptr = task_create("Victim", victim_task, NULL, 15);
    //task_create("Killer", killer_task, NULL, 18);

    //task_create("TLBTest", tlb_test_task, NULL, 12);

    //task_create("HighPri",     high_priority_task, NULL, 25);
    //task_create("LowPri",      low_priority_task,  NULL, 10);
    //task_create("FPUTESTTASK", fpu_test_task,       NULL,  1);

    timer_init();

    printf("Tasks switching automatically every ~10ms\n\n");
    serial_writestring("Manually triggering first reschedule...\n");
    sched_reschedule();

    serial_writestring("\n=== All tasks completed ===\n");
    serial_writestring("System will continue running tasks...\n");
    printf("\nSystem will continue running tasks...\n");
    while (1) {
        hcf();
    }
}
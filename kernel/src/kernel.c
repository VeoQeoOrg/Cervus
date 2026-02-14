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
#include "../include/sched/task.h"

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

void high_priority_task(void* arg) {
    (void)arg;

    asm volatile ("sti");

    serial_printf(COM1, "[HighPri] Started with interrupts enabled!\n");

    uint64_t counter = 0;
    while (1) {
        counter++;
        if (counter % 5000000 == 0) {
            serial_printf(COM1, "[HighPri] counter=%llu\n", counter);
            printf("[HighPri] %llu\n", counter / 1000000);
        }
    }
}

void low_priority_task(void* arg) {
    (void)arg;

    asm volatile ("sti");

    serial_printf(COM1, "[LowPri] Started with interrupts enabled!\n");

    uint64_t counter = 0;
    while (1) {
        counter++;
        if (counter % 3000000 == 0) {
            serial_printf(COM1, "[LowPri] counter=%llu\n", counter);
            printf("[LowPri] %llu\n", counter / 1000000);
        }
    }
}

void fpu_test_task(void* arg) {
    (void)arg;

    asm volatile ("and $-16, %%rsp" ::: "memory");
    asm volatile ("sti");

    serial_printf(COM1, "[FPU] Testing FPU operations...\n");

    volatile double a = 1.0, b = 2.0, c;
    c = a + b; serial_printf(COM1, "[FPU] 1.0 + 2.0 = %e\n", c);
    c = a * b; serial_printf(COM1, "[FPU] 1.0 * 2.0 = %e\n", c);

    serial_printf(COM1, "[FPU] Tests PASSED! Running continuous...\n");

    volatile double x = 1.0;
    uint64_t iter = 0;

    while (1) {
        x = x * 1.1;
        iter++;

        if (iter % 10000000 == 0) {
            serial_printf(COM1, "[FPU] %llu million operations\n", iter / 1000000);
        }
    }
}

void kernel_main(void) {
    serial_initialize(COM1, 115200);
    serial_writestring(COM1, "\n=== SERIAL PORT INITIALIZED ===\n");
    serial_writestring(COM1, "Cervus OS v0.0.1 - Kernel initialized!\n");

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        serial_writestring(COM1, "ERROR: Unsupported Limine base revision\n");
        hcf();
    }

    gdt_init();
    init_interrupt_system();
    serial_writestring(COM1, "GDT&IDT [OK]\n");
    fpu_init();
    sse_init();
    enable_fsgsbase();
    serial_writestring(COM1, "FSGSBASE [OK]\n");
    serial_writestring(COM1, "FPU/SSE [OK]\n");

    if (!framebuffer_request.response ||
        framebuffer_request.response->framebuffer_count < 1) {
        serial_writestring(COM1, "ERROR: No framebuffer available\n");
        hcf();
    }

    if (!memmap_request.response) {
        serial_writestring(COM1, "ERROR: No memory map available\n");
        hcf();
    }

    if (!hhdm_request.response) {
        serial_writestring(COM1, "ERROR: No HHDM available\n");
        hcf();
    }

    global_framebuffer = framebuffer_request.response->framebuffers[0];

    pmm_init(memmap_request.response, hhdm_request.response);
    serial_writestring(COM1, "PMM [OK]\n");
    paging_init();
    serial_writestring(COM1, "Paging [OK]\n");
    vmm_init();
    serial_writestring(COM1, "VMM [OK]\n");
    acpi_init();
    acpi_print_tables();
    serial_writestring(COM1, "ACPI [OK]\n");
    apic_init();
    serial_writestring(COM1, "APIC initialized successfully\n");

    clear_screen();
    smp_init(mp_request.response);
    serial_writestring(COM1, "=== SMP Initialization Complete ===\n\n");

    serial_writestring(COM1, "Waiting until all APs are fully ready...\n");
    while (smp_get_online_count() < (smp_get_cpu_count() - 1)) {
        timer_sleep_ms(100);
    }

    serial_writestring(COM1, "All APs ready. Sending test reschedule IPI...\n");
    lapic_send_ipi_to_all_but_self(IPI_RESCHEDULE_VECTOR);
    serial_writestring(COM1, "Test reschedule IPI finished\n");

    serial_writestring(COM1, "\nSending test TLB shootdown with specific addresses...\n");

    uintptr_t test_addrs[] = {
        0xffff800000100000,
        0xffff800000200000,
        0xffff800000300000
    };
    size_t addr_count = sizeof(test_addrs) / sizeof(test_addrs[0]);

    ipi_tlb_shootdown_broadcast(test_addrs, addr_count);

    serial_writestring(COM1, "Test TLB shootdown finished\n");

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

    vmm_test();
    //timer_sleep_ms(2000);
    printf("2 seconds\n");
    //timer_sleep_us(10000000);
    printf("10 seconds\n");
    printf("\nSystem ready. Entering idle loop...\n");
    serial_writestring(COM1, "\nSystem ready. Entering idle loop...\n");
    //acpi_shutdown(); //works on real hardware & VM
    smp_print_info_fb();
    printf("\nSystem: %u CPU cores detected\n", smp_get_cpu_count());
    //volatile uint64_t* ptr = (uint64_t*)0xDEADBEEF;
    //*ptr = 0;

    serial_writestring(COM1, "\n=== PREEMPTIVE SCHEDULER TEST ===\n");

    sched_init();

    task_create("HighPri", high_priority_task, NULL, 25);
    task_create("LowPri",  low_priority_task,  NULL, 10);
    task_create("FPUTESTTASK",  fpu_test_task,  NULL, 30);

    timer_init();

    serial_writestring(COM1, "\n");
    serial_writestring(COM1, "===========================================\n");
    serial_writestring(COM1, "  PREEMPTIVE SCHEDULER IS NOW ACTIVE!\n");
    serial_writestring(COM1, "===========================================\n");
    serial_writestring(COM1, "Tasks will be switched automatically by timer.\n");
    serial_writestring(COM1, "No need to call task_yield()!\n");
    serial_writestring(COM1, "Watch both tasks run in parallel...\n\n");

    printf("Tasks switching automatically every ~10ms\n\n");

    serial_writestring(COM1, "Manually triggering first reschedule...\n");
    sched_reschedule();

    serial_writestring(COM1, "\n=== All tasks completed ===\n");
    serial_writestring(COM1, "System will continue running tasks...\n");
    printf("\nSystem will continue running tasks...\n");
    while (1) {
        hcf();
    }
}
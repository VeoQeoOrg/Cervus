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
#include "../include/sse/fpu.h"
#include "../include/sse/sse.h"
#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/memory/paging.h"
#include "../include/acpi/acpi.h"
#include "../include/apic/apic.h"
#include "../include/io/ports.h"

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
static volatile uint64_t timer_ticks = 0;
static volatile bool test_active = true;
static volatile uint64_t messages_sent = 0;

DEFINE_IRQ(0x20, apic_timer_handler) {
    timer_ticks++;
    
    if (test_active && messages_sent < 10) {
        if (timer_ticks % 300 == 0) {
            serial_writestring(COM1, "Hello from APIC HPET Timer!\n");
            printf("Hello from APIC HPET Timer!\n");
            messages_sent++;
            
            if (messages_sent == 10) {
                test_active = false;
                serial_writestring(COM1, "Test completed: 10 messages sent in 30 seconds\n");
                printf("Test completed: 10 messages sent in 30 seconds\n");
            }
        }
    }
    
    lapic_eoi();

    (void)frame;
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
        
    apic_timer_calibrate();
        
    apic_setup_irq(0, 0x20, false, 0);

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    serial_writestring(COM1, "Legacy PIC disabled\n");
    serial_writestring(COM1, "APIC/HPET system ready\n");

    serial_writestring(COM1, "Checking APIC Timer Status\n");
    
    uint32_t timer_config = lapic_read(LAPIC_TIMER);
    uint32_t initial_count = lapic_read(LAPIC_TIMER_ICR);
    uint32_t current_count = lapic_read(LAPIC_TIMER_CCR);
    
    serial_printf(COM1, "APIC Timer Config: 0x%x\n", timer_config);
    serial_printf(COM1, "  Vector: 0x%x\n", timer_config & 0xFF);
    serial_printf(COM1, "  Masked: %s\n", (timer_config & LAPIC_TIMER_MASKED) ? "YES" : "NO");
    serial_printf(COM1, "  Periodic: %s\n", (timer_config & LAPIC_TIMER_PERIODIC) ? "YES" : "NO");
    serial_printf(COM1, "Initial Count: %u\n", initial_count);
    serial_printf(COM1, "Current Count: %u\n", current_count);
    serial_printf(COM1, "Ticks remaining until interrupt: %u\n", current_count);
    
    if (timer_config & LAPIC_TIMER_MASKED) {
        serial_writestring(COM1, "WARNING: APIC timer is masked! Unmasking...\n");
        lapic_write(LAPIC_TIMER, (timer_config & ~LAPIC_TIMER_MASKED));
    }
    
    serial_printf(COM1, "APIC timer should fire in approximately %u ms\n", 
                      (current_count * 10) / 63347);

    clear_screen();
    
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
    
    printf("\nSystem ready. Entering idle loop...\n");
    serial_writestring(COM1, "\nSystem ready. Entering idle loop...\n");
    //acpi_shutdown(); //works on real hardware & VM
    while (1) {
        hcf();
    }
}
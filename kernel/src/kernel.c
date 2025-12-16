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
#include "../include/interrupts/idt.h"
#include "../include/sse/fpu.h"
#include "../include/sse/sse.h"
#include "../include/memory/pmm.h"

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

void kernel_main(void) {
    serial_initialize(COM1, 115200);
    serial_writestring(COM1, "\n=== SERIAL PORT INITIALIZED ===\n");
    serial_writestring(COM1, "Cervus OS v0.0.1 - Kernel initialized!\n");
    
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        serial_writestring(COM1, "ERROR: Unsupported Limine base revision\n");
        hcf();
    }
    
    gdt_init();
    idt_init();
    asm volatile ("sti");
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
    
    clear_screen();
    
    printf("=== CERVUS OS v0.0.1 ===\n");
    printf("Kernel initialized successfully!\n\n");
    
    printf("Framebuffer: %dx%d, %d bpp\n", 
           global_framebuffer->width, 
           global_framebuffer->height,
           global_framebuffer->bpp);
    
    printf("\nMemory Information:\n");
    printf("HHDM offset: 0x%llx\n", hhdm_request.response->offset);
    printf("Memory map entries: %llu\n", memmap_request.response->entry_count);
    
    pmm_print_stats();
    
    printf("\nSystem ready. Entering idle loop...\n");
    serial_writestring(COM1, "\nSystem ready. Entering idle loop...\n");

    while (1) {
        asm volatile ("hlt");
    }
}
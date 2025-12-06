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

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
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
    serial_writestring(COM1, "Kernel initialized successfully!\n");
    gdt_init();
    
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        serial_writestring(COM1, "ERROR: Unsupported Limine base revision\n");
        hcf();
    }

    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        serial_writestring(COM1, "ERROR: No framebuffer available\n");
        hcf();
    }

    global_framebuffer = framebuffer_request.response->framebuffers[0];
    
    serial_printf(COM1, "Framebuffer: %dx%d, %d bpp\n", 
                  global_framebuffer->width, 
                  global_framebuffer->height,
                  global_framebuffer->bpp);
    
    clear_screen();
    
    printf("=== CERVUS OS v0.0.1 ===\n");
    
    hcf(); 
}
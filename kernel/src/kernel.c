#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limine.h>
#include "../include/graphics/fb/fb.h"
#include "../include/io/serial.h"

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
    
    serial_writestring(COM1, "\n=== CERVUS OS v0.0.1 ===\n");
    serial_writestring(COM1, "Testing serial output:\n");
    serial_printf(COM1, "Decimal (42): %d\n", 42);
    serial_printf(COM1, "Decimal negative (-42): %d\n", -42);
    serial_printf(COM1, "Decimal large (1000): %d\n", 1000);
    serial_printf(COM1, "Hex lowercase (0xDEADBEEF): 0x%x\n", 0xDEADBEEF);
    serial_printf(COM1, "Hex uppercase (0xCAFEBABE): 0x%X\n", 0xCAFEBABE);
    serial_printf(COM1, "Hex zero: 0x%x\n", 0x0);
    serial_printf(COM1, "Hex small (0x1A3): 0x%x\n", 0x1A3);
    serial_printf(COM1, "String: %s\n", "Hello from serial!");
    serial_printf(COM1, "Characters: %c%c%c\n", 'A', 'B', 'C');
    serial_printf(COM1, "Unsigned: %u\n", 1234567890U);
    serial_printf(COM1, "Unsigned max (0xFFFFFFFF): %u\n", 0xFFFFFFFFU);
    serial_printf(COM1, "Unsigned zero: %u\n", 0U);
    serial_printf(COM1, "Pointer (0x12345678): %p\n", (void*)0x12345678);
    serial_printf(COM1, "Pointer (NULL): %p\n", NULL);
    serial_printf(COM1, "Pointer (this function): %p\n", (void*)kernel_main);
    serial_printf(COM1, "Mix: %s %d 0x%x %c\n", "Test", 100, 255, '!');
    serial_printf(COM1, "Long decimal (1234567890123): %ld\n", 1234567890123UL);
    serial_printf(COM1, "Long hex (0xDEADBEEFCAFEBABE): 0x%lx\n", 0xDEADBEEFCAFEBABEUL);
    serial_printf(COM1, "Long negative: %ld\n", -1234567890123L);
    serial_printf(COM1, "\nBoundary values:\n");
    serial_printf(COM1, "INT_MAX: %d\n", 2147483647);
    serial_printf(COM1, "INT_MIN: %d\n", -2147483648);
    serial_printf(COM1, "UINT_MAX: %u\n", 4294967295U);
    
    printf("=== CERVUS OS v0.0.1 ===\n");
    printf("Testing printf function:\n");
    printf("Decimal: %d\n", 42);
    printf("Hex (lower): 0x%x\n", 0xDEADBEEF);
    printf("Hex (upper): 0x%X\n", 0xCAFEBABE);
    printf("String: %s\n", "Hello from printf!");
    printf("Character: %c%c%c\n", 'A', 'B', 'C');
    printf("Unsigned: %u\n", 1234567890U);
    printf("Pointer: %p\n", (void*)0x12345678);
    printf("Mix: %s %d 0x%X\n", "Test", 100, 255);
    
    printf("itoa() test\n");
    int input_number = 1245;
    printf("Input number: %d\n", input_number);
    char buffer[16];
    printf("itoa() result [10]: %s\n", itoa(input_number, buffer, 10));
    printf("itoa() result [16]: %s\n", itoa(input_number, buffer, 16));
    printf("itoa() result [2]: %s\n", itoa(input_number, buffer, 2));

    const char *string = "Hello, World! 343434";
    printf("strlen() result: %d\n", (int)strlen(string));

    putchar('\n');
    puts("Testing puts function:");
    puts("This is line 1");
    puts("This is line 2");
    set_text_color(COLOR_CYAN);
    puts("Cyan text");
    set_text_color(COLOR_GREEN);
    printf("Green text: %d\n", 999);
    set_text_color(COLOR_YELLOW);
    puts("Yellow text at the bottom");
    
    serial_writestring(COM1, "\nAll tests completed successfully!\n");
    serial_writestring(COM1, "Kernel initialized successfully!\n");
    
    hcf(); 
}
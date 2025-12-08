#include "../../include/gdt/gdt.h"
#include "../../include/io/serial.h"
#include <stddef.h>

struct gdt_entry gdt[5];
struct gdt_ptr gdt_ptr;
void gdt_load(void);

static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    gdt[index].base_low = base & 0xFFFF;
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;
    
    gdt[index].limit_low = limit & 0xFFFF;
    gdt[index].granularity = (limit >> 16) & 0x0F;
    
    gdt[index].granularity |= granularity & 0xF0;
    gdt[index].access = access;
}

void gdt_init(void) {
    serial_writestring(COM1, "[GDT] Initializing GDT...\n");
    
    gdt_set_entry(0, 0, 0, 0, 0);
    
    gdt_set_entry(1, 0, 0xFFFFF, 
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SEGMENT | GDT_ACCESS_EXECUTABLE | GDT_ACCESS_READ_WRITE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_LONG);
    
    gdt_set_entry(2, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SEGMENT | GDT_ACCESS_READ_WRITE,
                  GDT_GRANULARITY_4K);
    
    gdt_set_entry(3, 0, 0, 0, 0);
    
    gdt_set_entry(4, 0, 0, 0, 0);
    
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;
    
    serial_writestring(COM1, "[GDT] GDT entries set up\n");
    gdt_load();
}

void gdt_load(void) {
    serial_writestring(COM1, "[GDT] Loading GDT...\n");
    
    asm volatile("lgdt %0" : : "m"(gdt_ptr));
    
    asm volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        : : : "rax", "memory"
    );
    
    serial_writestring(COM1, "[GDT] GDT loaded successfully\n");
}

const struct gdt_entry* gdt_get_descriptor(int index) {
    if (index >= 0 && index < 5) {
        return &gdt[index];
    }
    return NULL;
}
#include "../../include/gdt/gdt.h"
#include "../../include/io/serial.h"
#include <string.h>

#define KERNEL_STACK_SIZE 0x4000
#define IST_STACK_SIZE    0x4000

static struct gdt_entry gdt_entries[7] __attribute__((aligned(8)));
static struct gdt_ptr gdt_ptr;

static struct tss kernel_tss;

static uint64_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
static uint64_t ist1_stack[IST_STACK_SIZE] __attribute__((aligned(16)));

extern void gdt_flush(struct gdt_ptr*);

static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[index].base_low     = base & 0xFFFF;
    gdt_entries[index].base_middle  = (base >> 16) & 0xFF;
    gdt_entries[index].base_high    = (base >> 24) & 0xFF;

    gdt_entries[index].limit_low    = limit & 0xFFFF;
    gdt_entries[index].granularity  = (limit >> 16) & 0x0F;
    gdt_entries[index].granularity |= gran & 0xF0;
    gdt_entries[index].access       = access;
}

static void tss_init(void) {
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.rsp0    = (uint64_t)kernel_stack + sizeof(kernel_stack);
    kernel_tss.ist[0]  = (uint64_t)ist1_stack + sizeof(ist1_stack);
    kernel_tss.iomap_base = sizeof(kernel_tss);
}

static void gdt_set_tss(void) {
    uint64_t base  = (uint64_t)&kernel_tss;
    uint32_t limit = sizeof(kernel_tss) - 1;

    struct tss_descriptor* tss_desc = (struct tss_descriptor*)&gdt_entries[5];

    memset(tss_desc, 0, sizeof(*tss_desc));

    tss_desc->limit_low   = limit & 0xFFFF;
    tss_desc->base_low    = base & 0xFFFF;
    tss_desc->base_middle    = (base >> 16) & 0xFF;
    tss_desc->access      = 0x89; 
    tss_desc->granularity = 0;
    tss_desc->base_high   = (base >> 24) & 0xFF;
    tss_desc->base_upper  = (base >> 32) & 0xFFFFFFFF;
    tss_desc->reserved    = 0;
}

static void tss_load(void) {
    asm volatile ("ltr %0" : : "r" ((uint16_t)GDT_TSS));
}

void tss_set_kernel_stack(uint64_t stack_top) {
    kernel_tss.rsp0 = stack_top;
}

void gdt_init(void) {
    serial_writestring(COM1, "[GDT] Initializing GDT + TSS...\n");

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0, 0x9A, 0x20);
    gdt_set_entry(2, 0, 0, 0x92, 0x00);
    gdt_set_entry(3, 0, 0, 0, 0);
    gdt_set_entry(4, 0, 0, 0, 0);

    tss_init();
    gdt_set_tss();

    gdt_ptr.base  = (uint64_t)&gdt_entries;
    gdt_ptr.limit = sizeof(gdt_entries) - 1;

    gdt_flush(&gdt_ptr);
    tss_load();

    serial_writestring(COM1, "[GDT] GDT and TSS loaded successfully\n");
}

#include "../../include/gdt/gdt.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stddef.h>

gdt_pointer_t gdtr;
struct {
    gdt_entry_t gdt_entries[5];
    tss_entry_t tss_entry;
}__attribute__((packed)) gdt;
tss_t tss = {0};

#define KERNEL_STACK_SIZE 4096 * 8

__attribute__((aligned(16)))
char kernel_stack[KERNEL_STACK_SIZE];

__attribute__((aligned(16)))
char def_ist_stack[KERNEL_STACK_SIZE];

__attribute__((aligned(16)))
char df_stack[KERNEL_STACK_SIZE];

__attribute__((aligned(16)))
char nmi_stack[KERNEL_STACK_SIZE];

__attribute__((aligned(16)))
char pf_stack[KERNEL_STACK_SIZE];

extern void _load_gdt(gdt_pointer_t *descriptor);
extern void _reload_segments(uint64_t cs, uint64_t ds);

void gdt_init() {
    gdt.gdt_entries[0] = (gdt_entry_t)GDT_ENTRY(0, 0, 0, 0);
    gdt.gdt_entries[1] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0x9A, 0xA);
    gdt.gdt_entries[2] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0x92, 0xC);
    gdt.gdt_entries[3] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0xFA, 0xA);
    gdt.gdt_entries[4] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0xF2, 0xC);

    gdtr.size = (uint16_t)(sizeof(gdt) - 1);
    gdtr.pointer = (gdt_entry_t *)&gdt;

    tss.rsp0 = (uint64_t)(kernel_stack + KERNEL_STACK_SIZE);
    tss.ist[0] = (uint64_t)(def_ist_stack + KERNEL_STACK_SIZE);
    tss.ist[1] = (uint64_t)(df_stack + KERNEL_STACK_SIZE);
    tss.ist[2] = (uint64_t)(nmi_stack + KERNEL_STACK_SIZE);
    tss.ist[3] = (uint64_t)(pf_stack + KERNEL_STACK_SIZE);

    gdt.tss_entry.limit_low   = sizeof(tss_t) - 1;
    gdt.tss_entry.base_low    = (uint16_t)((uint64_t)&tss & 0xffff);
    gdt.tss_entry.base_middle = (uint8_t)(((uint64_t)&tss >> 16) & 0xff);
    gdt.tss_entry.access      = 0x89;
    gdt.tss_entry.limit_high_and_flags = 0;
    gdt.tss_entry.base_high   = (uint8_t)(((uint64_t)&tss >> 24) & 0xff);
    gdt.tss_entry.base_higher = (uint32_t)((uint64_t)&tss >> 32);
    gdt.tss_entry.zero        = 0;

    serial_printf(COM1, "Installing GDT...\n");

    _load_gdt(&gdtr);

    _reload_segments(GDT_CODE_SEGMENT, GDT_DATA_SEGMENT);

    serial_printf(COM1, "GDT installed successfully\n");
}
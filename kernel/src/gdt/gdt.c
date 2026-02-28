#include "../../include/gdt/gdt.h"
#include "../../include/smp/smp.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stddef.h>

gdt_pointer_t gdtr;

struct {
    gdt_entry_t gdt_entries[5 + (MAX_CPUS * 2)];
} __attribute__((packed)) gdt;

tss_t *tss[MAX_CPUS] = {0};
__attribute__((aligned(16))) char kernel_stacks[MAX_CPUS][KERNEL_STACK_SIZE];
__attribute__((aligned(16))) char def_ist_stacks[MAX_CPUS][KERNEL_STACK_SIZE];
__attribute__((aligned(16))) char df_stacks[MAX_CPUS][KERNEL_STACK_SIZE];
__attribute__((aligned(16))) char nmi_stacks[MAX_CPUS][KERNEL_STACK_SIZE];
__attribute__((aligned(16))) char pf_stacks[MAX_CPUS][KERNEL_STACK_SIZE];

extern void _load_gdt(gdt_pointer_t *descriptor);
extern void _reload_segments(uint64_t cs, uint64_t ds);

void gdt_init(void) {
    memset(&gdt, 0, sizeof(gdt));

    gdt.gdt_entries[0] = (gdt_entry_t)GDT_ENTRY(0, 0,       0x00, 0x0);
    gdt.gdt_entries[1] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0x9A, 0xA);
    gdt.gdt_entries[2] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0x92, 0xC);
    gdt.gdt_entries[3] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0xF2, 0xC);
    gdt.gdt_entries[4] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0xFA, 0xA);

    gdtr.size    = (5 * sizeof(gdt_entry_t)) - 1;
    gdtr.pointer = &gdt.gdt_entries[0];

    serial_printf("Installing temporary GDT for BSP...\n");
    gdt_load();
    serial_printf("Temporary GDT installed\n");
}

void gdt_load(void) {
    _load_gdt(&gdtr);
    _reload_segments(GDT_CODE_SEGMENT, GDT_DATA_SEGMENT);
}
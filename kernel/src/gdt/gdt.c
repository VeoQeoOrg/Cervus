#include "../../include/gdt/gdt.h"
#include "../../include/smp/smp.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stddef.h>

gdt_pointer_t gdtr;

__attribute__((aligned(16))) static gdt_entry_t g_boot_gdt[GDT_ENTRIES];
__attribute__((aligned(16))) static gdt_entry_t g_cpu_gdt[MAX_CPUS][GDT_ENTRIES];
static gdt_pointer_t g_cpu_gdtr[MAX_CPUS];

tss_t *tss[MAX_CPUS] = {0};
__attribute__((aligned(16))) char kernel_stacks[MAX_CPUS][KERNEL_STACK_SIZE];
__attribute__((aligned(16))) char def_ist_stacks[MAX_CPUS][KERNEL_STACK_SIZE];
__attribute__((aligned(16))) char df_stacks[MAX_CPUS][KERNEL_STACK_SIZE];
__attribute__((aligned(16))) char nmi_stacks[MAX_CPUS][KERNEL_STACK_SIZE];
__attribute__((aligned(16))) char pf_stacks[MAX_CPUS][KERNEL_STACK_SIZE];

extern void _load_gdt(gdt_pointer_t *descriptor);
extern void _reload_segments(uint64_t cs, uint64_t ds);

static void fill_template(gdt_entry_t *g) {
    memset(g, 0, sizeof(gdt_entry_t) * GDT_ENTRIES);
    g[1] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0x9A, 0xA);
    g[2] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0x92, 0xC);
    g[4] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0xFA, 0xC);
    g[5] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0xF2, 0xC);
    g[6] = (gdt_entry_t)GDT_ENTRY(0, 0xFFFFF, 0xFA, 0xA);
}

void gdt_init(void) {
    fill_template(g_boot_gdt);
    gdtr.size    = (uint16_t)(sizeof(g_boot_gdt) - 1);
    gdtr.pointer = &g_boot_gdt[0];

    serial_printf("Installing temporary GDT for BSP...\n");
    gdt_load();
    serial_printf("Temporary GDT installed\n");
}

void gdt_load(void) {
    _load_gdt(&gdtr);
    _reload_segments(GDT_CODE_SEGMENT, GDT_DATA_SEGMENT);
}

void gdt_setup_cpu(uint32_t cpu, void *t) {
    gdt_entry_t *g = g_cpu_gdt[cpu];
    fill_template(g);
    tss_entry_t *entry = (tss_entry_t *)&g[TSS_SELECTOR >> 3];
    uint64_t addr = (uint64_t)t;
    entry->limit_low            = sizeof(tss_t) - 1;
    entry->base_low             = addr & 0xffff;
    entry->base_middle          = (addr >> 16) & 0xff;
    entry->access               = 0x89;
    entry->limit_high_and_flags = 0;
    entry->base_high            = (addr >> 24) & 0xff;
    entry->base_higher          = addr >> 32;
    entry->zero                 = 0;
    g_cpu_gdtr[cpu].size    = (uint16_t)(sizeof(gdt_entry_t) * GDT_ENTRIES - 1);
    g_cpu_gdtr[cpu].pointer = g;
}

void gdt_load_cpu(uint32_t cpu) {
    _load_gdt(&g_cpu_gdtr[cpu]);
    _reload_segments(GDT_CODE_SEGMENT, GDT_DATA_SEGMENT);
    load_tss(TSS_SELECTOR);
}

uint64_t gdt_cpu_base(uint32_t cpu) {
    return (uint64_t)g_cpu_gdt[cpu];
}

void gdt_set_tls(uint32_t cpu, const uint64_t *tls) {
    if (cpu >= MAX_CPUS) return;
    uint64_t *g = (uint64_t *)g_cpu_gdt[cpu];
    for (int i = 0; i < GDT_TLS_ENTRIES; i++)
        g[GDT_TLS_FIRST + i] = tls ? tls[i] : 0;
}

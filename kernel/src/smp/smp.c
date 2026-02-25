#include "../../include/smp/smp.h"
#include "../../include/smp/percpu.h"
#include "../../include/acpi/acpi.h"
#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/vmm.h"
#include "../../include/gdt/gdt.h"
#include "../../include/interrupts/idt.h"
#include "../../include/sse/fpu.h"
#include "../../include/sse/sse.h"
#include "../../include/sched/sched.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

extern tss_t *tss[MAX_CPUS];
extern struct {
    gdt_entry_t gdt_entries[5 + (MAX_CPUS * 2)];
} __attribute__((packed)) gdt;

static smp_info_t smp_info = {0};
static volatile uint32_t ap_online_count = 0;
tlb_shootdown_t tlb_shootdown_queue[MAX_CPUS] = {0};

volatile uint32_t sched_ready_flag = 0;

void sched_notify_ready(void) {
    __sync_synchronize();
    sched_ready_flag = 1;
    __sync_synchronize();
    serial_writestring("[SCHED] Scheduler ready, notifying all APs\n");
}

__attribute__((used))
void ap_entry_init(struct limine_mp_info* cpu_info) {
    (void)cpu_info;
    asm volatile ("cli");

    lapic_write(0xF0, 0);

    gdt_load();
    idt_load();

    uint32_t lapic_id = lapic_get_id();
    smp_info_t* info = smp_get_info();
    uint32_t my_index = 0;
    for (uint32_t i = 0; i < info->cpu_count; i++) {
        if (info->cpus[i].lapic_id == lapic_id && !info->cpus[i].is_bsp) {
            my_index = i;
            info->cpus[i].state = CPU_ONLINE;
            break;
        }
    }
    load_tss(info->cpus[my_index].tss_selector);
    serial_printf("TSS Loaded (selector 0x%x)\n", info->cpus[my_index].tss_selector);

    fpu_init();
    sse_init();
    enable_fsgsbase();
    lapic_enable();

    apic_timer_calibrate();
    serial_printf("[SMP] AP %u LAPIC timer started\n", lapic_id);

    cpu_info_t* cpu = smp_get_current_cpu();
    percpu_t* region = percpu_regions[cpu->cpu_index];
    set_percpu_base(region);
    serial_printf("PerCPU base set for AP %u: 0x%llx\n",
                  lapic_id, (uint64_t)region);

    __sync_fetch_and_add(&ap_online_count, 1);
    lapic_eoi();

    serial_printf("[SMP] AP (LAPIC ID %u) initialized and online!\n", lapic_id);

    while (!sched_ready_flag)
        asm volatile ("pause");

    serial_printf("[SMP] AP %u entering scheduler loop\n", lapic_id);

    asm volatile ("sti");

    sched_reschedule();

    while (1)
        asm volatile ("hlt");
}

void ap_entry_point(struct limine_mp_info* cpu_info) {
    uint64_t stack_top;
    asm volatile (
        "mov 24(%%rdi), %0"
        : "=r"(stack_top)
        : "D"(cpu_info)
    );
    asm volatile (
        "mov %0, %%rsp\n"
        "cli\n"
        "jmp ap_entry_init\n"
        :
        : "r"(stack_top), "D"(cpu_info)
        : "memory"
    );
}

static uint64_t smp_allocate_stack(uint32_t cpu_index, size_t stack_size) {
    size_t pages = (stack_size + PAGE_SIZE - 1) / PAGE_SIZE;
    void* stack_pages = pmm_alloc(pages);
    if (!stack_pages) {
        serial_printf("[SMP WARNING] Failed to allocate stack for CPU %u\n", cpu_index);
        return 0;
    }
    uint64_t stack_virt = (uint64_t)stack_pages + stack_size;
    serial_printf("[SMP] Allocated stack for CPU %u at 0x%llx (size %zu)\n",
                  cpu_index, stack_virt, stack_size);
    return stack_virt;
}

void smp_boot_aps(struct limine_mp_response* mp_response) {
    if (!mp_response) {
        serial_writestring("[SMP ERROR] MP response is NULL\n");
        return;
    }

    serial_writestring("\n[SMP] Booting Application Processors \n");

    smp_info_t* info = smp_get_info();
    uint32_t bsp_lapic_id = info->bsp_lapic_id;
    uint32_t ap_count = 0;

    for (uint64_t i = 0; i < mp_response->cpu_count; i++) {
        struct limine_mp_info* cpu = mp_response->cpus[i];
        if (cpu->lapic_id == bsp_lapic_id) continue;

        uint64_t stack_top = smp_allocate_stack(i, AP_STACK_SIZE);
        if (stack_top == 0) {
            serial_printf("[SMP] Skipping CPU %u (stack alloc failed)\n", i);
            info->cpus[i].state = CPU_FAULTED;
            continue;
        }

        info->cpus[i].stack_top = stack_top;
        info->cpus[i].state     = CPU_BOOTED;
        cpu->extra_argument     = stack_top;
        cpu->goto_address       = (void*)ap_entry_point;

        serial_printf("[SMP] Configured AP %lu (LAPIC ID %u) to boot at 0x%llx\n",
                      i, cpu->lapic_id, (uint64_t)ap_entry_point);
        ap_count++;
    }

    __sync_synchronize();

    if (ap_count > 0) {
        serial_printf("[SMP] Waiting for %u AP(s) to initialize...\n", ap_count);
        uint64_t timeout = 10000000;
        while (ap_online_count < ap_count && timeout--)
            asm volatile ("pause");

        uint32_t online = ap_online_count;
        if (online == ap_count)
            serial_printf("[SMP SUCCESS] All %u AP(s) online!\n", ap_count);
        else
            serial_printf("[SMP WARNING] Only %u/%u AP(s) online (timeout)\n",
                          online, ap_count);
        info->online_count = 1 + online;
    } else {
        serial_writestring("[SMP] No APs to boot\n");
    }

    serial_writestring("[SMP] AP Boot Sequence Complete \n\n");
}

static void smp_init_limine(struct limine_mp_response* response) {
    if (!response) { serial_writestring("[SMP] Limine MP response is NULL\n"); return; }

    serial_printf("[SMP] Initializing via Limine MP (CPU count: %u)\n",
                  response->cpu_count);
    smp_info.cpu_count    = response->cpu_count;
    smp_info.online_count = 1;

    acpi_madt_t* madt = (acpi_madt_t*)acpi_find_table("APIC", 0);
    if (madt) {
        smp_info.lapic_base = madt->local_apic_address;
        serial_printf("[SMP] LAPIC base from ACPI: 0x%x\n", smp_info.lapic_base);
    } else {
        smp_info.lapic_base = 0xFEE00000;
    }

    for (uint64_t i = 0; i < response->cpu_count; i++) {
        struct limine_mp_info* cpu = response->cpus[i];
        smp_info.cpus[i].lapic_id     = cpu->lapic_id;
        smp_info.cpus[i].processor_id = cpu->lapic_id;
        smp_info.cpus[i].acpi_id      = 0;
        smp_info.cpus[i].state        = CPU_UNINITIALIZED;
        smp_info.cpus[i].is_bsp       = (cpu->lapic_id == response->bsp_lapic_id);
        smp_info.cpus[i].cpu_index    = i;

        if (smp_info.cpus[i].is_bsp) {
            smp_info.bsp_lapic_id  = cpu->lapic_id;
            smp_info.cpus[i].state = CPU_ONLINE;
            serial_printf("[SMP] BSP detected - APIC ID: %u\n", cpu->lapic_id);
        }
        serial_printf("[SMP] CPU[%lu] - APIC ID: %u, Processor ID: %u, BSP: %s\n",
                      i, cpu->lapic_id, cpu->lapic_id,
                      smp_info.cpus[i].is_bsp ? "YES" : "NO");
    }
}

void smp_init(struct limine_mp_response* mp_response) {
    serial_writestring("\n[SMP] Initialization\n");
    smp_init_limine(mp_response);

    uint32_t bsp_index = 0;
    for (uint32_t i = 0; i < smp_info.cpu_count; i++)
        if (smp_info.cpus[i].is_bsp) { bsp_index = i; break; }

    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        tss[i] = (tss_t *)calloc(1, sizeof(tss_t));
        if (!tss[i]) { serial_printf("[SMP ERROR] FAILED to allocate TSS for CPU %u\n", i); continue; }

        tss[i]->rsp0   = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        tss[i]->ist[0] = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        tss[i]->ist[1] = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        tss[i]->ist[2] = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        tss[i]->ist[3] = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        tss[i]->iobase = sizeof(tss_t);

        serial_printf("TSS[%u] base: 0x%llx\n", i, (uint64_t)tss[i]);

        tss_entry_t *entry = (tss_entry_t *)&gdt.gdt_entries[5 + (i * 2)];
        entry->limit_low            = sizeof(tss_t) - 1;
        uint64_t addr               = (uint64_t)tss[i];
        entry->base_low             = addr & 0xffff;
        entry->base_middle          = (addr >> 16) & 0xff;
        entry->access               = 0x89;
        entry->limit_high_and_flags = 0;
        entry->base_high            = (addr >> 24) & 0xff;
        entry->base_higher          = addr >> 32;
        entry->zero                 = 0;

        smp_info.cpus[i].tss_selector = TSS_SELECTOR_BASE + (i * 0x10);
    }

    gdtr.size = (5 + (smp_info.cpu_count * 2)) * sizeof(gdt_entry_t) - 1;
    serial_printf("Reloading extended GDT on BSP...\n");
    gdt_load();

    load_tss(smp_info.cpus[bsp_index].tss_selector);
    serial_printf("BSP TSS loaded (selector 0x%x)\n",
                  smp_info.cpus[bsp_index].tss_selector);

    uint32_t current_lapic_id = lapic_get_id();
    serial_printf("[SMP] Current LAPIC ID: %u\n", current_lapic_id);
    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        if (smp_info.cpus[i].lapic_id == current_lapic_id) {
            smp_info.cpus[i].is_bsp  = true;
            smp_info.cpus[i].state   = CPU_ONLINE;
            smp_info.bsp_lapic_id    = current_lapic_id;
            break;
        }
    }

    smp_print_info();
    init_percpu_regions();
    smp_boot_aps(mp_response);
    set_percpu_base(percpu_regions[bsp_index]);
    serial_printf("PerCPU base set for BSP %u: 0x%llx\n",
                  smp_info.bsp_lapic_id, (uint64_t)percpu_regions[bsp_index]);
    serial_writestring("[SMP] Initialization Complete \n\n");
}

smp_info_t* smp_get_info(void)       { return &smp_info; }
uint32_t    smp_get_cpu_count(void)  { return smp_info.cpu_count; }
uint32_t    smp_get_online_count(void){ return smp_info.online_count; }
bool        smp_is_bsp(void)         { return lapic_get_id() == smp_info.bsp_lapic_id; }

cpu_info_t* smp_get_current_cpu(void) {
    uint32_t id = lapic_get_id();
    for (uint32_t i = 0; i < smp_info.cpu_count; i++)
        if (smp_info.cpus[i].lapic_id == id) return &smp_info.cpus[i];
    for (uint32_t i = 0; i < smp_info.cpu_count; i++)
        if (smp_info.cpus[i].is_bsp) return &smp_info.cpus[i];
    return NULL;
}

void smp_wait_for_ready(void) {
    serial_writestring("Waiting until all APs are fully ready...\n");
    while (smp_get_online_count() < smp_get_cpu_count())
        asm volatile ("pause");
    serial_writestring("All APs ready.\n");
}

uint32_t smp_get_lapic_id_for_cpu(uint32_t cpu_index) {
    smp_info_t* info = smp_get_info();
    if (cpu_index >= info->cpu_count) return 0xFFFFFFFF;
    return info->cpus[cpu_index].lapic_id;
}

void smp_print_info(void) {
    serial_printf("\n[SMP] CPU Information \n");
    serial_printf("Total CPUs: %u\n",    smp_info.cpu_count);
    serial_printf("Online CPUs: %u\n",   smp_info.online_count);
    serial_printf("BSP APIC ID: %u\n",   smp_info.bsp_lapic_id);
    serial_printf("LAPIC Base: 0x%llx\n",smp_info.lapic_base);
    const char* states[] = {"UNINITIALIZED","BOOTED","ONLINE","OFFLINE","FAULTED"};
    for (uint32_t i = 0; i < smp_info.cpu_count; i++)
        serial_printf("CPU[%u]: APIC ID: %u, Processor ID: %u, ACPI ID: %u, State: %s, BSP: %s\n",
                      i, smp_info.cpus[i].lapic_id, smp_info.cpus[i].processor_id,
                      smp_info.cpus[i].acpi_id, states[smp_info.cpus[i].state],
                      smp_info.cpus[i].is_bsp ? "YES" : "NO");
    serial_printf("[SMP] End CPU Information \n");
}

void smp_print_info_fb(void) {
    printf("[SMP] CPU Information \n");
    printf("Total CPUs: %u\n",  smp_info.cpu_count);
    printf("Online CPUs: %u\n", smp_info.online_count);
    printf("BSP APIC ID: %u\n", smp_info.bsp_lapic_id);
    const char* states[] = {"UNINITIALIZED","BOOTED","ONLINE","OFFLINE","FAULTED"};
    for (uint32_t i = 0; i < smp_info.cpu_count; i++)
        printf("CPU[%u]: APIC ID: %u, State: %s, BSP: %s\n",
               i, smp_info.cpus[i].lapic_id, states[smp_info.cpus[i].state],
               smp_info.cpus[i].is_bsp ? "YES" : "NO");
}
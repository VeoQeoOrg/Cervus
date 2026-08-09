#include "../../include/boot/boot_info.h"
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
#include "../include/syscall/syscall.h"
#include "../../include/panic/panic.h"
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
static uint32_t expected_online = 1;
tlb_shootdown_t tlb_shootdown_queue[MAX_CPUS] = {0};

volatile uint32_t sched_ready_flag = 0;

void sched_notify_ready(void) {
    __sync_synchronize();
    sched_ready_flag = 1;
    __sync_synchronize();
    serial_writestring("[SCHED] Scheduler ready, notifying all APs\n");
}

__attribute__((used))
void ap_entry_init(void) {
    asm volatile ("cli");

    lapic_write(0xF0, 0);

    gdt_load();
    idt_load();

    uint32_t lapic_id = lapic_get_id();
    smp_info_t* info = smp_get_info();
    uint32_t my_index = 0;
    bool found = false;
    for (uint32_t i = 0; i < info->cpu_count; i++) {
        if (info->cpus[i].lapic_id == lapic_id && !info->cpus[i].is_bsp) {
            my_index = i;
            info->cpus[i].state = CPU_ONLINE;
            found = true;
            break;
        }
    }
    if (!found || !tss[my_index]) {
        serial_printf("[SMP ERROR] AP LAPIC %u: no CPU slot/TSS, halting this core\n",
                      lapic_id);
        for (;;) asm volatile ("cli; hlt");
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
    syscall_init();
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

extern char ap_trampoline_start[], ap_trampoline_end[];
extern char ap_tramp_cr3[], ap_tramp_entry[], ap_tramp_stack[];

#define AP_TRAMP_PHYS 0x8000ULL

static void smp_prepare_trampoline(void) {
    vmm_map_page(vmm_get_kernel_pagemap(), AP_TRAMP_PHYS, AP_TRAMP_PHYS,
                 VMM_PRESENT | VMM_WRITE);

    uint8_t *dst = (uint8_t *)pmm_phys_to_virt(AP_TRAMP_PHYS);
    size_t   sz  = (size_t)(ap_trampoline_end - ap_trampoline_start);
    memcpy(dst, ap_trampoline_start, sz);

    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    *(volatile uint64_t *)(dst + (ap_tramp_cr3   - ap_trampoline_start)) = cr3;
    *(volatile uint64_t *)(dst + (ap_tramp_entry - ap_trampoline_start)) =
        (uint64_t)(uintptr_t)ap_entry_init;
}

static void smp_set_tramp_stack(uint64_t stack_top) {
    uint8_t *dst = (uint8_t *)pmm_phys_to_virt(AP_TRAMP_PHYS);
    *(volatile uint64_t *)(dst + (ap_tramp_stack - ap_trampoline_start)) = stack_top;
    __sync_synchronize();
}

static void smp_boot_aps_native(void) {
    serial_writestring("\n[SMP] Booting APs via native INIT-SIPI-SIPI\n");

    smp_info_t *info   = smp_get_info();
    uint32_t    online = 0;
    uint8_t     vector = (uint8_t)(AP_TRAMP_PHYS >> 12);

    smp_prepare_trampoline();

    for (uint32_t i = 0; i < info->cpu_count; i++) {
        if (info->cpus[i].is_bsp) continue;
        if (!tss[i] || info->cpus[i].state == CPU_FAULTED) {
            serial_printf("[SMP] Skipping CPU %u (no TSS)\n", i);
            continue;
        }

        uint64_t stack_top = smp_allocate_stack(i, AP_STACK_SIZE);
        if (stack_top == 0) {
            info->cpus[i].state = CPU_FAULTED;
            continue;
        }
        info->cpus[i].stack_top = stack_top;
        info->cpus[i].state     = CPU_BOOTED;

        uint32_t before = __atomic_load_n(&ap_online_count, __ATOMIC_ACQUIRE);
        smp_set_tramp_stack(stack_top);

        uint32_t lid = info->cpus[i].lapic_id;
        lapic_send_init(lid);
        apic_udelay(10000);
        lapic_send_startup(lid, vector);
        apic_udelay(200);
        if (__atomic_load_n(&ap_online_count, __ATOMIC_ACQUIRE) == before) {
            lapic_send_startup(lid, vector);
            apic_udelay(200);
        }

        uint64_t timeout = 50000000;
        while (__atomic_load_n(&ap_online_count, __ATOMIC_ACQUIRE) == before && timeout--)
            asm volatile ("pause");

        if (__atomic_load_n(&ap_online_count, __ATOMIC_ACQUIRE) == before) {
            serial_printf("[SMP] AP %u (LAPIC %u) did not start\n", i, lid);
            info->cpus[i].state = CPU_FAULTED;
        } else {
            online++;
            serial_printf("[SMP] AP %u (LAPIC %u) online via SIPI\n", i, lid);
        }
    }

    info->online_count = 1 + online;
    expected_online    = 1 + online;
    serial_printf("[SMP] native bringup complete: %u AP(s) online\n", online);
}

void smp_boot_aps(void) {
    smp_boot_aps_native();
}

static void smp_enumerate_madt_into_bootinfo(void) {
    acpi_madt_t* madt = (acpi_madt_t*)acpi_find_table("APIC", 0);
    if (!madt) {
        serial_writestring("[SMP] no MADT, staying single-CPU\n");
        return;
    }

    boot_info_t* bi  = boot_info_mut();
    uint32_t     bsp = lapic_get_id();
    int          n   = 0;

    uint8_t* p   = madt->entries;
    uint8_t* end = (uint8_t*)madt + madt->header.length;
    while (p + 2 <= end && n < BOOT_CPU_MAX) {
        madt_entry_header_t* h = (madt_entry_header_t*)p;
        if (h->length < 2) break;
        if (h->type == MADT_ENTRY_LAPIC) {
            madt_lapic_entry_t* e = (madt_lapic_entry_t*)p;
            if (e->flags & 1) {
                bi->cpus[n].lapic_id = e->apic_id;
                bi->cpus[n].is_bsp   = (e->apic_id == bsp);
                n++;
            }
        }
        p += h->length;
    }

    if (n > 0) {
        bi->cpu_count    = n;
        bi->bsp_lapic_id = bsp;
        serial_printf("[SMP] MADT enumerated %d CPU(s), BSP LAPIC %u\n", n, bsp);
    }
}

static void smp_setup_cpus(void) {
    smp_enumerate_madt_into_bootinfo();

    const boot_info_t* bi = boot_info();

    serial_printf("[SMP] CPUs from boot_info: %d\n", bi->cpu_count);
    smp_info.cpu_count    = bi->cpu_count;
    smp_info.online_count = 1;

    acpi_madt_t* madt = (acpi_madt_t*)acpi_find_table("APIC", 0);
    if (madt) {
        smp_info.lapic_base = madt->local_apic_address;
        serial_printf("[SMP] LAPIC base from ACPI: 0x%x\n", smp_info.lapic_base);
    } else {
        smp_info.lapic_base = 0xFEE00000;
    }

    for (int i = 0; i < bi->cpu_count; i++) {
        smp_info.cpus[i].lapic_id     = bi->cpus[i].lapic_id;
        smp_info.cpus[i].processor_id = bi->cpus[i].lapic_id;
        smp_info.cpus[i].acpi_id      = 0;
        smp_info.cpus[i].state        = CPU_UNINITIALIZED;
        smp_info.cpus[i].is_bsp       = bi->cpus[i].is_bsp;
        smp_info.cpus[i].cpu_index    = i;

        if (smp_info.cpus[i].is_bsp) {
            smp_info.bsp_lapic_id  = bi->cpus[i].lapic_id;
            smp_info.cpus[i].state = CPU_ONLINE;
            serial_printf("[SMP] BSP detected - APIC ID: %u\n", bi->cpus[i].lapic_id);
        }
        serial_printf("[SMP] CPU[%d] - APIC ID: %u, BSP: %s\n",
                      i, bi->cpus[i].lapic_id, bi->cpus[i].is_bsp ? "YES" : "NO");
    }
}

void smp_init(void) {
    serial_writestring("\n[SMP] Initialization\n");
    smp_setup_cpus();

    uint32_t bsp_index = 0;
    for (uint32_t i = 0; i < smp_info.cpu_count; i++)
        if (smp_info.cpus[i].is_bsp) { bsp_index = i; break; }

    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        bool is_bsp_cpu = (i == bsp_index);
        tss[i] = (tss_t *)calloc(1, sizeof(tss_t));
        if (!tss[i]) {
            if (is_bsp_cpu)
                kernel_panic("SMP: out of memory allocating BSP TSS");
            serial_printf("[SMP WARNING] no memory for CPU %u TSS, "
                          "disabling this core\n", i);
            smp_info.cpus[i].state = CPU_FAULTED;
            continue;
        }

        tss[i]->rsp0   = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        tss[i]->ist[0] = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        tss[i]->ist[1] = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        tss[i]->ist[2] = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        tss[i]->ist[3] = smp_allocate_stack(i, KERNEL_STACK_SIZE);
        if (!tss[i]->rsp0 || !tss[i]->ist[0] || !tss[i]->ist[1] ||
            !tss[i]->ist[2] || !tss[i]->ist[3]) {
            if (is_bsp_cpu)
                kernel_panic("SMP: out of memory allocating BSP TSS/IST stacks");
            serial_printf("[SMP WARNING] no memory for CPU %u kernel stacks, "
                          "disabling this core\n", i);
            free(tss[i]);
            tss[i] = NULL;
            smp_info.cpus[i].state = CPU_FAULTED;
            continue;
        }
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
    tsc_calibrate_bsp();
    smp_boot_aps();
    set_percpu_base(percpu_regions[bsp_index]);
    serial_printf("PerCPU base set for BSP %u: 0x%llx\n",
                  smp_info.bsp_lapic_id, (uint64_t)percpu_regions[bsp_index]);
    serial_writestring("[SMP] Initialization Complete \n\n");
}

smp_info_t* smp_get_info(void)       { return &smp_info; }
uint32_t    smp_get_cpu_count(void)  { return smp_info.cpu_count; }
uint32_t    smp_get_online_count(void){
    uint32_t dyn = 1 + __atomic_load_n(&ap_online_count, __ATOMIC_ACQUIRE);
    if (dyn > smp_info.online_count) smp_info.online_count = dyn;
    return smp_info.online_count;
}
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
    uint64_t timeout = 50000000;
    while (smp_get_online_count() < expected_online && timeout--)
        asm volatile ("pause");
    if (smp_get_online_count() < expected_online)
        serial_printf("[SMP WARNING] Only %u/%u CPU(s) ready, continuing anyway\n",
                      smp_get_online_count(), expected_online);
    else
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
    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        int online = smp_info.cpus[i].state == 2;
        printf("cpu%u: %s, apic id %u%s\n",
               i,
               smp_info.cpus[i].is_bsp ? "BSP" : "AP",
               smp_info.cpus[i].lapic_id,
               online ? "" : " (offline)");
    }
}
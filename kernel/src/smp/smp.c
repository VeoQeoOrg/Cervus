#include "../../include/smp/smp.h"
#include "../../include/acpi/acpi.h"
#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/gdt/gdt.h"
#include "../../include/interrupts/idt.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

static smp_info_t smp_info = {0};
static volatile uint32_t ap_online_count = 0;

__attribute__((used))
void ap_entry_init(struct limine_mp_info* cpu_info) {
    (void)cpu_info;

    asm volatile ("cli");
    serial_printf(COM1, "GDT Loading\n");

    gdt_load();
    serial_printf(COM1, "GDT Loaded\n");
    serial_printf(COM1, "IDT Loading\n");

    idt_load();
    serial_printf(COM1, "IDT Loaded\n");
    lapic_enable();

    uint32_t lapic_id = lapic_get_id();
    smp_info_t* info = smp_get_info();

    for (uint32_t i = 0; i < info->cpu_count; i++) {
        if (info->cpus[i].lapic_id == lapic_id && !info->cpus[i].is_bsp) {
            info->cpus[i].state = CPU_ONLINE;
            break;
        }
    }

    __sync_fetch_and_add(&ap_online_count, 1);

    lapic_eoi();

    asm volatile ("sti");

    serial_printf(COM1, "SMP: AP (LAPIC ID %u) initialized and online!\n", lapic_id);

    while (1) {
        asm volatile ("hlt");
    }
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

static uint64_t smp_allocate_stack(uint32_t cpu_index) {
    #define AP_STACK_SIZE 16384

    void* stack_pages = pmm_alloc(AP_STACK_SIZE / PAGE_SIZE);
    if (!stack_pages) {
        serial_printf(COM1, "SMP: WARNING - Failed to allocate stack for CPU %u\n", cpu_index);
        return 0;
    }

    uint64_t stack_virt = (uint64_t)stack_pages + AP_STACK_SIZE;
    serial_printf(COM1, "SMP: Allocated stack for CPU %u at 0x%llx\n",
                  cpu_index, stack_virt);

    return stack_virt;
}

void smp_boot_aps(struct limine_mp_response* mp_response) {
    if (!mp_response) {
        serial_writestring(COM1, "SMP: ERROR - MP response is NULL\n");
        return;
    }

    serial_writestring(COM1, "\n=== Booting Application Processors ===\n");

    smp_info_t* info = smp_get_info();
    uint32_t bsp_lapic_id = info->bsp_lapic_id;
    uint32_t ap_count = 0;

    for (uint64_t i = 0; i < mp_response->cpu_count; i++) {
        struct limine_mp_info* cpu = mp_response->cpus[i];

        if (cpu->lapic_id == bsp_lapic_id) {
            continue;
        }

        uint64_t stack_top = smp_allocate_stack(i);
        if (stack_top == 0) {
            serial_printf(COM1, "SMP: Skipping CPU %u (stack alloc failed)\n", i);
            info->cpus[i].state = CPU_FAULTED;
            continue;
        }

        info->cpus[i].stack_top = stack_top;
        info->cpus[i].state = CPU_BOOTED;

        cpu->extra_argument = stack_top;
        cpu->goto_address = (void*)ap_entry_point;

        serial_printf(COM1, "SMP: Configured AP %lu (LAPIC ID %u) to boot at 0x%llx\n",
                      i, cpu->lapic_id, (uint64_t)ap_entry_point);
        ap_count++;
    }

    __sync_synchronize();

    if (ap_count > 0) {
        serial_printf(COM1, "SMP: Waiting for %u AP(s) to initialize...\n", ap_count);
        uint64_t timeout = 10000000;
        while (ap_online_count < ap_count && timeout--) {
            asm volatile ("pause");
        }

        uint32_t online = ap_online_count;
        if (online == ap_count) {
            serial_printf(COM1, "SMP: SUCCESS - All %u AP(s) online!\n", ap_count);
        } else {
            serial_printf(COM1, "SMP: WARNING - Only %u/%u AP(s) online (timeout)\n",
                          online, ap_count);
        }

        info->online_count = 1 + online;
    } else {
        serial_writestring(COM1, "SMP: No APs to boot\n");
    }

    serial_writestring(COM1, "=== AP Boot Sequence Complete ===\n\n");
}

static void smp_init_limine(struct limine_mp_response* response) {
    if (!response) {
        serial_writestring(COM1, "SMP: Limine MP response is NULL\n");
        return;
    }

    serial_printf(COM1, "SMP: Initializing via Limine MP (CPU count: %u)\n",
                  response->cpu_count);

    smp_info.cpu_count = response->cpu_count;
    smp_info.online_count = 1;

    acpi_madt_t* madt = (acpi_madt_t*)acpi_find_table("APIC", 0);
    if (madt) {
        smp_info.lapic_base = madt->local_apic_address;
        serial_printf(COM1, "SMP: LAPIC base from ACPI: 0x%x\n", smp_info.lapic_base);
    } else {
        smp_info.lapic_base = 0xFEE00000;
        serial_printf(COM1, "SMP: Using default LAPIC base: 0x%x\n", smp_info.lapic_base);
    }

    for (uint64_t i = 0; i < response->cpu_count; i++) {
        struct limine_mp_info* cpu = response->cpus[i];

        smp_info.cpus[i].lapic_id = cpu->lapic_id;
        smp_info.cpus[i].processor_id = cpu->lapic_id;
        smp_info.cpus[i].acpi_id = 0;
        smp_info.cpus[i].state = CPU_UNINITIALIZED;
        smp_info.cpus[i].is_bsp = (cpu->lapic_id == response->bsp_lapic_id);

        if (smp_info.cpus[i].is_bsp) {
            smp_info.bsp_lapic_id = cpu->lapic_id;
            smp_info.cpus[i].state = CPU_ONLINE;
            serial_printf(COM1, "SMP: BSP detected - APIC ID: %u\n",
                          cpu->lapic_id);
        }

        serial_printf(COM1, "SMP: CPU[%lu] - APIC ID: %u, Processor ID: %u, BSP: %s\n",
                      i, cpu->lapic_id, cpu->lapic_id,
                      smp_info.cpus[i].is_bsp ? "YES" : "NO");
    }
}

void smp_init(struct limine_mp_response* mp_response) {
    serial_writestring(COM1, "\n=== SMP Initialization ===\n");

    smp_init_limine(mp_response);

    uint32_t current_lapic_id = lapic_get_id();
    serial_printf(COM1, "SMP: Current LAPIC ID: %u\n", current_lapic_id);

    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        if (smp_info.cpus[i].lapic_id == current_lapic_id) {
            smp_info.cpus[i].is_bsp = true;
            smp_info.cpus[i].state = CPU_ONLINE;
            smp_info.bsp_lapic_id = current_lapic_id;
            break;
        }
    }

    smp_print_info();

    smp_boot_aps(mp_response);

    serial_writestring(COM1, "=== SMP Initialization Complete ===\n\n");
}

smp_info_t* smp_get_info(void) {
    return &smp_info;
}

cpu_info_t* smp_get_current_cpu(void) {
    uint32_t current_lapic_id = lapic_get_id();

    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        if (smp_info.cpus[i].lapic_id == current_lapic_id) {
            return &smp_info.cpus[i];
        }
    }

    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        if (smp_info.cpus[i].is_bsp) {
            return &smp_info.cpus[i];
        }
    }

    return NULL;
}

uint32_t smp_get_cpu_count(void) {
    return smp_info.cpu_count;
}

uint32_t smp_get_online_count(void) {
    return smp_info.online_count;
}

bool smp_is_bsp(void) {
    cpu_info_t* cpu = smp_get_current_cpu();
    return cpu ? cpu->is_bsp : true;
}

void smp_print_info(void) {
    serial_writestring(COM1, "\n=== SMP CPU Information ===\n");
    serial_printf(COM1, "Total CPUs: %u\n", smp_info.cpu_count);
    serial_printf(COM1, "Online CPUs: %u\n", smp_info.online_count);
    serial_printf(COM1, "BSP APIC ID: %u\n", smp_info.bsp_lapic_id);

    if (smp_info.lapic_base != 0) {
        serial_printf(COM1, "LAPIC Base: 0x%llx\n", smp_info.lapic_base);
    } else {
        serial_writestring(COM1, "LAPIC Base: NOT AVAILABLE\n");
    }

    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        const char* state_str;
        switch (smp_info.cpus[i].state) {
            case CPU_UNINITIALIZED: state_str = "UNINITIALIZED"; break;
            case CPU_BOOTED: state_str = "BOOTED"; break;
            case CPU_ONLINE: state_str = "ONLINE"; break;
            case CPU_OFFLINE: state_str = "OFFLINE"; break;
            case CPU_FAULTED: state_str = "FAULTED"; break;
            default: state_str = "UNKNOWN"; break;
        }

        serial_printf(COM1, "CPU[%u]: APIC ID: %u, Processor ID: %u, ACPI ID: %u, State: %s, BSP: %s\n",
                      i, smp_info.cpus[i].lapic_id,
                      smp_info.cpus[i].processor_id,
                      smp_info.cpus[i].acpi_id,
                      state_str,
                      smp_info.cpus[i].is_bsp ? "YES" : "NO");
    }

    serial_writestring(COM1, "=== End SMP CPU Information ===\n");
}

void smp_print_info_fb(void) {
    printf("\n=== SMP CPU Information ===\n");
    printf("Total CPUs: %u\n", smp_info.cpu_count);
    printf("Online CPUs: %u\n", smp_info.online_count);
    printf("BSP APIC ID: %u\n", smp_info.bsp_lapic_id);

    if (smp_info.lapic_base != 0) {
        printf("LAPIC Base: 0x%llx\n", smp_info.lapic_base);
    }

    printf("\nCPU List:\n");
    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        const char* status = smp_info.cpus[i].is_bsp ? " [BSP]" : "";
        printf("  CPU[%u]: APIC ID %u%s\n",
               i, smp_info.cpus[i].lapic_id, status);
    }

    printf("============================\n");
}

void smp_wait_for_ready(void) {
    // TODO
    return;
}
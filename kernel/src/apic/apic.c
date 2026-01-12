#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include "../../include/io/ports.h"
#include "../../include/acpi/acpi.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/vmm.h"
#include <string.h>
#include <stddef.h>

uintptr_t lapic_base = 0;
uintptr_t ioapic_base = 0;
uintptr_t hpet_base = 0;
uint32_t hpet_period = 0;
static acpi_madt_t* madt = NULL;
static acpi_hpet_t* hpet_table = NULL;

static inline uintptr_t phys_to_virt(uintptr_t phys) {
    return phys + pmm_get_hhdm_offset();
}

static bool map_mmio_region(uintptr_t phys_base, size_t size) {
    vmm_pagemap_t* kernel_pagemap = vmm_get_kernel_pagemap();
    if (!kernel_pagemap) return false;
    
    for (uintptr_t offset = 0; offset < size; offset += 0x1000) {
        uintptr_t phys_addr = phys_base + offset;
        uintptr_t virt_addr = phys_to_virt(phys_addr);
        
        uintptr_t mapped_phys;
        if (vmm_virt_to_phys(kernel_pagemap, virt_addr, &mapped_phys)) {
            if (mapped_phys != phys_addr) return false;
            continue;
        }
        
        if (!vmm_map_page(kernel_pagemap, virt_addr, phys_addr, 
                         VMM_PRESENT | VMM_WRITE | VMM_NOEXEC)) {
            return false;
        }
    }
    
    return true;
}

bool hpet_init(void) {
    hpet_table = (acpi_hpet_t*)acpi_find_table("HPET", 0);
    if (!hpet_table) return false;
    
    uintptr_t phys_base = hpet_table->address;
    if (!map_mmio_region(phys_base, 0x1000)) return false;
    
    hpet_base = phys_to_virt(phys_base);
    
    volatile uint32_t* hpet_regs = (volatile uint32_t*)hpet_base;
    hpet_period = hpet_regs[HPET_PERIOD / 4];
    if (hpet_period == 0) return false;
    
    uint64_t config = *(volatile uint64_t*)(hpet_base + HPET_CONFIG);
    config |= HPET_ENABLE_CNF;
    if (hpet_table->legacy_replacement) config |= HPET_LEGACY_CNF;
    *(volatile uint64_t*)(hpet_base + HPET_CONFIG) = config;
    
    return true;
}

bool hpet_is_available(void) {
    return hpet_base != 0 && hpet_period != 0;
}

uint64_t hpet_read_counter(void) {
    if (!hpet_base) return 0;
    
    if (hpet_table->counter_size) {
        return *(volatile uint64_t*)(hpet_base + HPET_MAIN_COUNTER);
    } else {
        return *(volatile uint32_t*)(hpet_base + HPET_MAIN_COUNTER);
    }
}

uint64_t hpet_get_frequency(void) {
    if (!hpet_period) return 0;
    return 1000000000000000ULL / hpet_period;
}

void hpet_sleep_ns(uint64_t nanoseconds) {
    if (!hpet_base || !hpet_period) return;
    
    uint64_t ticks_needed = (nanoseconds * 1000000ULL) / hpet_period;
    if (ticks_needed == 0) ticks_needed = 1;
    
    uint64_t start = hpet_read_counter();
    uint64_t target = start + ticks_needed;
    
    if (target > start) {
        while (hpet_read_counter() < target) asm volatile("pause");
    } else {
        while (hpet_read_counter() > start) asm volatile("pause");
        while (hpet_read_counter() < (target - 0xFFFFFFFFFFFFFFFFULL)) asm volatile("pause");
    }
}

void hpet_sleep_us(uint64_t microseconds) {
    hpet_sleep_ns(microseconds * 1000ULL);
}

void hpet_sleep_ms(uint64_t milliseconds) {
    hpet_sleep_ns(milliseconds * 1000000ULL);
}

static void parse_madt(void) {
    madt = (acpi_madt_t*)acpi_find_table("APIC", 0);
    if (!madt) return;
    
    if (!map_mmio_region(madt->local_apic_address, 0x1000)) return;
    lapic_base = phys_to_virt(madt->local_apic_address);
    
    uint8_t* entries = madt->entries;
    uint32_t length = madt->header.length;
    uint8_t* end = (uint8_t*)madt + length;
    
    while (entries < end) {
        madt_entry_header_t* header = (madt_entry_header_t*)entries;
        
        switch (header->type) {
            case MADT_ENTRY_LAPIC: {
                madt_lapic_entry_t* lapic_entry = (madt_lapic_entry_t*)entries;
                (void)lapic_entry;
                break;
            }
            
            case MADT_ENTRY_IOAPIC: {
                madt_ioapic_entry_t* ioapic_entry = (madt_ioapic_entry_t*)entries;
                if (!map_mmio_region(ioapic_entry->ioapic_address, 0x1000)) break;
                ioapic_base = phys_to_virt(ioapic_entry->ioapic_address);
                break;
            }
            
            default:
                break;
        }
        
        entries += header->length;
    }
}

bool apic_is_available(void) {
    return madt != NULL;
}

void apic_init(void) {
    parse_madt();
    if (!madt) return;
    if (!lapic_base) return;
    
    hpet_init();
    lapic_enable();
    
    if (ioapic_base) {
        uint32_t max_redirects = ioapic_get_max_redirects(ioapic_base);
        for (uint32_t i = 0; i < max_redirects; i++) {
            ioapic_mask_irq(i);
        }
    }
}

void apic_setup_irq(uint8_t irq, uint8_t vector, bool mask, uint32_t flags) {
    if (!ioapic_base) return;
    
    uint32_t redir_flags = IOAPIC_DELIVERY_FIXED | flags;
    if (mask) redir_flags |= IOAPIC_INT_MASKED;
    
    ioapic_redirect_irq(irq, vector, redir_flags);
}

void apic_timer_calibrate(void) {
    if (!hpet_is_available()) return;
    
    uint64_t measurement_time_ns = 10000000ULL;
    uint64_t hpet_ticks_needed = (measurement_time_ns * 1000000ULL) / hpet_period;
    
    lapic_write(LAPIC_TIMER_DCR, 0x3);
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED | 0xFF);
    lapic_write(LAPIC_TIMER, 0xFF);
    
    uint64_t hpet_start = hpet_read_counter();
    uint64_t hpet_target = hpet_start + hpet_ticks_needed;
    
    while (hpet_read_counter() < hpet_target) asm volatile("pause");
    
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED | 0xFF);
    uint32_t remaining = lapic_read(LAPIC_TIMER_CCR);
    
    if (remaining == 0 || remaining == 0xFFFFFFFF) return;
    
    uint32_t ticks_elapsed = 0xFFFFFFFF - remaining;
    uint32_t ticks_per_10ms = (uint32_t)((ticks_elapsed * 10000000ULL) / measurement_time_ns);
    
    if (ticks_per_10ms == 0) return;
    
    lapic_timer_init(0x20, ticks_per_10ms, true, 0x3);
}
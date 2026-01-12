#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include "../../include/io/ports.h"
#include "../../include/memory/pmm.h"
#include <stddef.h>

static inline uintptr_t phys_to_virt(uintptr_t phys) {
    return phys + pmm_get_hhdm_offset();
}

void lapic_write(uint32_t reg, uint32_t value) {
    if (!lapic_base) {
        serial_printf(COM1, "LAPIC: Attempt to write to unmapped LAPIC (reg: 0x%x)\n", reg);
        return;
    }
    
    if (reg & 0x3) {
        serial_printf(COM1, "LAPIC: Unaligned register access: 0x%x\n", reg);
        return;
    }
    
    volatile uint32_t* addr = (volatile uint32_t*)(lapic_base + reg);
    *addr = value;
    
    (void)*addr;
}

uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) {
        serial_printf(COM1, "LAPIC: Attempt to read from unmapped LAPIC (reg: 0x%x)\n", reg);
        return 0;
    }
    
    if (reg & 0x3) {
        serial_printf(COM1, "LAPIC: Unaligned register access: 0x%x\n", reg);
        return 0;
    }
    
    volatile uint32_t* addr = (volatile uint32_t*)(lapic_base + reg);
    return *addr;
}

void lapic_enable(void) {
    if (!lapic_base) return;
    
    lapic_write(LAPIC_SIVR, lapic_read(LAPIC_SIVR) | LAPIC_ENABLE | LAPIC_SPURIOUS_VECTOR);
    serial_printf(COM1, "LAPIC enabled, ID: 0x%x\n", lapic_get_id());
}

uint32_t lapic_get_id(void) {
    return (lapic_read(LAPIC_ID) >> 24) & 0xFF;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

void lapic_timer_init(uint32_t vector, uint32_t count, bool periodic, uint8_t divisor) {
    serial_printf(COM1, "Initializing LAPIC timer: vector=0x%x, count=%u, periodic=%d, divisor=%u\n",
                  vector, count, periodic, divisor);
    
    if (count == 0) {
        serial_writestring(COM1, "Warning: APIC timer count is 0, timer will not generate interrupts\n");
    }
    
    lapic_write(LAPIC_TIMER_DCR, divisor & 0x7);
    
    uint32_t timer_config = vector & 0xFF;
    if (periodic) {
        timer_config |= LAPIC_TIMER_PERIODIC;
    }
    
    lapic_write(LAPIC_TIMER_ICR, count);
    
    lapic_write(LAPIC_TIMER, timer_config | LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_TIMER, timer_config);
    
    uint32_t current = lapic_read(LAPIC_TIMER_CCR);
    serial_printf(COM1, "LAPIC timer current count: %u\n", current);
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED);
}

uint32_t lapic_timer_get_current(void) {
    return lapic_read(LAPIC_TIMER_CCR);
}

void apic_send_ipi(uint32_t lapic_id, uint8_t vector, uint32_t delivery_mode) {
    uint32_t icr_low = vector | delivery_mode;
    uint32_t icr_high = lapic_id << 24;
    
    lapic_write(0x0310, icr_high);   
    lapic_write(0x0300, icr_low);
    while (lapic_read(0x0300) & (1 << 12));
}

void apic_send_init(uint32_t lapic_id) {
    apic_send_ipi(lapic_id, 0, 0x500);
    if (hpet_is_available()) {
        hpet_sleep_ms(10);
    }
}

void apic_send_startup(uint32_t lapic_id, uint32_t vector) {
    apic_send_ipi(lapic_id, vector, 0x600);
    if (hpet_is_available()) {
        hpet_sleep_us(200);
    }
}
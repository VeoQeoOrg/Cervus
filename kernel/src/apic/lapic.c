#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include "../../include/io/ports.h"
#include "../../include/memory/pmm.h"
#include "../../include/smp/smp.h"
#include "../../include/interrupts/interrupts.h"
#include <stddef.h>

static inline uintptr_t phys_to_virt(uintptr_t phys) {
    return phys + pmm_get_hhdm_offset();
}

void lapic_write(uint32_t reg, uint32_t value) {
    if (!lapic_base) {
        serial_printf("LAPIC: Attempt to write to unmapped LAPIC (reg: 0x%x)\n", reg);
        return;
    }

    if (reg & 0x3) {
        serial_printf("LAPIC: Unaligned register access: 0x%x\n", reg);
        return;
    }

    volatile uint32_t* addr = (volatile uint32_t*)(lapic_base + reg);
    *addr = value;

    (void)*addr;
}

uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) {
        serial_printf("LAPIC: Attempt to read from unmapped LAPIC (reg: 0x%x)\n", reg);
        return 0;
    }

    if (reg & 0x3) {
        serial_printf("LAPIC: Unaligned register access: 0x%x\n", reg);
        return 0;
    }

    volatile uint32_t* addr = (volatile uint32_t*)(lapic_base + reg);
    return *addr;
}

void lapic_enable(void) {
    if (!lapic_base) return;

    lapic_write(LAPIC_SIVR, lapic_read(LAPIC_SIVR) | LAPIC_ENABLE | LAPIC_SPURIOUS_VECTOR);
    serial_printf("LAPIC enabled, ID: 0x%x\n", lapic_get_id());
}

uint32_t lapic_get_id(void) {
    return (lapic_read(LAPIC_ID) >> 24) & 0xFF;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

void lapic_timer_init(uint32_t vector, uint32_t count, bool periodic, uint8_t divisor) {
    serial_printf("Initializing LAPIC timer: vector=0x%x, count=%u, periodic=%d, divisor=%u\n",
                  vector, count, periodic, divisor);

    if (count == 0) {
        serial_writestring("Warning: APIC timer count is 0, timer will not generate interrupts\n");
    }

    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED);

    lapic_write(LAPIC_TIMER_DCR, divisor & 0x7);

    lapic_write(LAPIC_TIMER_ICR, count);

    uint32_t timer_config = vector & 0xFF;
    if (periodic) {
        timer_config |= LAPIC_TIMER_PERIODIC;
    }

    lapic_write(LAPIC_TIMER, timer_config);

    uint32_t current = lapic_read(LAPIC_TIMER_CCR);
    serial_printf("LAPIC timer started: current count: %u\n", current);
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED);
}

uint32_t lapic_timer_get_current(void) {
    return lapic_read(LAPIC_TIMER_CCR);
}

void lapic_send_ipi(uint32_t target_lapic_id, uint8_t vector)
{
    uint32_t icr_low  = vector | (0 << 8) | (1 << 14);
    uint32_t icr_high = target_lapic_id << 24;

    lapic_write(0x310, icr_high);
    lapic_write(0x300, icr_low);

    while (lapic_read(0x300) & (1 << 12))
        asm volatile ("pause");

    serial_printf("Sent IPI vector 0x%02x to LAPIC %u\n", vector, target_lapic_id);
}

void lapic_send_ipi_to_all_but_self(uint8_t vector)
{
    uint32_t icr_low = vector | (0 << 8) | (1 << 14) | (3 << 18);

    lapic_write(0x310, 0);
    lapic_write(0x300, icr_low);

    while (lapic_read(0x300) & (1 << 12))
        asm volatile ("pause");

    serial_printf("Broadcast IPI (all but self) vector 0x%02x sent\n", vector);
}

void ipi_reschedule_all(void) {
    lapic_send_ipi_to_all_but_self(IPI_RESCHEDULE_VECTOR);
}

void ipi_reschedule_cpu(uint32_t lapic_id) {
    lapic_send_ipi(lapic_id, IPI_RESCHEDULE_VECTOR);
}

void ipi_reschedule_single(uint32_t target_lapic_id) {
    uint32_t icr_high = target_lapic_id << 24;
    uint32_t icr_low = IPI_RESCHEDULE_VECTOR | (0 << 8) | (1 << 14);

    lapic_write(0x310, icr_high);
    lapic_write(0x300, icr_low);

    while (lapic_read(0x300) & (1 << 12))
        asm volatile ("pause");

    serial_printf("Reschedule IPI sent to LAPIC %u\n", target_lapic_id);
}

void ipi_tlb_shootdown_broadcast(const uintptr_t* addrs, size_t count) {
    if (count > MAX_TLB_ADDRESSES) count = MAX_TLB_ADDRESSES;

    uint32_t id = lapic_get_id();
    tlb_shootdown_t* q = &tlb_shootdown_queue[id];

    q->count = count;
    for (size_t i = 0; i < count; i++) {
        q->addresses[i] = addrs[i];
    }
    q->pending = true;

    uint32_t icr_low = IPI_TLB_SHOOTDOWN | (0 << 8) | (1 << 14) | (3 << 18);
    lapic_write(0x310, 0);
    lapic_write(0x300, icr_low);

    while (lapic_read(0x300) & (1 << 12))
        asm volatile ("pause");

    serial_printf("TLB shootdown broadcast sent for %zu addresses\n", count);
}

void ipi_tlb_shootdown_single(uint32_t target_lapic_id, uintptr_t addr) {
    tlb_shootdown_t* q = &tlb_shootdown_queue[target_lapic_id];

    q->addresses[0] = addr;
    q->count = 1;
    q->pending = true;

    uint32_t icr_high = target_lapic_id << 24;
    uint32_t icr_low = IPI_TLB_SHOOTDOWN | (0 << 8) | (1 << 14);

    lapic_write(0x310, icr_high);
    lapic_write(0x300, icr_low);

    while (lapic_read(0x300) & (1 << 12))
        asm volatile ("pause");

    serial_printf("TLB shootdown sent to LAPIC %u for virt 0x%llx\n", target_lapic_id, addr);
}
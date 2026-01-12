#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>
#include "../acpi/acpi.h"

#define LAPIC_ID                0x0020
#define LAPIC_VERSION           0x0030
#define LAPIC_TPR               0x0080
#define LAPIC_EOI               0x00B0
#define LAPIC_SIVR              0x00F0
#define LAPIC_ISR0              0x0100
#define LAPIC_TIMER             0x0320
#define LAPIC_THERMAL           0x0330
#define LAPIC_PERFORMANCE       0x0340
#define LAPIC_LINT0             0x0350
#define LAPIC_LINT1             0x0360
#define LAPIC_ERROR             0x0370
#define LAPIC_TIMER_ICR         0x0380
#define LAPIC_TIMER_CCR         0x0390
#define LAPIC_TIMER_DCR         0x03E0

#define LAPIC_ENABLE            (1 << 8)
#define LAPIC_SPURIOUS_VECTOR   0xFF

#define LAPIC_TIMER_MASKED      (1 << 16)
#define LAPIC_TIMER_PERIODIC    (1 << 17)
#define LAPIC_TIMER_DIV1        0x0
#define LAPIC_TIMER_DIV2        0x1
#define LAPIC_TIMER_DIV4        0x2
#define LAPIC_TIMER_DIV8        0x3
#define LAPIC_TIMER_DIV16       0x4
#define LAPIC_TIMER_DIV32       0x5
#define LAPIC_TIMER_DIV64       0x6
#define LAPIC_TIMER_DIV128      0x7

#define IOAPIC_ID               0x00
#define IOAPIC_VERSION          0x01
#define IOAPIC_ARB              0x02
#define IOAPIC_REDIR_START      0x10

#define IOAPIC_INT_MASKED       (1 << 16)
#define IOAPIC_TRIGGER_LEVEL    (1 << 15)
#define IOAPIC_POLARITY_LOW     (1 << 13)
#define IOAPIC_DELIVERY_FIXED   0x0
#define IOAPIC_DELIVERY_NMI     0x4

#define HPET_CAPABILITIES      0x000
#define HPET_PERIOD            0x004
#define HPET_CONFIG            0x010
#define HPET_INTERRUPT_STATUS  0x020
#define HPET_MAIN_COUNTER      0x0F0
#define HPET_TIMER0_CONFIG     0x100
#define HPET_TIMER0_COMPARATOR 0x108

#define HPET_ENABLE_CNF        (1ULL << 0)
#define HPET_LEGACY_CNF        (1ULL << 1)

#define HPET_TN_INT_ENABLE_CNF   (1 << 2)
#define HPET_TN_INT_TYPE_CNF     (1 << 3)
#define HPET_TN_PERIODIC_CNF     (1 << 4)
#define HPET_TN_32BIT_CNF        (1 << 8)
#define HPET_TN_FSB_ENABLE_CNF   (1 << 14)
#define HPET_TN_FSB_INT_DEL_CNF  (1 << 15)

void apic_init(void);
bool apic_is_available(void);
bool hpet_is_available(void);
bool hpet_init(void);

void lapic_write(uint32_t reg, uint32_t value);
uint32_t lapic_read(uint32_t reg);
void lapic_eoi(void);
void lapic_enable(void);
uint32_t lapic_get_id(void);
void lapic_timer_init(uint32_t vector, uint32_t count, bool periodic, uint8_t divisor);
void lapic_timer_stop(void);
uint32_t lapic_timer_get_current(void);

void ioapic_write(uintptr_t base, uint32_t reg, uint32_t value);
uint32_t ioapic_read(uintptr_t base, uint32_t reg);
uint32_t ioapic_get_max_redirects(uintptr_t base);
void ioapic_redirect_irq(uint8_t irq, uint8_t vector, uint32_t flags);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);

void apic_setup_irq(uint8_t irq, uint8_t vector, bool mask, uint32_t flags);
void apic_timer_calibrate(void);

uint64_t hpet_read_counter(void);
uint64_t hpet_get_frequency(void);
void hpet_sleep_ns(uint64_t nanoseconds);
void hpet_sleep_us(uint64_t microseconds);
void hpet_sleep_ms(uint64_t milliseconds);

void apic_send_ipi(uint32_t lapic_id, uint8_t vector, uint32_t delivery_mode);
void apic_send_init(uint32_t lapic_id);
void apic_send_startup(uint32_t lapic_id, uint32_t vector);

extern uintptr_t lapic_base;
extern uintptr_t ioapic_base;
extern uintptr_t hpet_base;
extern uint32_t hpet_period;

#endif
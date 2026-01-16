#include "../include/drivers/timer.h"
#include "../include/apic/apic.h"
#include "../include/io/serial.h"
#include "../include/interrupts/interrupts.h"
#include "../include/io/ports.h"

static volatile uint64_t ticks = 0;

DEFINE_IRQ(0x20, timer_handler)
{
    ticks++;
    lapic_eoi();
    (void)frame;
}

bool timer_init(void) {
    serial_writestring(COM1, "Initializing timer subsystem...\n");

    if (!apic_is_available()) {
        serial_writestring(COM1, "ERROR: APIC not available\n");
        return false;
    }

    apic_timer_calibrate();

    apic_setup_irq(0, 0x20, false, 0);

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    serial_writestring(COM1, "Timer subsystem initialized\n");

    if (hpet_is_available()) {
        serial_printf(COM1, "HPET frequency: %llu Hz\n", hpet_get_frequency());
    } else {
        serial_writestring(COM1, "HPET not available\n");
    }

        serial_writestring(COM1, "Checking APIC Timer Status\n");

    uint32_t timer_config = lapic_read(LAPIC_TIMER);
    uint32_t initial_count = lapic_read(LAPIC_TIMER_ICR);
    uint32_t current_count = lapic_read(LAPIC_TIMER_CCR);

    serial_printf(COM1, "APIC Timer Config: 0x%x\n", timer_config);
    serial_printf(COM1, "  Vector: 0x%x\n", timer_config & 0xFF);
    serial_printf(COM1, "  Masked: %s\n", (timer_config & LAPIC_TIMER_MASKED) ? "YES" : "NO");
    serial_printf(COM1, "  Periodic: %s\n", (timer_config & LAPIC_TIMER_PERIODIC) ? "YES" : "NO");
    serial_printf(COM1, "Initial Count: %u\n", initial_count);
    serial_printf(COM1, "Current Count: %u\n", current_count);
    serial_printf(COM1, "Ticks remaining until interrupt: %u\n", current_count);

    if (timer_config & LAPIC_TIMER_MASKED) {
        serial_writestring(COM1, "WARNING: APIC timer is masked! Unmasking...\n");
        lapic_write(LAPIC_TIMER, (timer_config & ~LAPIC_TIMER_MASKED));
    }

    serial_printf(COM1, "APIC timer should fire in approximately %u ms\n",
                      (current_count * 10) / 63347);

    return true;
}

uint64_t timer_get_ticks(void) {
    return ticks;
}

void timer_sleep_ms(uint64_t milliseconds) {
    if (hpet_is_available()) {
        hpet_sleep_ms(milliseconds);
    } else {
        volatile uint64_t i;
        for (i = 0; i < milliseconds * 1000; i++) {
            asm volatile("pause");
        }
    }
}

void timer_sleep_us(uint64_t microseconds) {
    if (hpet_is_available()) {
        hpet_sleep_us(microseconds);
    } else {
        volatile uint64_t i;
        for (i = 0; i < microseconds; i++) {
            asm volatile("pause");
        }
    }
}

void timer_sleep_ns(uint64_t nanoseconds) {
    if (hpet_is_available()) {
        hpet_sleep_ns(nanoseconds);
    } else {
        timer_sleep_us(nanoseconds / 1000);
    }
}
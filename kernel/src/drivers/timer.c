#include "../include/drivers/timer.h"
#include "../include/apic/apic.h"
#include "../include/io/serial.h"
#include "../include/interrupts/interrupts.h"
#include "../include/io/ports.h"
#include "../include/sched/task.h"

static volatile uint64_t ticks = 0;
static volatile uint64_t reschedule_count = 0;

DEFINE_IRQ(0x20, timer_handler)
{
    ticks++;
    lapic_eoi();

    uint32_t cpu = lapic_get_id();
    task_t* current = current_task[cpu];

    if (!current) return;

    if (current->time_slice > 0) {
        current->time_slice--;
    }

    if (current->time_slice == 0) {
        sched_reschedule();
    }

    (void)frame;
}

bool timer_init(void) {
    serial_writestring(COM1, "Initializing timer subsystem...\n");

    if (!apic_is_available()) {
        serial_writestring(COM1, "ERROR: APIC not available\n");
        return false;
    }

    apic_timer_calibrate();

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

    if (timer_config & LAPIC_TIMER_MASKED) {
        serial_writestring(COM1, "WARNING: APIC timer is masked! Unmasking...\n");
        lapic_write(LAPIC_TIMER, (timer_config & ~LAPIC_TIMER_MASKED));
    }

    uint64_t rflags;
    asm volatile("pushfq; pop %0" : "=r"(rflags));
    serial_printf(COM1, "RFLAGS: 0x%llx (IF=%s)\n", rflags, (rflags & 0x200) ? "ON" : "OFF");

    if (!(rflags & 0x200)) {
        serial_writestring(COM1, "WARNING: Interrupts are DISABLED! Enabling...\n");
        asm volatile("sti");
    }

    serial_writestring(COM1, "PREEMPTIVE SCHEDULING ENABLED!\n");
    serial_writestring(COM1, "DEBUG MODE: Will print timer info every 100 ticks\n");

    serial_printf(COM1, "Timer ticks at init: %llu\n", ticks);

    if (hpet_is_available()) {
        hpet_sleep_ms(100);
        serial_printf(COM1, "Timer ticks after 100ms: %llu\n", ticks);

        if (ticks == 0) {
            serial_writestring(COM1, "ERROR: Timer is NOT firing interrupts!\n");
            serial_writestring(COM1, "Check: IRQ handler registration, LAPIC setup, interrupts enabled\n");
        } else {
            serial_printf(COM1, "SUCCESS: Timer is working! (%llu ticks in 100ms)\n", ticks);
        }
    }

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
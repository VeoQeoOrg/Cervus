#include "../include/drivers/timer.h"
#include "../include/apic/apic.h"
#include "../include/io/serial.h"
#include "../include/interrupts/interrupts.h"
#include "../include/io/ports.h"
#include "../include/sched/sched.h"
#include "../include/smp/percpu.h"

static volatile uint64_t ticks = 0;

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

    bool force_resched = false;
    percpu_t* pc = get_percpu();
    if (pc && pc->need_resched) {
        pc->need_resched = false;
        force_resched = true;
    }

    if (current->time_slice == 0 || force_resched) {
        sched_reschedule();
    }

    (void)frame;
}

bool timer_init(void) {
    serial_writestring("Initializing timer subsystem...\n");

    if (!apic_is_available()) {
        serial_writestring("ERROR: APIC not available\n");
        return false;
    }

    apic_timer_calibrate();

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    serial_writestring("Timer subsystem initialized\n");

    if (hpet_is_available()) {
        serial_printf("HPET frequency: %llu Hz\n", hpet_get_frequency());
    } else {
        serial_writestring("HPET not available\n");
    }

    serial_writestring("Checking APIC Timer Status\n");

    uint32_t timer_config = lapic_read(LAPIC_TIMER);
    uint32_t initial_count = lapic_read(LAPIC_TIMER_ICR);
    uint32_t current_count = lapic_read(LAPIC_TIMER_CCR);

    serial_printf("APIC Timer Config: 0x%x\n", timer_config);
    serial_printf("  Vector: 0x%x\n", timer_config & 0xFF);
    serial_printf("  Masked: %s\n", (timer_config & LAPIC_TIMER_MASKED) ? "YES" : "NO");
    serial_printf("  Periodic: %s\n", (timer_config & LAPIC_TIMER_PERIODIC) ? "YES" : "NO");
    serial_printf("Initial Count: %u\n", initial_count);
    serial_printf("Current Count: %u\n", current_count);

    if (timer_config & LAPIC_TIMER_MASKED) {
        serial_writestring("WARNING: APIC timer is masked! Unmasking...\n");
        lapic_write(LAPIC_TIMER, (timer_config & ~LAPIC_TIMER_MASKED));
    }

    uint64_t rflags;
    asm volatile("pushfq; pop %0" : "=r"(rflags));
    serial_printf("RFLAGS: 0x%llx (IF=%s)\n", rflags, (rflags & 0x200) ? "ON" : "OFF");

    if (!(rflags & 0x200)) {
        serial_writestring("WARNING: Interrupts are DISABLED! Enabling...\n");
        asm volatile("sti");
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
#include "../include/drivers/timer.h"
#include "../include/time/clocksource.h"
#include "../include/apic/apic.h"
#include "../include/io/serial.h"
#include "../include/interrupts/interrupts.h"
#include "../include/io/ports.h"
#include "../include/sched/sched.h"
#include "../include/smp/percpu.h"
#include "../include/memory/pmm.h"
#include "../include/gdt/gdt.h"
#include <stdlib.h>

static volatile uint64_t ticks = 0;
volatile uint32_t g_ctrlc_pending = 0;
extern task_t* task_find_foreground(void);
extern void    task_kill(task_t* target);
extern void    task_kill_foreground_group(task_t* fg);
extern void    sched_wakeup_sleepers(uint64_t now_ns);

DEFINE_PER_CPU(uint32_t, g_stall_ctr) = 0;
DEFINE_PER_CPU(uint64_t, g_stall_seen) = 0;

static void tick_stall_check(uint32_t cpu)
{
    percpu_t *pc = get_percpu();
    if (!pc) return;
    uint32_t *ctr = per_cpu_ptr(g_stall_ctr, pc);
    if (++(*ctr) < 3000) return;
    *ctr = 0;
    uint64_t *seen = per_cpu_ptr(g_stall_seen, pc);
    uint64_t t = ticks;
    if (t != 0 && t == *seen) {
        serial_printf("[time] CPU%u: BSP tick stall detected (ticks=%llu), kicking via IPI\n",
                      cpu, (unsigned long long)t);
        smp_info_t *info = smp_get_info();
        lapic_send_ipi(info->bsp_lapic_id, 0x20);
    }
    *seen = t;
}

DEFINE_IRQ(0x20, timer_handler)
{
    if (apic_deadline_active()) apic_deadline_rearm();
    lapic_eoi();

    uint32_t cpu = smp_cpu_index();
    task_t* current = current_task[cpu];
    percpu_t* pc = get_percpu();

    if (cpu == 0) ticks++;
    else tick_stall_check(cpu);

    if (cpu == 0 && g_ctrlc_pending) {
        g_ctrlc_pending = 0;
        extern bool tty_has_isig_global(void);
        extern int  vt_active(void);
        extern void vt_write(int vt, const char *buf, size_t len);
        extern void tty_vt_input(int vt, char c);

        int  vt   = vt_active();
        bool isig = tty_has_isig_global();

        if (isig) {
            vt_write(vt, "^C\n", 3);
            task_t *fg = task_find_foreground();
            if (fg) task_kill_foreground_group(fg);
            else    tty_vt_input(vt, 0x03);
        } else {
            tty_vt_input(vt, 0x03);
        }
    }

    if (cpu == 0) {
        sched_wakeup_sleepers(sched_now_ns());
    }

    {
        extern void vt_tick_flush(void);
        vt_tick_flush();
    }

    if (cpu == 0) {
        extern void monitor_tick(void);
        monitor_tick();
        if ((ticks & 2047) == 0) clocksource_watchdog_tick();
    }

    if (!current) return;

    if (current->pending_kill && pc) {
        current->time_slice = 0;
        pc->need_resched = true;
        (void)frame;
        return;
    }

    if (current->time_slice > 0)
        current->time_slice--;

    if (current->time_slice == 0 && pc)
        pc->need_resched = true;

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

    clocksource_init();

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

static void tsc_recal_task(void *arg) {
    (void)arg;
    task_sleep_ms(2000);

    uint64_t h0 = hpet_elapsed_ns();
    uint64_t t0 = tsc_read();
    task_sleep_ms(1000);
    uint64_t h1 = hpet_elapsed_ns();
    uint64_t t1 = tsc_read();

    uint64_t dh = h1 - h0;
    if (dh < 500000000ULL) return;

    uint64_t new_khz = (t1 - t0) * 1000000ULL / dh;
    int64_t ppm = ((int64_t)new_khz - (int64_t)g_tsc_khz) * 1000000 / (int64_t)g_tsc_khz;
    serial_printf("[time] TSC recal vs HPET: %llu -> %llu kHz (%lld ppm)\n",
                  (unsigned long long)g_tsc_khz, (unsigned long long)new_khz,
                  (long long)ppm);

    if (ppm != 0 && ppm > -50000 && ppm < 50000)
        tsc_recalibrate(new_khz);
}

void timer_start_recal_task(void) {
    if (!hpet_is_available() || g_tsc_khz == 0 || !tsc_is_invariant()) return;
    task_create("tsc_recal", tsc_recal_task, NULL, 1);
}

uint64_t sched_now_ns(void) {
    return clocksource_now_ns();
}

void timer_sleep_ns(uint64_t nanoseconds) {
    uint64_t t0 = clocksource_now_ns();
    if (t0 == 0 && clocksource_now_ns() == 0) {
        volatile uint64_t i;
        for (i = 0; i < nanoseconds / 1000; i++)
            asm volatile("pause");
        return;
    }
    uint64_t t1 = t0 + nanoseconds;
    while (clocksource_now_ns() < t1) asm volatile("pause");
}

void timer_sleep_ms(uint64_t milliseconds) {
    timer_sleep_ns(milliseconds * 1000000ULL);
}

void timer_sleep_us(uint64_t microseconds) {
    timer_sleep_ns(microseconds * 1000ULL);
}

#include "../../include/time/ktimer.h"
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
DEFINE_PER_CPU(uint32_t, g_rlim_ctr) = 0;

#define NOHZ_MIN_NS 2000000ULL
#define NOHZ_MAX_NS 1000000000ULL
#define WATCHDOG_PERIOD_NS 2000000000ULL

extern volatile uint32_t sched_ready_flag;
extern uint64_t sched_next_wakeup_ns(void);
extern uint64_t ktimer_next_ns(void);
extern bool     vt_flush_pending(void);
extern uint64_t vt_blink_next_ns(void);
extern bool     monitor_wants_tick(void);
extern uint32_t smp_get_lapic_id_for_cpu(uint32_t cpu_index);

static volatile uint64_t g_nohz_until;
static uint64_t g_nohz_t0;
static uint64_t g_nohz_ticks0;
static uint64_t g_wd_last_ns;

static void nohz_catch_up(void)
{
    if (!g_nohz_t0) return;
    uint64_t want = g_nohz_ticks0 + (sched_now_ns() - g_nohz_t0) / 1000000ULL;
    if (want > ticks) ticks = want;
    g_nohz_t0 = 0;
}

void tick_nohz_idle_enter(uint32_t cpu)
{
    percpu_t *pc = get_percpu();
    if (!pc || !sched_ready_flag) return;
    uint64_t now = sched_now_ns();
    uint64_t next = (g_ctrlc_pending || vt_flush_pending() || clocksource_is_tick_based()) ? 0 : UINT64_MAX;
    if (cpu == 0) {
        nohz_catch_up();
        if (monitor_wants_tick()) next = 0;
        __atomic_store_n(&g_nohz_until, UINT64_MAX, __ATOMIC_SEQ_CST);
        uint64_t t = sched_next_wakeup_ns();
        if (t < next) next = t;
        t = ktimer_next_ns();
        if (t < next) next = t;
        t = vt_blink_next_ns();
        if (t < next) next = t;
        if (next > now + NOHZ_MAX_NS) next = now + NOHZ_MAX_NS;
    }
    if (next < now + NOHZ_MIN_NS) {
        if (cpu == 0) __atomic_store_n(&g_nohz_until, 0, __ATOMIC_SEQ_CST);
        apic_tick_restart();
        return;
    }
    if (cpu == 0) {
        __atomic_store_n(&g_nohz_until, next, __ATOMIC_SEQ_CST);
        g_nohz_t0 = now;
        g_nohz_ticks0 = ticks;
    }
    apic_tick_stop(next == UINT64_MAX ? 0 : next - now);
}

void tick_nohz_restart(void)
{
    percpu_t *pc = get_percpu();
    if (!pc || !pc->tick_stopped) return;
    if (smp_cpu_index() == 0) {
        nohz_catch_up();
        __atomic_store_n(&g_nohz_until, 0, __ATOMIC_SEQ_CST);
    }
    apic_tick_restart();
}

void tick_nohz_kick(uint64_t deadline_ns)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (deadline_ns >= __atomic_load_n(&g_nohz_until, __ATOMIC_SEQ_CST)) return;
    if (smp_cpu_index() == 0) return;
    ipi_reschedule_cpu(smp_get_lapic_id_for_cpu(0));
}

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
    percpu_t* pc = get_percpu();
    if (apic_deadline_active() && !(pc && pc->tick_stopped)) apic_deadline_rearm();
    lapic_eoi();

    uint32_t cpu = smp_cpu_index();
    task_t* current = current_task[cpu];
    if (current && current->prof_buf) profile_tick(current, frame->rip);

    if (cpu != 0) tick_stall_check(cpu);
    else if (g_nohz_t0) nohz_catch_up();
    else ticks++;

    extern int vt_kbd_muted(void);
    if (g_ctrlc_pending && __sync_bool_compare_and_swap(&g_ctrlc_pending, 1, 0) && !vt_kbd_muted()) {
        extern bool tty_echo_enabled(int vt);
        extern int  vt_active(void);
        extern void vt_write(int vt, const char *buf, size_t len);
        extern void tty_vt_inject_intr(int vt);
        extern void signal_send_subtree(task_t *root, int sig);

        int vt = vt_active();
        if (tty_echo_enabled(vt)) vt_write(vt, "^C\n", 3);
        task_t *fg = task_find_foreground();
        LOG_I("[tty] interrupt on vt%d: foreground=%u\n", vt, fg ? fg->pid : 0);
        if (fg) signal_send_subtree(fg, 2);
        else    tty_vt_inject_intr(vt);
    }

    if (cpu == 0) {
        uint64_t now = sched_now_ns();
        sched_wakeup_sleepers(now);
        ktimer_run(now);
    }

    {
        extern void vt_tick_flush(void);
        vt_tick_flush();
    }

    if (cpu == 0) {
        extern void monitor_tick(void);
        monitor_tick();
        uint64_t now = sched_now_ns();
        if (now - g_wd_last_ns >= WATCHDOG_PERIOD_NS) {
            g_wd_last_ns = now;
            clocksource_watchdog_tick();
        }
    }

    if (!current) return;

    if (current->is_userspace && current->rlim[RLIMIT_CPU_K].cur != RLIM_INF_K && pc &&
        ++(*per_cpu_ptr(g_rlim_ctr, pc)) % 100 == 0)
        sched_check_cpu_limit(current, sched_now_ns());

    if (current->pending_kill && pc) {
        current->time_slice = 0;
        pc->need_resched = true;
        (void)frame;
        return;
    }

    if (current->time_slice > 0 && current->policy != SCHED_FIFO_K)
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
    task_create("tsc_recal", tsc_recal_task, NULL, DEFAULT_PRIORITY);
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

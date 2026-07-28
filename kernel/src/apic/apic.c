#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include "../../include/io/ports.h"
#include "../../include/acpi/acpi.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/vmm.h"
#include "../../include/smp/percpu.h"
#include "../../include/sched/spinlock.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>

uintptr_t lapic_base = 0;
uintptr_t ioapic_base = 0;
uintptr_t hpet_base = 0;
uint32_t hpet_period = 0;
static acpi_madt_t* madt = NULL;
static acpi_hpet_t* hpet_table = NULL;

typedef struct {
    uint8_t  valid;
    uint8_t  source_irq;
    uint32_t gsi;
    uint16_t flags;
} irq_override_t;

#define MAX_IRQ_OVERRIDES 16
static irq_override_t g_irq_overrides[MAX_IRQ_OVERRIDES];
static int g_irq_override_count = 0;

typedef struct {
    uintptr_t base;
    uint32_t  gsi_base;
    uint32_t  pins;
    uint8_t   id;
} ioapic_dev_t;

#define MAX_IOAPICS 8
static ioapic_dev_t g_ioapics[MAX_IOAPICS];
static int g_ioapic_num = 0;

bool ioapic_resolve_gsi(uint32_t gsi, uintptr_t *base_out, uint32_t *pin_out) {
    for (int i = 0; i < g_ioapic_num; i++) {
        ioapic_dev_t *io = &g_ioapics[i];
        if (gsi >= io->gsi_base && gsi < io->gsi_base + io->pins) {
            if (base_out) *base_out = io->base;
            if (pin_out)  *pin_out  = gsi - io->gsi_base;
            return true;
        }
    }
    return false;
}

uint64_t g_hpet_boot_counter = 0;
uint64_t g_tsc_khz = 0;

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

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

    if (!hpet_table->counter_size)
        serial_printf("[APIC] HPET counter is 32-bit, software wrap extension active\n");

    uint64_t config = *(volatile uint64_t*)(hpet_base + HPET_CONFIG);
    config |= HPET_ENABLE_CNF;
    if (hpet_table->legacy_replacement) config |= HPET_LEGACY_CNF;
    *(volatile uint64_t*)(hpet_base + HPET_CONFIG) = config;

    for (volatile int _i = 0; _i < 1000; _i++) asm volatile("pause");
    g_hpet_boot_counter = hpet_read_counter();

    return true;
}

bool hpet_is_available(void) {
    return hpet_base != 0 && hpet_period != 0;
}

static spinlock_t g_hpet32_lock = SPINLOCK_INIT;
static uint64_t g_hpet32_hi   = 0;
static uint32_t g_hpet32_last = 0;

uint64_t hpet_read_counter(void) {
    if (!hpet_base) return 0;

    if (hpet_table->counter_size) {
        return *(volatile uint64_t*)(hpet_base + HPET_MAIN_COUNTER);
    }

    uint32_t now = *(volatile uint32_t*)(hpet_base + HPET_MAIN_COUNTER);
    uint64_t f = spinlock_acquire_irqsave(&g_hpet32_lock);
    if (now < g_hpet32_last) g_hpet32_hi += (1ULL << 32);
    g_hpet32_last = now;
    uint64_t v = g_hpet32_hi | (uint64_t)now;
    spinlock_release_irqrestore(&g_hpet32_lock, f);
    return v;
}

uint64_t hpet_get_frequency(void) {
    if (!hpet_period) return 0;
    return 1000000000000000ULL / hpet_period;
}

uint64_t hpet_elapsed_ns(void) {
    if (!hpet_is_available()) return 0;
    uint64_t delta = hpet_read_counter() - g_hpet_boot_counter;
    return (delta * (uint64_t)hpet_period) / 1000000ULL;
}

uint64_t g_tsc_boot = 0;

uint64_t tsc_elapsed_ns(void) {
    if (g_tsc_khz == 0 || g_tsc_boot == 0) return 0;
    uint64_t delta = rdtsc() - g_tsc_boot;
    uint64_t mhz = g_tsc_khz / 1000;
    if (mhz == 0) mhz = 1;
    return (delta * 1000ULL) / mhz;
}

uint64_t tsc_read(void) {
    return rdtsc();
}

void tsc_recalibrate(uint64_t new_khz) {
    if (new_khz == 0 || g_tsc_khz == 0) return;
    uint64_t cur = tsc_elapsed_ns();
    uint64_t mhz = new_khz / 1000;
    if (mhz == 0) mhz = 1;
    uint64_t now = rdtsc();
    g_tsc_khz  = new_khz;
    g_tsc_boot = now - (cur * mhz) / 1000ULL;
}

#define MSR_TSC_DEADLINE     0x6E0
#define LAPIC_TIMER_TSC_DDL  (1u << 18)

static bool g_deadline_mode = false;

DEFINE_PER_CPU(uint64_t, g_ddl_next) = 0;

static inline void apic_wrmsr(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val),
                            "d"((uint32_t)(val >> 32)));
}

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}

bool tsc_is_invariant(void) {
    uint32_t a, b, c, d;
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a < 0x80000007) return false;
    cpuid(0x80000007, &a, &b, &c, &d);
    return (d & (1u << 8)) != 0;
}

bool tsc_deadline_supported(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(c & (1u << 24))) return false;
    return tsc_is_invariant();
}

bool apic_deadline_active(void) {
    return g_deadline_mode;
}

void apic_deadline_rearm(void) {
    uint64_t period = g_tsc_khz;
    if (period == 0) return;

    percpu_t *pc = get_percpu();
    uint64_t now = rdtsc();
    uint64_t next;

    if (pc) {
        uint64_t *slot = per_cpu_ptr(g_ddl_next, pc);
        next = *slot + period;
        if (next <= now || next > now + period * 4)
            next = now + period;
        *slot = next;
    } else {
        next = now + period;
    }
    apic_wrmsr(MSR_TSC_DEADLINE, next);
}

static void apic_deadline_start(uint32_t vector) {
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_TIMER, (vector & 0xFF) | LAPIC_TIMER_TSC_DDL);
    asm volatile("mfence" ::: "memory");
    apic_wrmsr(MSR_TSC_DEADLINE, rdtsc() + g_tsc_khz);
}

void hpet_sleep_ns(uint64_t nanoseconds) {
    if (!hpet_base || !hpet_period) {
        uint64_t t0 = tsc_elapsed_ns();
        if (t0) {
            uint64_t t1 = t0 + nanoseconds;
            while (tsc_elapsed_ns() < t1) asm volatile("pause");
            return;
        }
        for (volatile uint64_t i = 0; i < nanoseconds / 100 + 1; i++)
            asm volatile("pause");
        return;
    }

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
                if (g_ioapic_num < MAX_IOAPICS) {
                    ioapic_dev_t *io = &g_ioapics[g_ioapic_num++];
                    io->base     = phys_to_virt(ioapic_entry->ioapic_address);
                    io->gsi_base = ioapic_entry->global_system_interrupt_base;
                    io->pins     = ioapic_get_max_redirects(io->base);
                    io->id       = ioapic_entry->ioapic_id;
                    serial_printf("[APIC] IOAPIC #%d id=%u addr=0x%x gsi_base=%u pins=%u\n",
                                  g_ioapic_num, io->id, ioapic_entry->ioapic_address,
                                  io->gsi_base, io->pins);
                    if (io->gsi_base == 0 || !ioapic_base)
                        ioapic_base = io->base;
                }
                break;
            }

            case MADT_ENTRY_ISO: {
                madt_iso_entry_t* iso = (madt_iso_entry_t*)entries;
                if (g_irq_override_count < MAX_IRQ_OVERRIDES) {
                    irq_override_t *o = &g_irq_overrides[g_irq_override_count++];
                    o->valid       = 1;
                    o->source_irq  = iso->source;
                    o->gsi         = iso->global_system_interrupt;
                    o->flags       = iso->flags;
                    serial_printf("[APIC] ISO: IRQ %u -> GSI %u flags=0x%04x\n",
                                  iso->source, iso->global_system_interrupt, iso->flags);
                }
                break;
            }

            default:
                break;
        }

        entries += header->length;
    }
}

static const irq_override_t *find_irq_override(uint8_t irq) {
    for (int i = 0; i < g_irq_override_count; i++) {
        if (g_irq_overrides[i].valid && g_irq_overrides[i].source_irq == irq)
            return &g_irq_overrides[i];
    }
    return NULL;
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

    outb(0x22, 0x70);
    outb(0x23, 0x01);
    serial_printf("[APIC] IMCR: PIC mode -> Symmetric I/O (APIC) mode (pcat=%u)\n",
                  (unsigned)(madt->flags & 1));

    for (int i = 0; i < g_ioapic_num; i++) {
        ioapic_dev_t *io = &g_ioapics[i];
        for (uint32_t p = 0; p < io->pins; p++) {
            uint32_t reg = IOAPIC_REDIR_START + p * 2;
            ioapic_write(io->base, reg, ioapic_read(io->base, reg) | IOAPIC_INT_MASKED);
        }
    }
}

void apic_setup_irq(uint8_t irq, uint8_t vector, bool mask, uint32_t flags) {
    if (!ioapic_base) return;

    uint32_t gsi = irq;
    uint32_t redir_flags = IOAPIC_DELIVERY_FIXED | flags;

    const irq_override_t *ov = find_irq_override(irq);
    if (ov) {
        gsi = ov->gsi;
        uint8_t polarity = ov->flags & 0x3;
        uint8_t trigger  = (ov->flags >> 2) & 0x3;
        if (polarity == 0x3) redir_flags |= IOAPIC_POLARITY_LOW;
        if (trigger  == 0x3) redir_flags |= IOAPIC_TRIGGER_LEVEL;
        serial_printf("[APIC] IRQ %u -> GSI %u (polarity=%u trigger=%u)\n",
                      irq, gsi, polarity, trigger);
    }

    if (mask) redir_flags |= IOAPIC_INT_MASKED;

    ioapic_redirect_irq(gsi, vector, redir_flags);
}

void apic_dump_diag(void) {
    printf("[apicdiag] madt flags=0x%x pcat=%u  lapic raw_id=0x%x get_id=%u\n",
           madt ? madt->flags : 0, madt ? (unsigned)(madt->flags & 1) : 0,
           lapic_read(LAPIC_ID), lapic_get_id());
    serial_printf("[apicdiag] madt flags=0x%x pcat=%u lapic raw_id=0x%x get_id=%u\n",
                  madt ? madt->flags : 0, madt ? (unsigned)(madt->flags & 1) : 0,
                  lapic_read(LAPIC_ID), lapic_get_id());
    if (g_ioapic_num > 0) {
        printf("[apicdiag] ioapic count=%d\n", g_ioapic_num);
        serial_printf("[apicdiag] ioapic count=%d\n", g_ioapic_num);
        for (int i = 0; i < g_ioapic_num; i++) {
            ioapic_dev_t *io = &g_ioapics[i];
            uint32_t id  = ioapic_read(io->base, IOAPIC_ID);
            uint32_t ver = ioapic_read(io->base, IOAPIC_VERSION);
            printf("[apicdiag]  ioapic[%d] id=%u ver=0x%x gsi=%u..%u pins=%u\n",
                   i, (id >> 24) & 0xF, ver & 0xFF,
                   io->gsi_base, io->gsi_base + io->pins - 1, io->pins);
            serial_printf("[apicdiag]  ioapic[%d] id=%u ver=0x%x gsi=%u..%u pins=%u\n",
                   i, (id >> 24) & 0xF, ver & 0xFF,
                   io->gsi_base, io->gsi_base + io->pins - 1, io->pins);
        }
    } else {
        printf("[apicdiag] NO IOAPIC BASE!\n");
        serial_printf("[apicdiag] NO IOAPIC BASE!\n");
    }
    for (int i = 0; i < g_irq_override_count; i++) {
        irq_override_t *o = &g_irq_overrides[i];
        printf("[apicdiag] ISO src=%u -> gsi=%u flags=0x%x\n",
               o->source_irq, o->gsi, o->flags);
        serial_printf("[apicdiag] ISO src=%u -> gsi=%u flags=0x%x\n",
                      o->source_irq, o->gsi, o->flags);
    }
}

void apic_dump_irq(uint8_t irq) {
    uint32_t gsi = irq;
    const irq_override_t *ov = find_irq_override(irq);
    if (ov) gsi = ov->gsi;

    uintptr_t base;
    uint32_t pin;
    if (!ioapic_resolve_gsi(gsi, &base, &pin)) {
        printf("  IRQ%u->GSI%u: NO IOAPIC\n", irq, gsi);
        serial_printf("[APIC] dump IRQ%u->GSI%u: no IOAPIC\n", irq, gsi);
        return;
    }

    uint32_t reg  = IOAPIC_REDIR_START + pin * 2;
    uint32_t low  = ioapic_read(base, reg);
    uint32_t high = ioapic_read(base, reg + 1);

    uint8_t vec = (uint8_t)(low & 0xFF);
    uint8_t pol = (uint8_t)((low >> 13) & 1);
    uint8_t trg = (uint8_t)((low >> 15) & 1);
    uint8_t msk = (uint8_t)((low >> 16) & 1);
    uint8_t dst = (uint8_t)((high >> 24) & 0xFF);

    uint32_t bsp = lapic_get_id();
    printf("  IRQ%u->GSI%u vec=0x%02x %s %s mask=%u dest=%u bsp_apic=%u %s iso=%s\n",
           irq, gsi, vec, pol ? "ACT-LOW" : "act-high",
           trg ? "LEVEL" : "edge", msk, dst, bsp,
           (dst == bsp) ? "OK" : "MISMATCH!", ov ? "YES" : "no");
    serial_printf("[APIC] IRQ%u->GSI%u vec=0x%02x pol=%s trig=%s mask=%u dest=%u bsp=%u%s iso=%s "
                  "(low=0x%08x high=0x%08x)\n",
                  irq, gsi, vec, pol ? "LOW" : "high", trg ? "level" : "edge",
                  msk, dst, bsp, (dst == bsp) ? "" : " MISMATCH", ov ? "yes" : "no", low, high);
}

static uint32_t apic_calibrate_pit_once(uint64_t *out_tsc_khz) {
    const uint32_t PIT_HZ      = 1193182;
    const uint32_t WAIT_MS     = 50;
    uint32_t pit_count = (uint32_t)((uint64_t)PIT_HZ * WAIT_MS / 1000);
    if (pit_count > 0xFFFF) pit_count = 0xFFFF;

    uint8_t pcb = inb(0x61);
    pcb = (pcb & ~0x02) | 0x01;
    outb(0x61, pcb);

    outb(0x43, 0xB0);
    outb(0x42, (uint8_t)(pit_count & 0xFF));
    outb(0x42, (uint8_t)((pit_count >> 8) & 0xFF));

    uint8_t t = inb(0x61) & ~0x01;
    outb(0x61, t);
    outb(0x61, t | 0x01);

    lapic_write(LAPIC_TIMER_DCR, 0x3);
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED | 0xFF);
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);

    uint64_t tsc_start = rdtsc();

    while (!(inb(0x61) & 0x20)) asm volatile("pause");

    uint64_t tsc_end = rdtsc();
    uint32_t remaining = lapic_read(LAPIC_TIMER_CCR);
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED | 0xFF);

    if (out_tsc_khz && tsc_end > tsc_start)
        *out_tsc_khz = (tsc_end - tsc_start) / WAIT_MS;

    if (remaining == 0 || remaining == 0xFFFFFFFF) return 0;

    uint32_t ticks_elapsed = 0xFFFFFFFF - remaining;
    return ticks_elapsed / WAIT_MS;
}

static uint32_t apic_calibrate_pit(void) {
    uint64_t best_khz   = 0;
    uint32_t best_ticks = 0;
    for (int round = 0; round < 3; round++) {
        uint64_t khz = 0;
        uint32_t ticks = apic_calibrate_pit_once(&khz);
        if (khz && (best_khz == 0 || khz < best_khz))
            best_khz = khz;
        if (ticks && (best_ticks == 0 || ticks < best_ticks))
            best_ticks = ticks;
    }
    if (best_khz && g_tsc_khz == 0)
        g_tsc_khz = best_khz;
    return best_ticks;
}

void tsc_calibrate_bsp(void) {
    if (g_tsc_khz != 0) return;
    uint64_t best = 0;
    for (int round = 0; round < 3; round++) {
        uint64_t khz = 0;
        (void)apic_calibrate_pit_once(&khz);
        if (khz && (best == 0 || khz < best)) best = khz;
    }
    if (best) {
        g_tsc_khz  = best;
        g_tsc_boot = rdtsc();
        serial_printf("[APIC] TSC calibrated on BSP: %llu kHz (min of 3 rounds)\n",
                      (unsigned long long)best);
    } else {
        serial_printf("[APIC] WARNING: BSP TSC calibration failed\n");
    }
}

static uint32_t apic_calibrate_lapic_via_tsc(void) {
    if (g_tsc_khz == 0) return 0;

    const uint32_t WAIT_MS = 10;
    uint64_t tsc_window = g_tsc_khz * WAIT_MS;

    lapic_write(LAPIC_TIMER_DCR, 0x3);
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED | 0xFF);
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);

    uint64_t tsc_start = rdtsc();
    while (rdtsc() - tsc_start < tsc_window) asm volatile("pause");

    uint32_t remaining = lapic_read(LAPIC_TIMER_CCR);
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED | 0xFF);

    if (remaining == 0 || remaining == 0xFFFFFFFF) return 0;
    return (0xFFFFFFFF - remaining) / WAIT_MS;
}

void apic_timer_calibrate(void) {
    uint32_t ticks_per_1ms = 0;

    if (g_tsc_khz != 0) {
        ticks_per_1ms = apic_calibrate_lapic_via_tsc();
    }

    if (ticks_per_1ms == 0 && hpet_is_available()) {
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

        if (remaining != 0 && remaining != 0xFFFFFFFF) {
            uint32_t ticks_elapsed = 0xFFFFFFFF - remaining;
            ticks_per_1ms = (uint32_t)((ticks_elapsed * 1000000ULL) / measurement_time_ns);
        }
    }

    if (ticks_per_1ms == 0 || g_tsc_khz == 0) {
        if (ticks_per_1ms == 0)
            serial_printf("[APIC] no TSC/HPET base; calibrating timer via PIT\n");
        uint32_t pit_ticks = apic_calibrate_pit();
        if (ticks_per_1ms == 0) ticks_per_1ms = pit_ticks;
    }

    if (ticks_per_1ms == 0) {
        serial_printf("[APIC] WARNING: timer calibration failed; using fallback count\n");
        ticks_per_1ms = 10000;
    }

    if (g_tsc_boot == 0 && g_tsc_khz != 0)
        g_tsc_boot = rdtsc();

    if (g_tsc_khz != 0 && tsc_deadline_supported()) {
        g_deadline_mode = true;
        serial_printf("[APIC] LAPIC timer: TSC-deadline mode (TSC %llu kHz)\n",
                      (unsigned long long)g_tsc_khz);
        apic_deadline_start(0x20);
        return;
    }

    serial_printf("[APIC] LAPIC timer: %u ticks/ms periodic (TSC %llu kHz)\n",
                  ticks_per_1ms, (unsigned long long)g_tsc_khz);

    lapic_timer_init(0x20, ticks_per_1ms, true, 0x3);
}
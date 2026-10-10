#include "../../include/time/clocksource.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <stdint.h>
#include <stdbool.h>

#define KVM_CPUID_SIGNATURE                0x40000000u
#define KVM_CPUID_FEATURES                 0x40000001u
#define KVM_FEATURE_CLOCKSOURCE            (1u << 0)
#define KVM_FEATURE_CLOCKSOURCE2           (1u << 3)
#define KVM_FEATURE_CLOCKSOURCE_STABLE_BIT (1u << 24)
#define MSR_KVM_SYSTEM_TIME                0x12u
#define MSR_KVM_SYSTEM_TIME_NEW            0x4b564d01u
#define PVCLOCK_TSC_STABLE_BIT             (1u << 0)

typedef struct {
    uint32_t version;
    uint32_t pad0;
    uint64_t tsc_timestamp;
    uint64_t system_time;
    uint32_t tsc_to_system_mul;
    int8_t   tsc_shift;
    uint8_t  flags;
    uint8_t  pad[2];
} __attribute__((packed)) pvclock_vcpu_time_info_t;

_Static_assert(sizeof(pvclock_vcpu_time_info_t) == 32, "pvclock_vcpu_time_info size");

static volatile pvclock_vcpu_time_info_t *g_pvti;

static inline void kvm_cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static inline void kvm_wrmsr(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static inline uint64_t rdtsc_ordered(void) {
    uint32_t lo, hi;
    asm volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t kvmclock_read(void) {
    volatile pvclock_vcpu_time_info_t *p = g_pvti;
    uint32_t version;
    uint64_t ns;
    do {
        version = p->version;
        asm volatile("" ::: "memory");
        uint64_t delta = rdtsc_ordered() - p->tsc_timestamp;
        int8_t shift = p->tsc_shift;
        if (shift < 0) delta >>= -shift;
        else delta <<= shift;
        ns = p->system_time +
             (uint64_t)(((unsigned __int128)delta * p->tsc_to_system_mul) >> 32);
        asm volatile("" ::: "memory");
    } while ((version & 1) || version != p->version);
    return ns;
}

static bool kvmclock_available(void) { return g_pvti != NULL; }

static clocksource_t cs_kvmclock = {
    .name = "kvm-clock", .rating = 400,
    .available = kvmclock_available, .read_ns = kvmclock_read,
};

void kvmclock_init(void) {
    uint32_t a, b, c, d;
    kvm_cpuid(1, &a, &b, &c, &d);
    if (!(c & (1u << 31))) return;

    kvm_cpuid(KVM_CPUID_SIGNATURE, &a, &b, &c, &d);
    if (b != 0x4b4d564bu || c != 0x564b4d56u || d != 0x0000004du) return;
    if (a < KVM_CPUID_FEATURES) a = KVM_CPUID_FEATURES;

    kvm_cpuid(KVM_CPUID_FEATURES, &a, &b, &c, &d);
    uint32_t msr;
    if (a & KVM_FEATURE_CLOCKSOURCE2) msr = MSR_KVM_SYSTEM_TIME_NEW;
    else if (a & KVM_FEATURE_CLOCKSOURCE) msr = MSR_KVM_SYSTEM_TIME;
    else return;

    if (!(a & KVM_FEATURE_CLOCKSOURCE_STABLE_BIT)) {
        serial_printf("[time] kvm-clock: host TSC is not stable, not used\n");
        return;
    }

    void *page = pmm_alloc_zero(1);
    if (!page) return;
    kvm_wrmsr(msr, (uint64_t)pmm_virt_to_phys(page) | 1);
    volatile pvclock_vcpu_time_info_t *p = page;

    if (!(p->flags & PVCLOCK_TSC_STABLE_BIT) || !p->tsc_to_system_mul) {
        kvm_wrmsr(msr, 0);
        pmm_free(page, 1);
        serial_printf("[time] kvm-clock: no stable master clock, not used\n");
        return;
    }

    g_pvti = p;
    clocksource_register(&cs_kvmclock);
    serial_printf("[time] kvm-clock: paravirtual clock available (rating 400)\n");
}

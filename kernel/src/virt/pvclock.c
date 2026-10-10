#include "../../include/virt/vmx.h"
#include "../../include/virt/kvm.h"
#include "../../include/time/clocksource.h"
#include "../../include/apic/apic.h"
#include "../../include/drivers/timer.h"
#include <string.h>

extern int64_t clock_realtime_offset_ns(void);

#define PVCLOCK_TSC_STABLE_BIT 1u

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

typedef struct {
    uint32_t version;
    uint32_t sec;
    uint32_t nsec;
} __attribute__((packed)) pvclock_wall_clock_t;

_Static_assert(sizeof(pvclock_vcpu_time_info_t) == 32, "pvclock_vcpu_time_info layout");
_Static_assert(sizeof(pvclock_wall_clock_t) == 12, "pvclock_wall_clock layout");

static inline uint64_t rdtsc_now(void) {
    uint32_t lo, hi;
    asm volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static void time_scale(uint64_t scaled_hz, uint64_t base_hz, int8_t *pshift, uint32_t *pmul) {
    int32_t shift = 0;
    uint64_t tps64 = base_hz, scaled64 = scaled_hz;
    while (tps64 > scaled64 * 2 || (tps64 & 0xFFFFFFFF00000000ull)) {
        tps64 >>= 1;
        shift--;
    }
    uint32_t tps32 = (uint32_t)tps64;
    while (tps32 <= scaled64 || (scaled64 & 0xFFFFFFFF00000000ull)) {
        if ((scaled64 & 0xFFFFFFFF00000000ull) || (tps32 & 0x80000000u)) scaled64 >>= 1;
        else tps32 <<= 1;
        shift++;
    }
    *pshift = (int8_t)shift;
    *pmul = (uint32_t)((scaled64 << 32) / tps32);
}

static uint64_t scale_delta(uint64_t delta, uint32_t mul, int8_t shift) {
    if (shift < 0) delta >>= -shift;
    else delta <<= shift;
    return (uint64_t)(((unsigned __int128)delta * mul) >> 32);
}

bool kvm_host_tsc_stable(void) {
    return tsc_is_invariant() || kvmclock_in_use();
}

void kvm_clock_init_vm(kvm_vm_t *vm) {
    uint64_t tsc = rdtsc_now();
    vm->clock_master_tsc = tsc;
    vm->clock_master_ns = 0;
    vm->tsc_offset = -(int64_t)tsc;
    vm->clock_stable = kvm_host_tsc_stable();
    time_scale(1000000000ull, g_tsc_khz * 1000ull, &vm->clock_shift, &vm->clock_mul);
    vm->clock_gen = 1;
}

static uint64_t clock_read_locked(kvm_vm_t *vm, uint64_t tsc) {
    return vm->clock_master_ns + scale_delta(tsc - vm->clock_master_tsc, vm->clock_mul, vm->clock_shift);
}

uint64_t kvm_clock_read(kvm_vm_t *vm) {
    uint64_t f = spinlock_acquire_irqsave(&vm->clock_lock);
    uint64_t ns = clock_read_locked(vm, rdtsc_now());
    spinlock_release_irqrestore(&vm->clock_lock, f);
    return ns;
}

void kvm_clock_set(kvm_vm_t *vm, uint64_t ns) {
    uint64_t f = spinlock_acquire_irqsave(&vm->clock_lock);
    vm->clock_master_tsc = rdtsc_now();
    vm->clock_master_ns = ns;
    vm->clock_gen++;
    spinlock_release_irqrestore(&vm->clock_lock, f);
}

int64_t kvm_tsc_offset_for_write(kvm_vcpu_t *v, uint64_t data, bool host) {
    kvm_vm_t *vm = v->vm;
    int64_t off = (int64_t)(data - rdtsc_now());
    if (!host) return off;
    uint64_t now = sched_now_ns();
    uint64_t f = spinlock_acquire_irqsave(&vm->clock_lock);
    if (vm->tsc_written && data == vm->tsc_last_value && now - vm->tsc_last_ns < 1000000000ull) {
        off = vm->tsc_offset;
    } else {
        vm->tsc_offset = off;
        vm->tsc_last_value = data;
        vm->tsc_last_ns = now;
        vm->tsc_written = true;
        vm->clock_gen++;
    }
    spinlock_release_irqrestore(&vm->clock_lock, f);
    return off;
}

void kvm_clock_sync(kvm_vcpu_t *v) {
    kvm_vm_t *vm = v->vm;
    if (v->wall_clock_pending) {
        v->wall_clock_pending = false;
        int64_t wall = (int64_t)sched_now_ns() + clock_realtime_offset_ns() - (int64_t)kvm_clock_read(vm);
        if (wall < 0) wall = 0;
        pvclock_wall_clock_t w;
        uint32_t ver = 0;
        kvm_read_guest(vm, v->wall_clock_gpa, &ver, sizeof ver);
        w.version = (ver + 2) & ~1u;
        w.sec = (uint32_t)((uint64_t)wall / 1000000000ull);
        w.nsec = (uint32_t)((uint64_t)wall % 1000000000ull);
        kvm_write_guest(vm, v->wall_clock_gpa, &w, sizeof w);
    }
    if (!v->pvclock_gpa) return;
    uint32_t gen = __atomic_load_n(&vm->clock_gen, __ATOMIC_ACQUIRE);
    if (!v->pvclock_dirty && v->pvclock_gen == gen) return;
    pvclock_vcpu_time_info_t p;
    memset(&p, 0, sizeof p);
    uint64_t f = spinlock_acquire_irqsave(&vm->clock_lock);
    gen = vm->clock_gen;
    p.tsc_timestamp = vm->clock_master_tsc + (uint64_t)v->tsc_offset;
    p.system_time = vm->clock_master_ns;
    p.tsc_to_system_mul = vm->clock_mul;
    p.tsc_shift = vm->clock_shift;
    p.flags = vm->clock_stable ? PVCLOCK_TSC_STABLE_BIT : 0;
    spinlock_release_irqrestore(&vm->clock_lock, f);
    v->pvclock_version += 2;
    p.version = v->pvclock_version;
    if (kvm_write_guest(vm, v->pvclock_gpa, &p, sizeof p) == 0) {
        v->pvclock_gen = gen;
        v->pvclock_dirty = false;
    }
}

#include "../../include/smp/percpu.h"
#include "../../include/smp/smp.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>

extern uintptr_t __percpu_start;
extern uintptr_t __percpu_end;

PERCPU_SECTION percpu_t percpu = {0};
PERCPU_SECTION int dummy_percpu = 0xDEADBEEF;

percpu_t* percpu_regions[MAX_CPUS] = {0};

bool g_has_fsgsbase = false;

#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

static inline uint64_t rdmsr_local(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr_local(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static bool detect_fsgsbase(void) {
    uint32_t eax = 7, ebx, ecx = 0, edx;
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "0"(eax), "2"(ecx));
    return (ebx & (1u << 0)) != 0;
}

void init_percpu_regions(void) {
    g_has_fsgsbase = detect_fsgsbase();
    serial_printf("[PerCPU] FSGSBASE: %s\n", g_has_fsgsbase ? "YES" : "NO (using MSR fallback)");

    size_t percpu_size = &__percpu_end - &__percpu_start;
    serial_printf("PerCPU size: %zu bytes\n", percpu_size);

    smp_info_t* info = smp_get_info();
    for (uint32_t i = 0; i < info->cpu_count; i++) {
        void* region = malloc(percpu_size);
        if (!region) {
            serial_printf("PerCPU: Alloc failed for CPU %u\n", i);
            continue;
        }
        memset(region, 0, percpu_size);
        memcpy(region, &__percpu_start, percpu_size);

        percpu_regions[i] = (percpu_t*)region;
        percpu_regions[i]->cpu_id = info->cpus[i].lapic_id;
        percpu_regions[i]->syscall_kernel_rsp = 0;
        percpu_regions[i]->syscall_user_rsp   = 0;

        serial_printf("PerCPU region for CPU %u at 0x%llx\n", i, (uint64_t)region);
    }
}

percpu_t* get_percpu(void) {
    uint64_t gs_base;
    if (g_has_fsgsbase) {
        asm volatile("rdgsbase %0" : "=r"(gs_base));
    } else {
        gs_base = rdmsr_local(MSR_GS_BASE);
    }
    if (gs_base == 0) return NULL;
    return (percpu_t*)gs_base;
}

percpu_t* get_percpu_mut(void) {
    return get_percpu();
}

void set_percpu_base(percpu_t* base) {
    uint64_t val = (uint64_t)base;
    if (g_has_fsgsbase) {
        asm volatile("wrgsbase %0" :: "r"(val) : "memory");
    } else {
        wrmsr_local(MSR_GS_BASE, val);
    }
    wrmsr_local(MSR_KERNEL_GS_BASE, val);
}

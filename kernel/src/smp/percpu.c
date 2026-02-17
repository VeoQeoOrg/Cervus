#include "../../include/smp/percpu.h"
#include "../../include/smp/smp.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <string.h>

extern uintptr_t __percpu_start;
extern uintptr_t __percpu_end;

PERCPU_SECTION percpu_t percpu = {0};
PERCPU_SECTION int dummy_percpu = 0xDEADBEEF;

percpu_t* percpu_regions[MAX_CPUS] = {0};

void init_percpu_regions(void) {
    size_t percpu_size = &__percpu_end - &__percpu_start;
    serial_printf("PerCPU size: %zu bytes\n", percpu_size);

    smp_info_t* info = smp_get_info();
    for (uint32_t i = 0; i < info->cpu_count; i++) {
        void* region = pmm_alloc((percpu_size + PAGE_SIZE - 1) / PAGE_SIZE);
        if (!region) {
            serial_printf("PerCPU: Alloc failed for CPU %u\n", i);
            continue;
        }
        memset(region, 0, percpu_size);
        memcpy(region, &__percpu_start, percpu_size);

        percpu_regions[i] = (percpu_t*)region;
        percpu_regions[i]->cpu_id = info->cpus[i].lapic_id;

        serial_printf("PerCPU region for CPU %u at 0x%llx\n", i, (uint64_t)region);
    }
}

percpu_t* get_percpu(void) {
    uint64_t gs_base;
    asm volatile ("rdgsbase %0" : "=r"(gs_base));
    if (gs_base == 0) {
        return NULL;
    }
    return (percpu_t*)gs_base;
}

percpu_t* get_percpu_mut(void) {
    return get_percpu();
}

void set_percpu_base(percpu_t* base) {
    __asm__ volatile (
        "wrgsbase %0"
        :
        : "r"(base)
        : "memory"
    );
}

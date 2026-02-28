#include "../../include/syscall/syscall.h"
#include "../../include/sched/sched.h"
#include "../../include/smp/smp.h"
#include "../../include/smp/percpu.h"
#include "../../include/apic/apic.h"
#include "../../include/gdt/gdt.h"
#include "../../include/memory/vmm.h"
#include "../../include/io/serial.h"
#include <stdint.h>
#include <string.h>

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084
#define EFER_SCE   (1ULL << 0)

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t val) {
    asm volatile ("wrmsr" :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static int copy_from_user(void* dst, const void* src, size_t n) {
    if ((uintptr_t)src >= 0x800000000000ULL) return -1;
    if ((uintptr_t)src + n > 0x800000000000ULL) return -1;
    memcpy(dst, src, n);
    return 0;
}

static int64_t sys_write(uint64_t fd, const char* ubuf, uint64_t count) {
    if (fd != 1 && fd != 2) return -9;
    if (count == 0) return 0;
    if (count > 4096) count = 4096;

    char kbuf[4097];
    if (copy_from_user(kbuf, ubuf, count) < 0) {
        serial_printf("[SYSCALL] write: EFAULT buf=0x%llx count=%llu\n",
                      (uint64_t)ubuf, count);
        return -14;
    }
    kbuf[count] = '\0';

    serial_writestring("[USER] ");
    for (uint64_t i = 0; i < count; i++)
        serial_write(kbuf[i]);

    return (int64_t)count;
}

static int64_t sys_exit(uint64_t code) {
    uint32_t cpu = lapic_get_id();
    serial_printf("[SYSCALL] exit(%llu) — task '%s' on CPU %u\n",
                  code,
                  current_task[cpu] ? current_task[cpu]->name : "?",
                  cpu);
    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    task_exit();
    return 0;
}

int64_t syscall_handler_c(uint64_t nr,
                           uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    switch (nr) {
        case SYS_WRITE: return sys_write(a1, (const char*)a2, a3);
        case SYS_EXIT:  return sys_exit(a1);
        default:
            serial_printf("[SYSCALL] unknown nr=%llu\n", nr);
            return -38;
    }
}

void syscall_init(void) {
    extern void syscall_entry(void);

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    uint64_t star = ((uint64_t)GDT_STAR_SYSRET_BASE << 48)
                  | ((uint64_t)GDT_STAR_SYSCALL_CS  << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, (1U << 9) | (1U << 10));
    percpu_t* pc = get_percpu();
    if (!pc) {
        serial_printf("[SYSCALL] WARNING: percpu not set, skipping kernel_rsp init\n");
        return;
    }

    extern tss_t* tss[MAX_CPUS];
    cpu_info_t* cpu_info = smp_get_current_cpu();
    if (!cpu_info) {
        serial_printf("[SYSCALL] WARNING: smp_get_current_cpu() returned NULL\n");
        return;
    }

    uint32_t cpu_index = cpu_info->cpu_index;
    if (cpu_index < MAX_CPUS && tss[cpu_index]) {
        pc->syscall_kernel_rsp = tss[cpu_index]->rsp0;
        serial_printf("[SYSCALL] CPU %u (index %u): kernel_rsp=0x%llx\n",
                      pc->cpu_id, cpu_index, pc->syscall_kernel_rsp);
    } else {
        serial_printf("[SYSCALL] WARNING: tss[%u] not ready\n", cpu_index);
    }
}
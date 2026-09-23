#include "../../include/memory/vmalloc.h"
#include "../../include/memory/vmm.h"
#include "../../include/memory/pmm.h"
#include "../../include/apic/apic.h"
#include "../../include/smp/smp.h"
#include "../../include/sched/spinlock.h"
#include "../../include/io/serial.h"

#define VMALLOC_BASE    0xFFFFFC0000000000ULL
#define VMALLOC_GRANULE 0x200000ULL
#define VMALLOC_SLOTS   2048
#define VMALLOC_END     (VMALLOC_BASE + VMALLOC_GRANULE * VMALLOC_SLOTS)

static uint64_t   g_used[VMALLOC_SLOTS / 64];
static size_t     g_cursor;
static bool       g_ready;
static spinlock_t g_lock = SPINLOCK_INIT;

static bool slot_used(size_t i) {
    return (g_used[i >> 6] >> (i & 63)) & 1;
}

static void slot_mark(size_t first, size_t n, bool used) {
    for (size_t i = first; i < first + n; i++) {
        if (used) g_used[i >> 6] |=  (1ULL << (i & 63));
        else      g_used[i >> 6] &= ~(1ULL << (i & 63));
    }
}

static long reserve_slots(size_t n) {
    size_t i = g_cursor, run = 0;
    for (size_t scanned = 0; scanned < VMALLOC_SLOTS + n; scanned++, i++) {
        if (i == VMALLOC_SLOTS) { i = 0; run = 0; }
        if (slot_used(i)) { run = 0; continue; }
        if (++run < n) continue;
        size_t first = i + 1 - n;
        slot_mark(first, n, true);
        g_cursor = (i + 1) % VMALLOC_SLOTS;
        return (long)first;
    }
    return -1;
}

static long lock_irq(void) {
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    spinlock_acquire(&g_lock);
    return (long)flags;
}

static void unlock_irq(long flags) {
    spinlock_release(&g_lock);
    asm volatile("push %0; popfq" :: "r"((uint64_t)flags) : "memory", "cc");
}

static void flush_everywhere(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3) :: "memory");
    if (smp_get_cpu_count() > 1) ipi_tlb_shootdown_broadcast(NULL, TLB_FLUSH_ALL);
}

static void unmap_range(uintptr_t base, size_t pages) {
    vmm_pagemap_t *kpm = vmm_get_kernel_pagemap();
    for (size_t i = 0; i < pages; i++) {
        uintptr_t va = base + i * PAGE_SIZE, phys;
        if (!vmm_virt_to_phys(kpm, va, &phys)) continue;
        vmm_unmap_page_noflush(kpm, va);
        pmm_free(pmm_phys_to_virt(phys & ~(PAGE_SIZE - 1)), 1);
    }
    flush_everywhere();
}

void vmalloc_init(void) {
    if (!vmm_prepare_kernel_range(VMALLOC_BASE, VMALLOC_END - VMALLOC_BASE)) {
        serial_printf("[VMALLOC] cannot prepare page tables\n");
        return;
    }
    g_ready = true;
    serial_printf("[VMALLOC] %llu MiB of kernel address space at 0x%llx\n",
                  (unsigned long long)((VMALLOC_END - VMALLOC_BASE) >> 20),
                  (unsigned long long)VMALLOC_BASE);
}

void *vmalloc_pages(size_t pages) {
    if (!g_ready || !pages) return NULL;
    size_t slots = (pages * PAGE_SIZE + VMALLOC_GRANULE - 1) / VMALLOC_GRANULE;
    if (slots > VMALLOC_SLOTS) return NULL;

    long fl = lock_irq();
    long first = reserve_slots(slots);
    unlock_irq(fl);
    if (first < 0) return NULL;

    uintptr_t base = VMALLOC_BASE + (uintptr_t)first * VMALLOC_GRANULE;
    vmm_pagemap_t *kpm = vmm_get_kernel_pagemap();
    for (size_t i = 0; i < pages; i++) {
        void *pg = pmm_alloc_zero(1);
        if (pg && vmm_map_page(kpm, base + i * PAGE_SIZE, pmm_virt_to_phys(pg),
                               VMM_PRESENT | VMM_WRITE | VMM_NOEXEC))
            continue;
        if (pg) pmm_free(pg, 1);
        unmap_range(base, i);
        fl = lock_irq();
        slot_mark((size_t)first, slots, false);
        unlock_irq(fl);
        return NULL;
    }
    return (void *)base;
}

void vfree_pages(void *addr, size_t pages) {
    uintptr_t base = (uintptr_t)addr & ~(PAGE_SIZE - 1);
    if (!vmalloc_owns(addr) || !pages) return;
    size_t first = (base - VMALLOC_BASE) / VMALLOC_GRANULE;
    size_t slots = (pages * PAGE_SIZE + VMALLOC_GRANULE - 1) / VMALLOC_GRANULE;
    unmap_range(base, pages);
    long fl = lock_irq();
    slot_mark(first, slots, false);
    unlock_irq(fl);
}

bool vmalloc_owns(const void *addr) {
    uintptr_t a = (uintptr_t)addr;
    return a >= VMALLOC_BASE && a < VMALLOC_END;
}

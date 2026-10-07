#include "../../include/memory/tlb.h"
#include "../../include/interrupts/interrupts.h"
#include "../../include/apic/apic.h"
#include "../../include/smp/smp.h"
#include "../../include/smp/percpu.h"
#include "../../include/sched/spinlock.h"
#include "../../include/io/serial.h"

static spinlock_t        g_tlb_lock = SPINLOCK_INIT;
static volatile uint64_t g_req_gen;
static volatile uint64_t g_req_cr3;
static volatile size_t   g_req_count;
static uintptr_t         g_req_addrs[TLB_SYNC_MAX_ADDRS];
static volatile int32_t  g_req_acks;
static volatile uint64_t g_seen_gen[MAX_CPUS];

static inline uint64_t read_cr3(void) {
    uint64_t v;
    asm volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void flush_here(uint64_t cr3, const uintptr_t *addrs, size_t count) {
    uint64_t cur = read_cr3() & ~0xFFFULL;
    if (cr3 && cur != (cr3 & ~0xFFFULL)) return;
    if (count == 0 || count > TLB_SYNC_MAX_ADDRS) {
        asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
        return;
    }
    for (size_t i = 0; i < count; i++)
        asm volatile("invlpg (%0)" :: "r"(addrs[i]) : "memory");
}

static void serve_pending(void) {
    uint32_t cpu = smp_cpu_index();
    if (cpu >= MAX_CPUS) return;
    uint64_t gen = __atomic_load_n(&g_req_gen, __ATOMIC_ACQUIRE);
    if (g_seen_gen[cpu] == gen) return;
    g_seen_gen[cpu] = gen;
    flush_here(g_req_cr3, g_req_addrs, g_req_count);
    __atomic_fetch_sub(&g_req_acks, 1, __ATOMIC_ACQ_REL);
}

void tlb_poll(void) {
    if (__atomic_load_n(&g_req_acks, __ATOMIC_ACQUIRE) > 0) serve_pending();
}

void tlb_flush_mm(uint64_t cr3, const uintptr_t *addrs, size_t count) {
    flush_here(cr3, addrs, count);
    uint32_t online = smp_get_online_count();
    if (online <= 1) return;

    uint64_t rf;
    asm volatile("pushfq; pop %0; cli" : "=r"(rf) :: "memory");
    while (!spinlock_try_acquire(&g_tlb_lock)) {
        if (__atomic_load_n(&g_req_acks, __ATOMIC_ACQUIRE) > 0) serve_pending();
        asm volatile("pause");
    }

    uint32_t self = smp_cpu_index();
    g_req_cr3 = cr3;
    g_req_count = (count > TLB_SYNC_MAX_ADDRS) ? 0 : count;
    for (size_t i = 0; i < g_req_count; i++) g_req_addrs[i] = addrs[i];
    __atomic_store_n(&g_req_acks, (int32_t)(online - 1), __ATOMIC_RELEASE);
    uint64_t gen = __atomic_add_fetch(&g_req_gen, 1, __ATOMIC_ACQ_REL);
    if (self < MAX_CPUS) g_seen_gen[self] = gen;

    lapic_write(0x310, 0);
    lapic_write(0x300, IPI_TLB_SYNC | (1 << 14) | (3 << 18));
    while (lapic_read(0x300) & (1 << 12)) asm volatile("pause");

    uint64_t spins = 0;
    while (__atomic_load_n(&g_req_acks, __ATOMIC_ACQUIRE) > 0) {
        asm volatile("pause");
        if (++spins == 400000000ULL)
            serial_printf("[tlb] still waiting for %d CPU(s) to flush\n", (int)g_req_acks);
    }
    spinlock_release(&g_tlb_lock);
    asm volatile("push %0; popfq" :: "r"(rf) : "memory", "cc");
}

DEFINE_IRQ(IPI_TLB_SYNC, ipi_tlb_sync_handler)
{
    (void)frame;
    serve_pending();
    lapic_eoi();
}

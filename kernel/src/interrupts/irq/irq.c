#include "../../../include/interrupts/interrupts.h"
#include "../../../include/io/serial.h"
#include "../../../include/interrupts/irq.h"
#include "../../../include/interrupts/idt.h"
#include "../../../include/io/ports.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/apic/apic.h"
#include <string.h>

extern const int_desc_t __start_irq_handlers[];
extern const int_desc_t __stop_irq_handlers[];
static int_handler_f registered_irq_interrupts[IRQ_INTERRUPTS_COUNT]__attribute__((aligned(64)));

typedef struct {
    irq_handler_f handler;
    void         *ctx;
    const char   *name;
    uint64_t      count;
    bool          in_use;
} runtime_irq_t;

static runtime_irq_t g_runtime_irqs[256];
static uint8_t       g_vector_bitmap[256 / 8];
static bool          g_irq_runtime_inited = false;

static inline void vec_mark_used(int v) { g_vector_bitmap[v >> 3] |=  (uint8_t)(1u << (v & 7)); }
static inline void vec_mark_free(int v) { g_vector_bitmap[v >> 3] &= (uint8_t)~(1u << (v & 7)); }
static inline bool vec_is_used(int v)   { return (g_vector_bitmap[v >> 3] & (1u << (v & 7))) != 0; }

void irq_runtime_init(void) {
    if (g_irq_runtime_inited) return;
    memset(g_runtime_irqs, 0, sizeof(g_runtime_irqs));
    memset(g_vector_bitmap, 0, sizeof(g_vector_bitmap));

    for (int v = 0; v < IRQ_VECTOR_MIN; v++)   vec_mark_used(v);
    for (int v = IRQ_VECTOR_MAX + 1; v < 256; v++) vec_mark_used(v);

    g_irq_runtime_inited = true;
    serial_printf("[irq] runtime IRQ allocator ready, range 0x%02x..0x%02x\n",
                  IRQ_VECTOR_MIN, IRQ_VECTOR_MAX);
}

int irq_alloc_vector(void) {
    if (!g_irq_runtime_inited) irq_runtime_init();
    for (int v = IRQ_VECTOR_MIN; v <= IRQ_VECTOR_MAX; v++) {
        if (!vec_is_used(v)) {
            vec_mark_used(v);
            return v;
        }
    }
    return -1;
}

int irq_alloc_vectors_contig(int count) {
    if (!g_irq_runtime_inited) irq_runtime_init();
    if (count <= 0) return -1;
    for (int base = IRQ_VECTOR_MIN; base + count - 1 <= IRQ_VECTOR_MAX; base++) {
        bool ok = true;
        for (int k = 0; k < count; k++) {
            if (vec_is_used(base + k)) { ok = false; break; }
        }
        if (ok) {
            for (int k = 0; k < count; k++) vec_mark_used(base + k);
            return base;
        }
    }
    return -1;
}

void irq_free_vector(int vector) {
    if (vector < IRQ_VECTOR_MIN || vector > IRQ_VECTOR_MAX) return;
    vec_mark_free(vector);
}

int irq_request(int vector, irq_handler_f handler, void *ctx, const char *name) {
    if (vector < 0 || vector > 255 || !handler) return -1;
    if (g_runtime_irqs[vector].in_use) {
        serial_printf("[irq] vector 0x%02x already has runtime handler '%s'\n",
                      vector, g_runtime_irqs[vector].name);
        return -1;
    }
    g_runtime_irqs[vector].handler = handler;
    g_runtime_irqs[vector].ctx     = ctx;
    g_runtime_irqs[vector].name    = name ? name : "?";
    g_runtime_irqs[vector].count   = 0;
    g_runtime_irqs[vector].in_use  = true;
    serial_printf("[irq] vector 0x%02x -> '%s'\n", vector, g_runtime_irqs[vector].name);
    return 0;
}

void irq_free(int vector) {
    if (vector < 0 || vector > 255) return;
    g_runtime_irqs[vector].handler = NULL;
    g_runtime_irqs[vector].ctx     = NULL;
    g_runtime_irqs[vector].name    = NULL;
    g_runtime_irqs[vector].in_use  = false;
}

uint64_t irq_count_for(int vector) {
    if (vector < 0 || vector > 255) return 0;
    return g_runtime_irqs[vector].count;
}

void irq_common_handler(struct int_frame_t* regs) {
    uint64_t vec = regs->interrupt;

    if (vec > 255) {
        serial_printf("IRQ vector out of range: %llu\n", vec);
        while (1) asm volatile ("hlt");
    }

    runtime_irq_t *r = &g_runtime_irqs[vec];
    if (r->in_use && r->handler) {
        r->count++;
        r->handler(r->ctx);
        lapic_eoi();
        return;
    }

    if (vec < IRQ_INTERRUPTS_COUNT && registered_irq_interrupts[vec]) {
        return registered_irq_interrupts[vec](regs);
    }

    serial_printf("Unhandled IRQ vector 0x%02llx\n", vec);
    lapic_eoi();
}

void setup_defined_irq_handlers(void) {
    irq_runtime_init();

    const int_desc_t* desc;
    for (desc = __start_irq_handlers; desc < __stop_irq_handlers; desc++) {
        if(desc->vector >= IRQ_INTERRUPTS_COUNT) {
            serial_printf("Invalid IRQ vector number! Must be < %d\n", IRQ_INTERRUPTS_COUNT);
            continue;
        }
        registered_irq_interrupts[desc->vector] = desc->handler;
        vec_mark_used((int)desc->vector);
        serial_printf("Registered IRQ vector 0x%d\n", desc->vector);
    }
}

extern percpu_t* percpu_regions[MAX_CPUS];

DEFINE_IRQ(IPI_RESCHEDULE_VECTOR, ipi_reschedule_handler)
{
    (void)frame;

    uint32_t id = lapic_get_id();
    if (id < MAX_CPUS && percpu_regions[id] != NULL) {
        percpu_regions[id]->need_resched = true;
    }

    lapic_eoi();
}

DEFINE_IRQ(IPI_TLB_SHOOTDOWN, ipi_tlb_shootdown_handler)
{
    (void)frame;

    uint32_t id = lapic_get_id();
    tlb_shootdown_t* q = &tlb_shootdown_queue[id];

    if (q->pending) {
        for (size_t i = 0; i < q->count; i++) {
            uintptr_t addr = q->addresses[i];
            if (addr != 0) {
                asm volatile ("invlpg (%0)" :: "r"(addr) : "memory");
            }
        }

        q->pending = false;
        q->count = 0;
    }

    lapic_eoi();
}

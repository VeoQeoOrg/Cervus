#include "../../../include/interrupts/interrupts.h"
#include "../../../include/io/serial.h"
#include "../../../include/interrupts/irq.h"
#include "../../../include/interrupts/idt.h"
#include "../../../include/io/ports.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/apic/apic.h"

extern const int_desc_t __start_irq_handlers[];
extern const int_desc_t __stop_irq_handlers[];
static int_handler_f registered_irq_interrupts[IRQ_INTERRUPTS_COUNT]__attribute__((aligned(64)));

void irq_common_handler(struct int_frame_t* regs) {
    uint64_t vec = regs->interrupt;

    if (vec >= IRQ_INTERRUPTS_COUNT || vec < (IDT_MAX_DESCRIPTORS - IRQ_INTERRUPTS_COUNT)) {
        serial_printf(COM1, "IRQ vector out of range: %d\n", vec);
        while (1)
        {
            asm volatile ("hlt");
        }
    }

    if(registered_irq_interrupts[vec]) {
        return registered_irq_interrupts[vec](regs);
    }

    serial_printf(COM1, "IRQ interrupt handler\n");

    while (1)
    {
        asm volatile ("hlt");
    }
}

void setup_defined_irq_handlers(void) {
    const int_desc_t* desc;
    for (desc = __start_irq_handlers; desc < __stop_irq_handlers; desc++) {
        if(desc->vector >= IRQ_INTERRUPTS_COUNT) {
            serial_printf(COM1, "Invalid IRQ vector number! Must be < %d\n", IRQ_INTERRUPTS_COUNT);
            continue;
        }
        registered_irq_interrupts[desc->vector] = desc->handler;
        serial_printf(COM1, "Registered IRQ vector 0x%d\n", desc->vector);
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
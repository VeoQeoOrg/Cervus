#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include <stdbool.h>
#include "interrupts.h"

#define IRQ_INTERRUPTS_COUNT 224

#define IRQ_VECTOR_MIN  0x50
#define IRQ_VECTOR_MAX  0xDF

static const char* irq_default_names[] __attribute__((unused)) = {
    "IRQ0 timer",
    "IRQ1 keyboard",
    "IRQ2 cascade",
    "IRQ3 COM2",
    "IRQ4 COM1",
    "IRQ5 LPT2",
    "IRQ6 floppy",
    "IRQ7 LPT1",
    "IRQ8 RTC",
    "IRQ9 ACPI",
    "IRQ10 reserved",
    "IRQ11 reserved",
    "IRQ12 mouse",
    "IRQ13 FPU",
    "IRQ14 ATA1",
    "IRQ15 ATA2"
};

typedef void (*irq_handler_f)(void *ctx);

void irq_common_handler(struct int_frame_t* regs);
void setup_defined_irq_handlers(void);

void irq_runtime_init(void);

int  irq_request(int vector, irq_handler_f handler, void *ctx, const char *name);
void irq_free(int vector);

int  irq_alloc_vector(void);
int  irq_alloc_vectors_contig(int count);
void irq_free_vector(int vector);

uint64_t irq_count_for(int vector);

#endif

#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "interrupts.h"

#define IRQ_INTERRUPTS_COUNT 224

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

void irq_common_handler(struct int_frame_t* regs);
void setup_defined_irq_handlers(void);

#endif

#include "../../include/interrupts/interrupts.h"
#include "../../include/io/serial.h"
#include "../../include/interrupts/isr.h"
#include "../../include/interrupts/irq.h"
#include "../../include/interrupts/idt.h"
#include "../../include/gdt/gdt.h"
#include "../../include/io/ports.h"

extern const int_desc_t __start_int_handlers[];
extern const int_desc_t __stop_int_handlers[];

void init_interrupt_system(void) {
    asm volatile("cli");

    setup_interrupt_descriptor_table(GDT_CODE_SEGMENT);

    setup_defined_isr_handlers();

    setup_defined_irq_handlers();

    asm volatile("sti");
}

void base_trap(void *ctx) {
    struct int_frame_t *regs = (struct int_frame_t*)ctx;

    if(regs->interrupt < ISR_EXCEPTION_COUNT) {
        return isr_common_handler(regs);
    } 

    return irq_common_handler(regs);
}
#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "idt.h"

void irq_init(void);
void irq_unmask(uint8_t irq);
void irq_mask(uint8_t irq);
void irq_install_handler(uint8_t irq, interrupt_handler_t handler);
void irq_uninstall_handler(uint8_t irq);
void irq_send_eoi(uint8_t irq);
void irq_default_handler(struct interrupt_frame* frame);

#endif // IRQ_H
#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "idt.h"

// Инициализация IRQ
void irq_init(void);

// Размаскирование IRQ (для будущего APIC)
void irq_unmask(uint8_t irq);

// Маскирование IRQ (для будущего APIC)
void irq_mask(uint8_t irq);

// Установка обработчика IRQ
void irq_install_handler(uint8_t irq, interrupt_handler_t handler);

// Удаление обработчика IRQ
void irq_uninstall_handler(uint8_t irq);

// Отправить EOI (End Of Interrupt) - для APIC будет заменено
void irq_send_eoi(uint8_t irq);

// Обработчики по умолчанию
void irq_default_handler(struct interrupt_frame* frame);

// Специализированные обработчики
void irq_timer_handler(struct interrupt_frame* frame);
void irq_keyboard_handler(struct interrupt_frame* frame);

#endif // IRQ_H
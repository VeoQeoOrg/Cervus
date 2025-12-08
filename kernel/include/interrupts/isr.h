#ifndef ISR_H
#define ISR_H

#include <stdint.h>
#include "idt.h"

// Максимальное количество обработчиков
#define MAX_INTERRUPT_HANDLERS 256

// Инициализация ISR
void isr_init(void);

// Установка обработчика для конкретного прерывания
void isr_install_handler(int interrupt, interrupt_handler_t handler);

// Удаление обработчика
void isr_uninstall_handler(int interrupt);

// Общие обработчики
void isr_exception_handler(struct interrupt_frame* frame);
void isr_irq_handler(struct interrupt_frame* frame);

// Функции для отладки
void print_exception_info(const char* name, struct interrupt_frame* frame);
void dump_registers(struct interrupt_frame* frame);

#endif // ISR_H
#include "../../include/interrupts/idt.h"
#include "../../include/interrupts/isr.h"
#include "../../include/interrupts/irq.h"
#include "../../include/io/serial.h"
#include <stddef.h>
#include <string.h>

static struct idt_entry idt[256];
static struct idt_ptr idt_ptr;

static interrupt_handler_t interrupt_handlers[256] = {0};

void idt_init(void) {
    serial_writestring(COM1, "[IDT] Initializing IDT...\n");
    
    // Очищаем IDT
    memset(idt, 0, sizeof(idt));
    
    // Устанавливаем обработчики исключений (0-31)
    idt_set_entry(0, (uint64_t)isr0, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(1, (uint64_t)isr1, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(2, (uint64_t)isr2, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(3, (uint64_t)isr3, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 3), 0);
    idt_set_entry(4, (uint64_t)isr4, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(5, (uint64_t)isr5, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(6, (uint64_t)isr6, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(7, (uint64_t)isr7, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(8, (uint64_t)isr8, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(9, (uint64_t)isr9, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(10, (uint64_t)isr10, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(11, (uint64_t)isr11, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(12, (uint64_t)isr12, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(13, (uint64_t)isr13, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(14, (uint64_t)isr14, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(15, (uint64_t)isr15, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(16, (uint64_t)isr16, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(17, (uint64_t)isr17, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(18, (uint64_t)isr18, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(19, (uint64_t)isr19, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(20, (uint64_t)isr20, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(21, (uint64_t)isr21, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(22, (uint64_t)isr22, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(23, (uint64_t)isr23, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(24, (uint64_t)isr24, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(25, (uint64_t)isr25, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(26, (uint64_t)isr26, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(27, (uint64_t)isr27, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(28, (uint64_t)isr28, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(29, (uint64_t)isr29, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(30, (uint64_t)isr30, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(31, (uint64_t)isr31, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    
    // Устанавливаем обработчики IRQ (32-47)
    idt_set_entry(32, (uint64_t)irq0, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(33, (uint64_t)irq1, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(34, (uint64_t)irq2, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(35, (uint64_t)irq3, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(36, (uint64_t)irq4, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(37, (uint64_t)irq5, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(38, (uint64_t)irq6, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(39, (uint64_t)irq7, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(40, (uint64_t)irq8, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(41, (uint64_t)irq9, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(42, (uint64_t)irq10, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(43, (uint64_t)irq11, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(44, (uint64_t)irq12, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(45, (uint64_t)irq13, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(46, (uint64_t)irq14, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    idt_set_entry(47, (uint64_t)irq15, 0x08, IDT_ATTR(IDT_TYPE_INTERRUPT, 0), 0);
    
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;
    
    idt_load();
    
    isr_init();
    irq_init();
    
    serial_writestring(COM1, "[IDT] IDT initialized successfully\n");
}

void idt_set_entry(uint8_t index, uint64_t offset, uint16_t selector, uint8_t type_attr, uint8_t ist) {
    idt[index].offset_low = offset & 0xFFFF;
    idt[index].offset_mid = (offset >> 16) & 0xFFFF;
    idt[index].offset_high = (offset >> 32) & 0xFFFFFFFF;
    
    idt[index].selector = selector;
    idt[index].ist = ist & 0x07;
    idt[index].type_attr = type_attr;
    idt[index].zero = 0;
}

void register_interrupt_handler(uint16_t interrupt, interrupt_handler_t handler) {
    if (interrupt < 256) {
        interrupt_handlers[interrupt] = handler;
    }
}

interrupt_handler_t get_interrupt_handler(uint16_t interrupt) {
    if (interrupt < 256) {
        return interrupt_handlers[interrupt];
    }
    return NULL;
}

void exception_handler(struct interrupt_frame* frame) {
    interrupt_handler_t handler = get_interrupt_handler(frame->interrupt_number);
    
    if (handler != NULL) {
        handler(frame);
    } else {
        serial_printf(COM1, "[EXCEPTION] Unhandled exception %d at RIP: 0x%llx, Error code: 0x%llx\n",
                     frame->interrupt_number, frame->rip, frame->error_code);
        
        if (frame->interrupt_number == EXCEPTION_DOUBLE_FAULT ||
            frame->interrupt_number == EXCEPTION_GENERAL_PROTECTION_FAULT ||
            frame->interrupt_number == EXCEPTION_PAGE_FAULT) {
            serial_writestring(COM1, "[EXCEPTION] Critical exception! Halting system.\n");
            for (;;) {
                asm volatile ("hlt");
            }
        }
    }
}

void irq_handler(struct interrupt_frame* frame) {
    interrupt_handler_t handler = get_interrupt_handler(frame->interrupt_number);
    
    if (handler != NULL) {
        handler(frame);
    } else {
        serial_printf(COM1, "[IRQ] Unhandled IRQ %d\n", frame->interrupt_number - IRQ_BASE);
    }
    
    irq_send_eoi(frame->interrupt_number - IRQ_BASE);
}
#include "../../include/interrupts/irq.h"
#include "../../include/interrupts/idt.h"
#include "../../include/io/serial.h"
#include "../../include/io/ports.h"

void irq_init(void) {
    serial_writestring(COM1, "[IRQ] Initializing IRQ subsystem...\n");
    
    // Устанавливаем обработчики по умолчанию
    for (int i = 0; i < 16; i++) {
        irq_install_handler(i, irq_default_handler);
    }
    
    //irq_install_handler(0, timer_handler);
    
    serial_writestring(COM1, "[IRQ] IRQ subsystem initialized\n");
}

void irq_unmask(uint8_t irq) {
    // В будущем будет реализовано через APIC
    serial_printf(COM1, "[IRQ] Unmasking IRQ %d (APIC stub)\n", irq);
}

// Маскирование IRQ (заглушка для APIC)
void irq_mask(uint8_t irq) {
    // В будущем будет реализовано через APIC
    serial_printf(COM1, "[IRQ] Masking IRQ %d (APIC stub)\n", irq);
}

void irq_install_handler(uint8_t irq, interrupt_handler_t handler) {
    if (irq < 16) {
        uint8_t interrupt_number = IRQ_BASE + irq;
        register_interrupt_handler(interrupt_number, handler);
        serial_printf(COM1, "[IRQ] Handler installed for IRQ %d (INT 0x%02x)\n", irq, interrupt_number);
    }
}

void irq_uninstall_handler(uint8_t irq) {
    if (irq < 16) {
        uint8_t interrupt_number = IRQ_BASE + irq;
        register_interrupt_handler(interrupt_number, NULL);
        serial_printf(COM1, "[IRQ] Handler uninstalled for IRQ %d\n", irq);
    }
}

// Отправка EOI (заглушка для APIC)
void irq_send_eoi(uint8_t irq) {
    // В будущем будет заменено на APIC EOI
    serial_printf(COM1, "[IRQ] EOI sent for IRQ %d (APIC stub)\n", irq);
}

void irq_default_handler(struct interrupt_frame* frame) {
    uint8_t irq_num = frame->interrupt_number - IRQ_BASE;
    serial_printf(COM1, "[IRQ] Default handler for IRQ %d\n", irq_num);
}

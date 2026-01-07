#include "../../include/interrupts/idt.h"
#include "../../include/interrupts/isr.h"
#include "../../include/interrupts/irq.h"
#include "../../include/memory/paging.h"
#include "../../include/io/serial.h"
#include <stddef.h>
#include <string.h>

#define IDT_MAX_DESCRIPTORS 256
#define GDT_CODE_SEGMENT 0x08

extern void *isr_stub_table[];

static struct idt_entry idt[IDT_MAX_DESCRIPTORS];
static struct idt_ptr idt_ptr;

static interrupt_handler_t interrupt_handlers[IDT_MAX_DESCRIPTORS] = {0};

void idt_set_entry(uint8_t index, void *base, uint16_t selector, uint8_t flags, uint8_t ist) {
    idt[index].base_low   = (uint64_t)base & 0xFFFF;
    idt[index].kernel_cs  = selector;
    idt[index].ist        = ist;
    idt[index].attributes = flags;
    idt[index].base_mid   = ((uint64_t)base >> 16) & 0xFFFF;
    idt[index].base_high  = ((uint64_t)base >> 32) & 0xFFFFFFFF;
    idt[index].reserved   = 0;
}

void idt_init(void) {
    serial_writestring(COM1, "[IDT] Initializing IDT with IST...\n");
    
    memset(idt, 0, sizeof(idt));
    
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;
    
    for (uint16_t vector = 0; vector < IDT_MAX_DESCRIPTORS; vector++) {
        uint8_t ist = 0;

        switch (vector) {
            case EXCEPTION_DOUBLE_FAULT:
                ist = 1;
                break;
            case EXCEPTION_PAGE_FAULT:
                ist = 2;
                break;
        }

        idt_set_entry(vector, isr_stub_table[vector],
                    GDT_CODE_SEGMENT, 0x8E, ist);
    }

    __asm__ volatile("lidt %0" : : "m"(idt_ptr)); 
    
    isr_init();
    register_interrupt_handler(14, page_fault_handler);
    
    serial_writestring(COM1, "[IDT] IDT initialized with IST support\n");
}

void register_interrupt_handler(uint16_t interrupt, interrupt_handler_t handler) {
    if (interrupt < IDT_MAX_DESCRIPTORS) {
        interrupt_handlers[interrupt] = handler;
        serial_printf(COM1, "[IDT] Registered handler for interrupt %d at %p\n", 
                     interrupt, handler);
    }
}

interrupt_handler_t get_interrupt_handler(uint16_t interrupt) {
    if (interrupt < IDT_MAX_DESCRIPTORS) {
        return interrupt_handlers[interrupt];
    }
    return NULL;
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
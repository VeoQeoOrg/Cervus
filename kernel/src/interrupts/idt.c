#include "../../include/interrupts/idt.h"
#include "../../include/interrupts/isr.h"
#include "../../include/interrupts/irq.h"
#include "../../include/io/serial.h"
#include <stddef.h>
#include <string.h>

extern void *interrupts_stub_table[];

__attribute__((
    aligned(0x10))) static idt_entry_t idt_entries[IDT_MAX_DESCRIPTORS];

idtr_t idtr;

void idt_set_gate(uint8_t index, void *base, uint16_t selector, uint8_t flags, uint8_t ist) {
    idt_entries[index].base_low   = (uint64_t)base & 0xFFFF;
    idt_entries[index].kernel_cs  = selector;
    idt_entries[index].ist        = ist;
    idt_entries[index].attributes = flags;
    idt_entries[index].base_mid   = ((uint64_t)base >> 16) & 0xFFFF;
    idt_entries[index].base_high  = ((uint64_t)base >> 32) & 0xFFFFFFFF;
    idt_entries[index].reserved   = 0;
}

void idt_gate_enable(int interrupt) {
    FLAG_SET(idt_entries[interrupt].attributes, IDT_FLAG_PRESENT);
}

void idt_gate_disable(int interrupt) {
    FLAG_UNSET(idt_entries[interrupt].attributes, IDT_FLAG_PRESENT);
}

bool setup_specific_vectors(uint64_t kernel_code_segment, uint64_t vector) {
    if(vector == EXCEPTION_DOUBLE_FAULT) {
        idt_set_gate(vector, interrupts_stub_table[vector], kernel_code_segment, 0x8E, 1);
        return true;
    }

    if(vector == EXCEPTION_NMI) {
        idt_set_gate(vector, interrupts_stub_table[vector], kernel_code_segment, 0x8E, 2);
        return true;
    }

    if(vector == EXCEPTION_PAGE_FAULT) {
        idt_set_gate(vector, interrupts_stub_table[vector], kernel_code_segment, 0x8E, 3);
        return true;
    }

    return false;
}

void idt_load(void) {
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

void setup_interrupt_descriptor_table(uint64_t kernel_code_segment) {
    serial_printf(COM1, "[IDT] Initializing IDT...\n");

    idtr.base  = (idt_entry_t *)&idt_entries[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * IDT_MAX_DESCRIPTORS - 1;

    for (uint16_t vector = 0; vector < IDT_MAX_DESCRIPTORS; vector++) {

        if(setup_specific_vectors(kernel_code_segment, vector)) {
            continue;
        }

        idt_set_gate(vector, interrupts_stub_table[vector], kernel_code_segment, 0x8E, 0);
    }

    idt_load();

    serial_printf(COM1, "[IDT] IDT initialized successfully\n");
}

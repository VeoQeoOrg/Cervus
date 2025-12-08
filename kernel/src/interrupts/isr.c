#include "../../include/interrupts/isr.h"
#include "../../include/interrupts/idt.h"
#include "../../include/io/serial.h"
#include <string.h>

void isr_init(void) {
    serial_writestring(COM1, "[ISR] Initializing ISR...\n");
    
    for (int i = 0; i <= 31; i++) {
        isr_install_handler(i, isr_exception_handler);
    }
    
    for (int i = IRQ_BASE; i < IRQ_BASE + 16; i++) {
        isr_install_handler(i, isr_irq_handler);
    }
    
    serial_writestring(COM1, "[ISR] ISR initialized successfully\n");
}

void isr_install_handler(int interrupt, interrupt_handler_t handler) {
    if (interrupt >= 0 && interrupt < MAX_INTERRUPT_HANDLERS) {
        register_interrupt_handler(interrupt, handler);
    }
}

void isr_uninstall_handler(int interrupt) {
    if (interrupt >= 0 && interrupt < MAX_INTERRUPT_HANDLERS) {
        register_interrupt_handler(interrupt, NULL);
    }
}

void isr_exception_handler(struct interrupt_frame* frame) {
    const char* exception_names[] = {
        "Divide Error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "Bound Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Segment Fault",
        "General Protection Fault",
        "Page Fault",
        "Reserved",
        "x87 Floating-Point Exception",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating-Point Exception",
        "Virtualization Exception",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Security Exception",
        "Reserved"
    };
    
    if (frame->interrupt_number <= 31) {
        print_exception_info(exception_names[frame->interrupt_number], frame);
    }
}

void isr_irq_handler(struct interrupt_frame* frame) {
    serial_printf(COM1, "[ISR] Unhandled IRQ: %d\n", frame->interrupt_number - IRQ_BASE);
}

void print_exception_info(const char* name, struct interrupt_frame* frame) {
    serial_printf(COM1, "\n[EXCEPTION] %s (0x%02x)\n", name, frame->interrupt_number);
    serial_printf(COM1, "  Error Code: 0x%016llx\n", frame->error_code);
    serial_printf(COM1, "  RIP: 0x%016llx\n", frame->rip);
    serial_printf(COM1, "  CS: 0x%04x\n", frame->cs);
    serial_printf(COM1, "  RFLAGS: 0x%016llx\n", frame->rflags);
    serial_printf(COM1, "  RSP: 0x%016llx\n", frame->rsp);
    serial_printf(COM1, "  SS: 0x%04x\n", frame->ss);
    
    dump_registers(frame);
}

void dump_registers(struct interrupt_frame* frame) {
    serial_writestring(COM1, "  Registers:\n");
    serial_printf(COM1, "    RAX: 0x%016llx  RBX: 0x%016llx\n", frame->rax, frame->rbx);
    serial_printf(COM1, "    RCX: 0x%016llx  RDX: 0x%016llx\n", frame->rcx, frame->rdx);
    serial_printf(COM1, "    RSI: 0x%016llx  RDI: 0x%016llx\n", frame->rsi, frame->rdi);
    serial_printf(COM1, "    RBP: 0x%016llx\n", frame->rbp);
    serial_printf(COM1, "    R8:  0x%016llx  R9:  0x%016llx\n", frame->r8, frame->r9);
    serial_printf(COM1, "    R10: 0x%016llx  R11: 0x%016llx\n", frame->r10, frame->r11);
    serial_printf(COM1, "    R12: 0x%016llx  R13: 0x%016llx\n", frame->r12, frame->r13);
    serial_printf(COM1, "    R14: 0x%016llx  R15: 0x%016llx\n", frame->r14, frame->r15);
}
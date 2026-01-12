#ifndef ISR_H
#define ISR_H

#include "interrupts.h"

#define ISR_EXCEPTION_COUNT 32

enum {
    EXCEPTION_DIVIDE_ERROR = 0,
    EXCEPTION_DEBUG,
    EXCEPTION_NMI,
    EXCEPTION_BREAKPOINT,
    EXCEPTION_OVERFLOW,
    EXCEPTION_BOUND_RANGE,
    EXCEPTION_INVALID_OPCODE,
    EXCEPTION_DEVICE_NOT_AVAILABLE,
    EXCEPTION_DOUBLE_FAULT,
    EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN,
    EXCEPTION_INVALID_TSS,
    EXCEPTION_SEGMENT_NOT_PRESENT,
    EXCEPTION_STACK_SEGMENT_FAULT,
    EXCEPTION_GENERAL_PROTECTION_FAULT,
    EXCEPTION_PAGE_FAULT,
    EXCEPTION_RESERVED15,
    EXCEPTION_X87_FPU_ERROR,
    EXCEPTION_ALIGNMENT_CHECK,
    EXCEPTION_MACHINE_CHECK,
    EXCEPTION_SIMD_FPU_EXCEPTION,
    EXCEPTION_VIRTUALIZATION_EXCEPTION,
    EXCEPTION_RESERVED21,
    EXCEPTION_RESERVED22,
    EXCEPTION_RESERVED23,
    EXCEPTION_RESERVED24,
    EXCEPTION_RESERVED25,
    EXCEPTION_RESERVED26,
    EXCEPTION_RESERVED27,
    EXCEPTION_RESERVED28,
    EXCEPTION_RESERVED29,
    EXCEPTION_SECURITY_EXCEPTION = 30,
    EXCEPTION_RESERVED31
};

static const char* exception_names[] __attribute__((unused)) = {
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

void isr_common_handler(struct int_frame_t* regs);
void setup_defined_isr_handlers(void);

#endif

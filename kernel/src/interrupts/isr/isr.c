#include "../../../include/interrupts/interrupts.h"
#include "../../../include/io/serial.h"
#include "../../../include/interrupts/isr.h"
#include <stdio.h>

extern const int_desc_t __start_isr_handlers[];
extern const int_desc_t __stop_isr_handlers[];
static int_handler_f registered_isr_interrupts[ISR_EXCEPTION_COUNT]__attribute__((aligned(64)));

void registers_dump(struct int_frame_t *regs) {
    serial_printf(COM1, "\nRegisters Dump:\n");
    serial_printf(COM1, "\tRAX:0x%x\n\tRBX:0x%x\n\tRCX:0x%x\n\tRDX:0x%x\n", regs->rax, regs->rbx, regs->rcx, regs->rdx);
    serial_printf(COM1, "\tRSI:0x%x\n\tRDI:0x%x\n\tRBP:0x%x\n\tRSP:0x%x\n", regs->rsi, regs->rdi, regs->rbp, regs->rsp);
    serial_printf(COM1, "\tRIP:0x%x\n\tRFL:0x%x\n\tCS:0x%x\n\tERR:0x%x\n", regs->rip, regs->rflags, regs->cs, regs->error);
    serial_printf(COM1, "\nInt:%d (%s)\n", regs->interrupt, exception_names[regs->interrupt]);

    printf("\nRegisters Dump:\n");
    printf("\tRAX:0x%x\n\tRBX:0x%x\n\tRCX:0x%x\n\tRDX:0x%x\n", regs->rax, regs->rbx, regs->rcx, regs->rdx);
    printf("\tRSI:0x%x\n\tRDI:0x%x\n\tRBP:0x%x\n\tRSP:0x%x\n", regs->rsi, regs->rdi, regs->rbp, regs->rsp);
    printf("\tRIP:0x%x\n\tRFL:0x%x\n\tCS:0x%x\n\tERR:0x%x\n", regs->rip, regs->rflags, regs->cs, regs->error);
    printf("\nInt:%d (%s)\n", regs->interrupt, exception_names[regs->interrupt]);
}

void handle_intercpu_interrupt(struct int_frame_t* regs) {
    registers_dump(regs);

    switch(regs->interrupt) {
        case EXCEPTION_DIVIDE_ERROR: 
        case EXCEPTION_OVERFLOW: 
        case EXCEPTION_BOUND_RANGE:
        case EXCEPTION_INVALID_OPCODE:
        case EXCEPTION_DEVICE_NOT_AVAILABLE: 
        case EXCEPTION_X87_FPU_ERROR: 
            return;
            
        case EXCEPTION_DOUBLE_FAULT:
        case EXCEPTION_INVALID_TSS:
        case EXCEPTION_SEGMENT_NOT_PRESENT:
        case EXCEPTION_STACK_SEGMENT_FAULT:
        case EXCEPTION_GENERAL_PROTECTION_FAULT:
        case EXCEPTION_PAGE_FAULT:
        case EXCEPTION_MACHINE_CHECK: 
            serial_printf(COM1, "CRITICAL: System halted\n");
            printf("CRITICAL: System halted\n");
            while (1) asm volatile ("hlt");
            
        default:
            serial_printf(COM1, "UNKNOWN EXCEPTION: System halted\n");
            printf("UNKNOWN EXCEPTION: System halted\n");
            while (1) asm volatile ("hlt");
    }
}

DEFINE_ISR(0x3, isr_breakpoint) {
    (void)frame;
    serial_printf(COM1, "Breakpoint hit\n");
    printf("Breakpoint hit\n");
}

void isr_common_handler(struct int_frame_t* regs) {
    uint64_t vec = regs->interrupt;
    
    if (vec >= ISR_EXCEPTION_COUNT) {
        serial_printf(COM1, "Invalid ISR vector %d\n", vec);
        while (1) asm volatile ("hlt");
    }

    if(registered_isr_interrupts[vec]) {
        registered_isr_interrupts[vec](regs);
        return;
    }

    serial_printf(COM1, "ISR %d (%s) - No custom handler\n", vec, exception_names[vec]);
    printf("ISR %d (%s) - No custom handler\n", vec, exception_names[vec]);
    handle_intercpu_interrupt(regs);
}

void setup_defined_isr_handlers(void) {
    const int_desc_t* desc;
    for (desc = __start_isr_handlers; desc < __stop_isr_handlers; desc++) {
        if(desc->vector >= ISR_EXCEPTION_COUNT) {
            serial_printf(COM1, "Invalid ISR vector %d\n", desc->vector);
            continue;
        }
        registered_isr_interrupts[desc->vector] = desc->handler;
        serial_printf(COM1, "ISR: Registered vector %d\n", desc->vector);
    }
}
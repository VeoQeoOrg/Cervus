#include "../../../include/interrupts/interrupts.h"
#include "../../../include/io/serial.h"
#include "../../../include/interrupts/isr.h"
#include "../../../include/sched/sched.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/apic/apic.h"
#include "../../../include/memory/vmm.h"
#include <stdio.h>

extern const int_desc_t __start_isr_handlers[];
extern const int_desc_t __stop_isr_handlers[];
static int_handler_f registered_isr_interrupts[ISR_EXCEPTION_COUNT] __attribute__((aligned(64)));

void registers_dump(struct int_frame_t *regs) {
    uint64_t cr2 = 0;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));

    serial_printf("\nRegisters Dump:\n");
    serial_printf("\tRAX:0x%llx\n\tRBX:0x%llx\n\tRCX:0x%llx\n\tRDX:0x%llx\n",
                  regs->rax, regs->rbx, regs->rcx, regs->rdx);
    serial_printf("\tRSI:0x%llx\n\tRDI:0x%llx\n\tRBP:0x%llx\n\tRSP:0x%llx\n",
                  regs->rsi, regs->rdi, regs->rbp, regs->rsp);
    serial_printf("\tRIP:0x%llx\n\tRFL:0x%llx\n\tCS:0x%llx\n\tERR:0x%llx\n",
                  regs->rip, regs->rflags, regs->cs, regs->error);
    serial_printf("\tCR2:0x%llx\n", cr2);
    serial_printf("\nInt:%d (%s)\n", regs->interrupt, exception_names[regs->interrupt]);

    printf("\nRegisters Dump:\n");
    printf("\tRAX:0x%llx\n\tRBX:0x%llx\n\tRCX:0x%llx\n\tRDX:0x%llx\n",
           regs->rax, regs->rbx, regs->rcx, regs->rdx);
    printf("\tRSI:0x%llx\n\tRDI:0x%llx\n\tRBP:0x%llx\n\tRSP:0x%llx\n",
           regs->rsi, regs->rdi, regs->rbp, regs->rsp);
    printf("\tRIP:0x%llx\n\tRFL:0x%llx\n\tCS:0x%llx\n\tERR:0x%llx\n",
           regs->rip, regs->rflags, regs->cs, regs->error);
    printf("\tCR2:0x%llx\n", cr2);
    printf("\nInt:%d (%s)\n", regs->interrupt, exception_names[regs->interrupt]);
}

static void try_kill_or_halt(struct int_frame_t *regs, const char *label)
{
    if ((regs->cs & 3) == 3) {
        serial_printf("[ISR] %s in userspace at RIP=0x%llx — killing task\n",
                      label, regs->rip);
        printf("[ISR] %s in userspace — task killed\n", label);

        percpu_t *pc = get_percpu();
        task_t   *me = pc ? (task_t *)pc->current_task : NULL;
        if (!me) {
            uint32_t cpu = lapic_get_id();
            me = current_task[cpu];
        }
        if (me) {
            me->exit_code = 139;
            task_wakeup_waiters(me->pid);
        }
        vmm_switch_pagemap(vmm_get_kernel_pagemap());
        task_exit();
    }

    serial_printf("CRITICAL: %s in kernel mode — system halted\n", label);
    printf("CRITICAL: %s in kernel mode — system halted\n", label);
    while (1) asm volatile("cli; hlt");
}

void handle_intercpu_interrupt(struct int_frame_t *regs)
{
    registers_dump(regs);

    switch (regs->interrupt) {
        case EXCEPTION_DIVIDE_ERROR:
        case EXCEPTION_OVERFLOW:
        case EXCEPTION_BOUND_RANGE:
        case EXCEPTION_INVALID_OPCODE:
        case EXCEPTION_DEVICE_NOT_AVAILABLE:
        case EXCEPTION_X87_FPU_ERROR:
            try_kill_or_halt(regs, exception_names[regs->interrupt]);
            return;

        case EXCEPTION_DOUBLE_FAULT:
            serial_printf("CRITICAL: Double Fault — system halted\n");
            printf("CRITICAL: Double Fault — system halted\n");
            while (1) asm volatile("cli; hlt");

        case EXCEPTION_INVALID_TSS:
        case EXCEPTION_SEGMENT_NOT_PRESENT:
        case EXCEPTION_STACK_SEGMENT_FAULT:
        case EXCEPTION_GENERAL_PROTECTION_FAULT:
        case EXCEPTION_PAGE_FAULT:
        case EXCEPTION_MACHINE_CHECK:
            try_kill_or_halt(regs, exception_names[regs->interrupt]);
            return;

        default:
            serial_printf("UNKNOWN EXCEPTION %d — system halted\n", regs->interrupt);
            printf("UNKNOWN EXCEPTION %d — system halted\n", regs->interrupt);
            while (1) asm volatile("cli; hlt");
    }
}

DEFINE_ISR(0x3, isr_breakpoint) {
    (void)frame;
    serial_printf("Breakpoint hit\n");
    printf("Breakpoint hit\n");
}

void isr_common_handler(struct int_frame_t *regs)
{
    uint64_t vec = regs->interrupt;

    if (vec >= ISR_EXCEPTION_COUNT) {
        serial_printf("Invalid ISR vector %d\n", vec);
        while (1) asm volatile("hlt");
    }

    if (registered_isr_interrupts[vec]) {
        registered_isr_interrupts[vec](regs);
        return;
    }

    serial_printf("ISR %d (%s) - No custom handler\n", vec, exception_names[vec]);
    printf("ISR %d (%s) - No custom handler\n", vec, exception_names[vec]);
    handle_intercpu_interrupt(regs);
}

void setup_defined_isr_handlers(void)
{
    const int_desc_t *desc;
    for (desc = __start_isr_handlers; desc < __stop_isr_handlers; desc++) {
        if (desc->vector >= ISR_EXCEPTION_COUNT) {
            serial_printf("ISR: vector %d out of range\n", desc->vector);
            continue;
        }
        registered_isr_interrupts[desc->vector] = desc->handler;
        serial_printf("ISR: Registered vector %d\n", desc->vector);
    }
}
#include "../../../include/interrupts/interrupts.h"
#include "../../../include/io/serial.h"
#include "../../../include/interrupts/isr.h"
#include "../../../include/sched/sched.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/apic/apic.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/panic/panic.h"
#include <stdio.h>

extern const int_desc_t __start_isr_handlers[];
extern const int_desc_t __stop_isr_handlers[];
static int_handler_f registered_isr_interrupts[ISR_EXCEPTION_COUNT] __attribute__((aligned(64)));

extern volatile int g_panic_owner;

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
}


void handle_intercpu_interrupt(struct int_frame_t *regs)
{
    if (regs->interrupt == 2) {
        if (__atomic_load_n(&g_panic_owner, __ATOMIC_ACQUIRE) != 0) {
            for (;;) asm volatile("cli; hlt");
        }
        kernel_panic_regs("Non-Maskable Interrupt (hardware)", regs);
    }

    serial_force_unlock();
    registers_dump(regs);

    switch (regs->interrupt) {
        case EXCEPTION_DIVIDE_ERROR:
        case EXCEPTION_OVERFLOW:
        case EXCEPTION_BOUND_RANGE:
        case EXCEPTION_INVALID_OPCODE:
        case EXCEPTION_DEVICE_NOT_AVAILABLE:
        case EXCEPTION_X87_FPU_ERROR:
            if ((regs->cs & 3) == 3) {
                if (regs->interrupt == EXCEPTION_INVALID_OPCODE) {
                    volatile uint8_t *ip = (volatile uint8_t *)regs->rip;
                    serial_printf("[ISR-UD] RIP=0x%llx bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                                  regs->rip,
                                  (unsigned)ip[0], (unsigned)ip[1],
                                  (unsigned)ip[2], (unsigned)ip[3],
                                  (unsigned)ip[4], (unsigned)ip[5],
                                  (unsigned)ip[6], (unsigned)ip[7]);
                    serial_printf("[ISR-UD] RBP=0x%llx RSP=0x%llx\n",
                                  regs->rbp, regs->rsp);
                    volatile uint64_t *sp = (volatile uint64_t *)regs->rsp;
                    serial_printf("[ISR-UD] Stack: [0]=%llx [1]=%llx [2]=%llx [3]=%llx\n",
                                  sp[0], sp[1], sp[2], sp[3]);
                    serial_printf("[ISR-UD] Stack: [4]=%llx [5]=%llx [6]=%llx [7]=%llx\n",
                                  sp[4], sp[5], sp[6], sp[7]);
                    {
                        uint64_t cr3_val = 0;
                        asm volatile("mov %%cr3, %0" : "=r"(cr3_val));
                        uintptr_t hhdm = (uintptr_t)pmm_phys_to_virt(0);
                        uint64_t vpage = regs->rip & ~0xFFFULL;
                        volatile uint64_t *pml4v = (volatile uint64_t*)(hhdm + (cr3_val & ~0xFFFULL));
                        uint64_t e4 = pml4v[(vpage >> 39) & 0x1FF];
                        if (e4 & 1) {
                            volatile uint64_t *pdpt = (volatile uint64_t*)(hhdm + (e4 & ~0xFFFULL));
                            uint64_t e3 = pdpt[(vpage >> 30) & 0x1FF];
                            if (e3 & 1) {
                                volatile uint64_t *pd = (volatile uint64_t*)(hhdm + (e3 & ~0xFFFULL));
                                uint64_t e2 = pd[(vpage >> 21) & 0x1FF];
                                if (e2 & 1) {
                                    volatile uint64_t *pt = (volatile uint64_t*)(hhdm + (e2 & ~0xFFFULL));
                                    uint64_t pte = pt[(vpage >> 12) & 0x1FF];
                                    serial_printf("[ISR-UD] PTE virt=0x%llx: 0x%llx (phys=0x%llx flags=0x%03llx)\n",
                                                  vpage, pte, pte & ~0xFFFULL, pte & 0xFFFULL);
                                    uintptr_t phys_page = pte & ~0xFFFULL;
                                    uintptr_t off = regs->rip & 0xFFF;
                                    volatile uint8_t *hhdm_bytes = (volatile uint8_t*)(hhdm + phys_page + off);
                                    serial_printf("[ISR-UD] HHDM bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                                                  (unsigned)hhdm_bytes[0], (unsigned)hhdm_bytes[1],
                                                  (unsigned)hhdm_bytes[2], (unsigned)hhdm_bytes[3],
                                                  (unsigned)hhdm_bytes[4], (unsigned)hhdm_bytes[5],
                                                  (unsigned)hhdm_bytes[6], (unsigned)hhdm_bytes[7]);
                                    volatile uint8_t *hhdm_page = (volatile uint8_t*)(hhdm + phys_page);
                                    serial_printf("[ISR-UD] HHDM page start: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                                                  (unsigned)hhdm_page[0], (unsigned)hhdm_page[1],
                                                  (unsigned)hhdm_page[2], (unsigned)hhdm_page[3],
                                                  (unsigned)hhdm_page[4], (unsigned)hhdm_page[5],
                                                  (unsigned)hhdm_page[6], (unsigned)hhdm_page[7]);
                                }
                            }
                        }
                    }
                }
                serial_printf("[ISR] %s in userspace at RIP=0x%llx — killing task\n",
                              exception_names[regs->interrupt], regs->rip);
                percpu_t *pc = get_percpu();
                task_t   *me = pc ? (task_t *)pc->current_task : NULL;
                if (!me) { uint32_t cpu = lapic_get_id(); me = current_task[cpu]; }
                if (me) me->exit_code = 139;
                vmm_switch_pagemap(vmm_get_kernel_pagemap());
                task_exit();
            }
            kernel_panic_regs(exception_names[regs->interrupt], regs);

        case EXCEPTION_DOUBLE_FAULT:
            kernel_panic_regs("Double Fault", regs);

        case EXCEPTION_INVALID_TSS:
        case EXCEPTION_SEGMENT_NOT_PRESENT:
        case EXCEPTION_STACK_SEGMENT_FAULT:
            kernel_panic_regs(exception_names[regs->interrupt], regs);

        case EXCEPTION_GENERAL_PROTECTION_FAULT:
            if ((regs->cs & 3) == 3) {
                serial_printf("[ISR] GPF in userspace at RIP=0x%llx — killing task\n",
                              regs->rip);
                percpu_t *pc = get_percpu();
                task_t   *me = pc ? (task_t *)pc->current_task : NULL;
                if (!me) { uint32_t cpu = lapic_get_id(); me = current_task[cpu]; }
                if (me) me->exit_code = 139;
                vmm_switch_pagemap(vmm_get_kernel_pagemap());
                task_exit();
            }
            kernel_panic_regs("General Protection Fault (kernel)", regs);

        case EXCEPTION_PAGE_FAULT: {
            uint64_t cr2val = 0;
            asm volatile("mov %%cr2, %0" : "=r"(cr2val));
            if ((regs->cs & 3) == 3) {
                percpu_t *pc = get_percpu();
                task_t   *me = pc ? (task_t *)pc->current_task : NULL;
                if (!me) { uint32_t cpu = lapic_get_id(); me = current_task[cpu]; }
                serial_printf("[ISR] Page Fault in userspace: RIP=0x%llx CR2=0x%llx ERR=0x%llx task='%s' pid=%u\n",
                              regs->rip, cr2val, regs->error,
                              me ? me->name : "?", me ? me->pid : 0);
                if (me) me->exit_code = 139;
                vmm_switch_pagemap(vmm_get_kernel_pagemap());
                task_exit();
            }
            kernel_panic_regs("Page Fault (kernel)", regs);
        }

        case EXCEPTION_MACHINE_CHECK:
            kernel_panic_regs("Machine Check Exception", regs);

        default: {
            char buf[64];
            serial_printf("UNKNOWN EXCEPTION %llu\n", regs->interrupt);
            const char *pfx = "Unknown Exception #";
            int i = 0;
            while (pfx[i] && i < 50) { buf[i] = pfx[i]; i++; }
            uint64_t v = regs->interrupt;
            if (v >= 100) buf[i++] = '0' + (int)(v / 100);
            if (v >= 10)  buf[i++] = '0' + (int)((v / 10) % 10);
            buf[i++] = '0' + (int)(v % 10);
            buf[i] = '\0';
            kernel_panic_regs(buf, regs);
        }
    }
}

DEFINE_ISR(0x3, isr_breakpoint) {
    (void)frame;
    serial_printf("Breakpoint hit\n");
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
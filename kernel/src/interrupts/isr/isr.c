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

static void dump_rip_bytes_safe(uint64_t rip) {
    uintptr_t hhdm = (uintptr_t)pmm_phys_to_virt(0);
    uint64_t vpage = rip & ~0xFFFULL;
    uintptr_t off  = rip & 0xFFF;

    if (off > PAGE_SIZE - 8) {
        serial_printf("[ISR-UD] RIP=0x%llx near page boundary, skipping byte dump\n", rip);
        return;
    }

    uint64_t cr3_val = 0;
    asm volatile("mov %%cr3, %0" : "=r"(cr3_val));

    volatile uint64_t *pml4v = (volatile uint64_t*)(hhdm + (cr3_val & ~0xFFFULL));
    uint64_t e4 = pml4v[(vpage >> 39) & 0x1FF];
    if (!(e4 & 1)) {
        serial_printf("[ISR-UD] RIP=0x%llx: PML4 not present\n", rip);
        return;
    }

    volatile uint64_t *pdpt = (volatile uint64_t*)(hhdm + (e4 & ~0xFFFULL));
    uint64_t e3 = pdpt[(vpage >> 30) & 0x1FF];
    if (!(e3 & 1)) {
        serial_printf("[ISR-UD] RIP=0x%llx: PDPT not present\n", rip);
        return;
    }

    volatile uint64_t *pd = (volatile uint64_t*)(hhdm + (e3 & ~0xFFFULL));
    uint64_t e2 = pd[(vpage >> 21) & 0x1FF];
    if (!(e2 & 1)) {
        serial_printf("[ISR-UD] RIP=0x%llx: PD not present\n", rip);
        return;
    }

    if (e2 & (1ULL << 7)) {
        uintptr_t hp_phys = (e2 & ~0x1FFFFFULL) + (vpage & 0x1FFFFFULL);
        volatile uint8_t *bytes = (volatile uint8_t*)(hhdm + hp_phys + off);
        serial_printf("[ISR-UD] RIP=0x%llx (huge page) bytes: "
                      "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                      rip,
                      (unsigned)bytes[0], (unsigned)bytes[1],
                      (unsigned)bytes[2], (unsigned)bytes[3],
                      (unsigned)bytes[4], (unsigned)bytes[5],
                      (unsigned)bytes[6], (unsigned)bytes[7]);
        return;
    }

    volatile uint64_t *pt = (volatile uint64_t*)(hhdm + (e2 & ~0xFFFULL));
    uint64_t pte = pt[(vpage >> 12) & 0x1FF];
    if (!(pte & 1)) {
        serial_printf("[ISR-UD] RIP=0x%llx: PT not present (pte=0x%llx)\n", rip, pte);
        return;
    }

    uintptr_t phys_page = pte & ~0xFFFULL;
    volatile uint8_t *bytes = (volatile uint8_t*)(hhdm + phys_page + off);

    serial_printf("[ISR-UD] RIP=0x%llx phys=0x%llx flags=0x%03llx bytes: "
                  "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                  rip, phys_page, pte & 0xFFFULL,
                  (unsigned)bytes[0], (unsigned)bytes[1],
                  (unsigned)bytes[2], (unsigned)bytes[3],
                  (unsigned)bytes[4], (unsigned)bytes[5],
                  (unsigned)bytes[6], (unsigned)bytes[7]);

    if (off >= 16) {
        volatile uint8_t *prev = (volatile uint8_t*)(hhdm + phys_page + off - 16);
        serial_printf("[ISR-UD] RIP-16..RIP-1: "
                      "%02x %02x %02x %02x %02x %02x %02x %02x "
                      "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                      (unsigned)prev[0], (unsigned)prev[1],
                      (unsigned)prev[2], (unsigned)prev[3],
                      (unsigned)prev[4], (unsigned)prev[5],
                      (unsigned)prev[6], (unsigned)prev[7],
                      (unsigned)prev[8], (unsigned)prev[9],
                      (unsigned)prev[10], (unsigned)prev[11],
                      (unsigned)prev[12], (unsigned)prev[13],
                      (unsigned)prev[14], (unsigned)prev[15]);
    }
}

static void dump_user_qword_via_task(uint64_t uaddr, const char *label) {
    percpu_t *pc = get_percpu();
    task_t   *me = pc ? (task_t *)pc->current_task : NULL;
    if (!me) { uint32_t cpu = lapic_get_id(); me = current_task[cpu]; }
    if (!me || !me->pagemap) {
        serial_printf("[ISR-PF] %s: no task/pagemap\n", label);
        return;
    }
    if ((uaddr & 0x7) != 0) {
        serial_printf("[ISR-PF] %s addr 0x%llx not 8-aligned\n", label, uaddr);
        return;
    }
    uint64_t flags = 0;
    if (!vmm_get_page_flags(me->pagemap, uaddr, &flags) || !(flags & 1)) {
        serial_printf("[ISR-PF] %s addr 0x%llx not mapped (flags=0x%llx)\n",
                      label, uaddr, flags);
        return;
    }
    uintptr_t phys = 0;
    if (!vmm_virt_to_phys(me->pagemap, uaddr, &phys)) {
        serial_printf("[ISR-PF] %s virt_to_phys failed for 0x%llx\n", label, uaddr);
        return;
    }
    volatile uint64_t *p = (volatile uint64_t *)pmm_phys_to_virt(phys & ~7ULL);
    uint64_t v = *p;
    serial_printf("[ISR-PF] %s [0x%llx] = 0x%llx\n", label, uaddr, v);
}

static __attribute__((noreturn)) void kill_current_task(int exit_code) {
    percpu_t *pc = get_percpu();
    task_t   *me = pc ? (task_t *)pc->current_task : NULL;
    if (!me) { uint32_t cpu = lapic_get_id(); me = current_task[cpu]; }
    if (me) me->exit_code = exit_code;
    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    task_exit();
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
                    dump_rip_bytes_safe(regs->rip);

                    serial_printf("[ISR-UD] RBP=0x%llx RSP=0x%llx\n",
                                  regs->rbp, regs->rsp);
                }
                serial_printf("[ISR] %s in userspace at RIP=0x%llx — killing task\n",
                              exception_names[regs->interrupt], regs->rip);
                kill_current_task(139);
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
                kill_current_task(139);
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
                dump_rip_bytes_safe(regs->rip);
                dump_user_qword_via_task(regs->rsp,        "[rsp]    ");
                dump_user_qword_via_task(regs->rsp +  8,   "[rsp+8]  ");
                dump_user_qword_via_task(regs->rsp + 16,   "[rsp+16] ");
                dump_user_qword_via_task(regs->rsp + 24,   "[rsp+24] ");
                kill_current_task(139);
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
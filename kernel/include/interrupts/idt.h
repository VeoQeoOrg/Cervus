#ifndef IDT_H
#define IDT_H

#include <stdint.h>

struct idt_entry {
    uint16_t base_low;  
    uint16_t kernel_cs; 
    uint8_t ist; 
    uint8_t attributes;
    uint16_t base_mid; 
    uint32_t base_high; 
    uint32_t reserved; 
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#define IDT_TYPE_TASK       0x5
#define IDT_TYPE_INTERRUPT  0xE
#define IDT_TYPE_TRAP       0xF

#define IDT_PRESENT         (1 << 7)
#define IDT_RING0           (0 << 5)
#define IDT_RING3           (3 << 5)
#define IDT_STORAGE_SEGMENT (1 << 4)

#define IDT_ATTR(type, dpl) (IDT_PRESENT | ((dpl) << 5) | IDT_STORAGE_SEGMENT | (type))

#define IRQ_BASE 0x20
#define IRQ0_TIMER 0x20
#define IRQ1_KEYBOARD 0x21
#define IRQ2_CASCADE 0x22
#define IRQ3_COM2 0x23
#define IRQ4_COM1 0x24
#define IRQ5_LPT2 0x25
#define IRQ6_FLOPPY 0x26
#define IRQ7_LPT1 0x27
#define IRQ8_RTC 0x28
#define IRQ9_ACPI 0x29
#define IRQ10_RESERVED1 0x2A
#define IRQ11_RESERVED2 0x2B
#define IRQ12_MOUSE 0x2C
#define IRQ13_FPU 0x2D
#define IRQ14_ATA1 0x2E
#define IRQ15_ATA2 0x2F

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

struct interrupt_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

typedef void (*interrupt_handler_t)(struct interrupt_frame* frame);

void idt_init(void);
void idt_set_entry(uint8_t index, void *base, uint16_t selector, uint8_t flags, uint8_t ist);

void register_interrupt_handler(uint16_t interrupt, interrupt_handler_t handler);
interrupt_handler_t get_interrupt_handler(uint16_t interrupt);
void irq_handler(struct interrupt_frame* frame);

#endif
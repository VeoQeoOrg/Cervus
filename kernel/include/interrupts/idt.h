#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// Структура дескриптора IDT (16 байт)
struct idt_entry {
    uint16_t offset_low;    // Младшие 16 бит смещения
    uint16_t selector;      // Селектор сегмента кода (0x08 для kernel code)
    uint8_t ist;           // Номер стека IST (0 если не используется)
    uint8_t type_attr;     // Тип и атрибуты
    uint16_t offset_mid;   // Средние 16 бит смещения
    uint32_t offset_high;  // Старшие 32 бита смещения
    uint32_t zero;         // Зарезервировано
} __attribute__((packed));

// Структура указателя на IDT
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Типы шлюзов (Gate Types)
#define IDT_TYPE_TASK       0x5  // Task gate (32-bit)
#define IDT_TYPE_INTERRUPT  0xE  // 64-bit Interrupt gate
#define IDT_TYPE_TRAP       0xF  // 64-bit Trap gate

// Атрибуты дескриптора
#define IDT_PRESENT         (1 << 7)
#define IDT_RING0           (0 << 5)
#define IDT_RING3           (3 << 5)
#define IDT_STORAGE_SEGMENT (1 << 4)

// Макрос для создания атрибутов
#define IDT_ATTR(type, dpl) (IDT_PRESENT | ((dpl) << 5) | IDT_STORAGE_SEGMENT | (type))

// Номера прерываний
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

// Исключения (0-31)
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

// Структура для сохранения регистров
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

// Тип обработчика прерываний
typedef void (*interrupt_handler_t)(struct interrupt_frame* frame);

// Прототипы функций
void idt_init(void);
void idt_set_entry(uint8_t index, uint64_t offset, uint16_t selector, uint8_t type_attr, uint8_t ist);
void idt_load(void);
void register_interrupt_handler(uint8_t interrupt, interrupt_handler_t handler);
void exception_handler(struct interrupt_frame* frame);
void irq_handler(struct interrupt_frame* frame);

// Внешние функции из ассемблера
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

// IRQ handlers (0x20-0x2F)
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

#endif // IDT_H
#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include <stdbool.h>

#define IDT_MAX_DESCRIPTORS 256

#define IS_FLAG_SETTED(x, flag)   ((x) | (flag))
#define FLAG_SET(x, flag)   ((x) |= (flag))
#define FLAG_UNSET(x, flag) ((x) &= ~(flag))

typedef enum {
    IDT_FLAG_GATE_TASK       = 0x5,
    IDT_FLAG_GATE_16BIT_INT  = 0x6,
    IDT_FLAG_GATE_16BIT_TRAP = 0x7,
    IDT_FLAG_GATE_32BIT_INT  = 0xE,
    IDT_FLAG_GATE_32BIT_TRAP = 0xF,

    IDT_FLAG_RING0 = (0 << 5),
    IDT_FLAG_RING1 = (1 << 5),
    IDT_FLAG_RING2 = (2 << 5),
    IDT_FLAG_RING3 = (3 << 5),

    IDT_FLAG_PRESENT = 0x80,

} IDT_FLAGS;

typedef struct {
    uint16_t base_low;
    uint16_t kernel_cs;
    uint8_t ist;
    uint8_t attributes;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t reserved;
}__attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    idt_entry_t *base;
}__attribute__((packed)) idtr_t;

void setup_interrupt_descriptor_table(uint64_t kernel_code_segment);

#endif

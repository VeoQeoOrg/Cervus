#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_CODE_SEGMENT      0x08
#define GDT_DATA_SEGMENT      0x10
#define GDT_USER_CODE_SEGMENT 0x18
#define GDT_USER_DATA_SEGMENT 0x20
#define GDT_LIMIT_LOW(limit)  (limit & 0xFFFF)
#define GDT_BASE_LOW(base)    (base & 0xFFFF)
#define GDT_BASE_MIDDLE(base) ((base >> 16) & 0xFF)
#define GDT_FLAGS_HI_LIMIT(limit, flags)                                       \
    (((limit >> 16) & 0xF) | ((flags << 4) & 0xF0))
#define GDT_BASE_HIGH(base) ((base >> 24) & 0xFF)

#define GDT_ENTRY(base, limit, access, flags)                                  \
    {GDT_LIMIT_LOW(limit),                                                     \
     GDT_BASE_LOW(base),                                                       \
     GDT_BASE_MIDDLE(base),                                                    \
     access,                                                                   \
     GDT_FLAGS_HI_LIMIT(limit, flags),                                         \
     GDT_BASE_HIGH(base)}

#define TSS_SELECTOR_BASE 0x28
#define KERNEL_STACK_SIZE (4096 * 8)

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint32_t iobase;
}__attribute__((packed)) tss_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t limit_high_and_flags;
    uint8_t base_high;
}__attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t limit_high_and_flags;
    uint8_t base_high;
    uint32_t base_higher;
    uint32_t zero;
}__attribute__((packed)) tss_entry_t;

typedef struct {
    uint16_t size;
    gdt_entry_t *pointer;
}__attribute__((packed)) gdt_pointer_t;
extern gdt_pointer_t gdtr;
extern void load_tss(uint16_t sel);
void gdt_init();
void gdt_load(void);

#endif
#ifndef GDT_H
#define GDT_H

#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#define GDT_KERNEL_CS 0x08  
#define GDT_KERNEL_DS 0x10  

#define GDT_ACCESS_PRESENT     (1 << 7)
#define GDT_ACCESS_RING0       (0 << 5)
#define GDT_ACCESS_SEGMENT     (1 << 4)
#define GDT_ACCESS_EXECUTABLE  (1 << 3)
#define GDT_ACCESS_READ_WRITE  (1 << 1)

#define GDT_GRANULARITY_4K     (1 << 7)
#define GDT_GRANULARITY_LONG   (1 << 5)

extern struct gdt_entry gdt[5];
extern struct gdt_ptr gdt_ptr;

void gdt_init(void);
void gdt_load(void);
const struct gdt_entry* gdt_get_descriptor(int index);

#endif 
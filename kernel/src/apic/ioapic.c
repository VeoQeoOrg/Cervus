#include "../../include/apic/apic.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include <stddef.h>

static void ioapic_write_internal(uintptr_t base, uint32_t reg, uint32_t value) {
    if (!base) {
        serial_printf("IOAPIC: Attempt to write to unmapped IOAPIC (reg: 0x%x)\n", reg);
        return;
    }

    volatile uint32_t* ioregsel = (volatile uint32_t*)base;
    volatile uint32_t* iowin = (volatile uint32_t*)(base + 0x10);

    *ioregsel = reg;
    *iowin = value;
}

static uint32_t ioapic_read_internal(uintptr_t base, uint32_t reg) {
    if (!base) {
        serial_printf("IOAPIC: Attempt to read from unmapped IOAPIC (reg: 0x%x)\n", reg);
        return 0;
    }

    volatile uint32_t* ioregsel = (volatile uint32_t*)base;
    volatile uint32_t* iowin = (volatile uint32_t*)(base + 0x10);

    *ioregsel = reg;
    return *iowin;
}

void ioapic_write(uintptr_t base, uint32_t reg, uint32_t value) {
    ioapic_write_internal(base, reg, value);
}

uint32_t ioapic_read(uintptr_t base, uint32_t reg) {
    return ioapic_read_internal(base, reg);
}

uint32_t ioapic_get_max_redirects(uintptr_t base) {
    uint32_t version = ioapic_read(base, IOAPIC_VERSION);
    return ((version >> 16) & 0xFF) + 1;
}

void ioapic_redirect_irq(uint8_t irq, uint8_t vector, uint32_t flags) {
    if (!ioapic_base) return;

    uint32_t max_redirects = ioapic_get_max_redirects(ioapic_base);
    if (irq >= max_redirects) {
        serial_printf("IOAPIC: IRQ %u out of range (max %u)\n", irq, max_redirects);
        return;
    }

    uint32_t low = vector | flags;
    uint32_t high = 0;

    uint32_t redir_reg = IOAPIC_REDIR_START + irq * 2;

    ioapic_write(ioapic_base, redir_reg, low);
    ioapic_write(ioapic_base, redir_reg + 1, high);

    serial_printf("IOAPIC: IRQ %u redirected to vector 0x%x\n", irq, vector);
}

void ioapic_mask_irq(uint8_t irq) {
    if (!ioapic_base) return;

    uint32_t redir_reg = IOAPIC_REDIR_START + irq * 2;
    uint32_t current = ioapic_read(ioapic_base, redir_reg);
    ioapic_write(ioapic_base, redir_reg, current | IOAPIC_INT_MASKED);
}

void ioapic_unmask_irq(uint8_t irq) {
    if (!ioapic_base) return;

    uint32_t redir_reg = IOAPIC_REDIR_START + irq * 2;
    uint32_t current = ioapic_read(ioapic_base, redir_reg);
    ioapic_write(ioapic_base, redir_reg, current & ~IOAPIC_INT_MASKED);
}
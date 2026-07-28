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

void ioapic_redirect_irq(uint32_t gsi, uint8_t vector, uint32_t flags) {
    uintptr_t base;
    uint32_t pin;
    if (!ioapic_resolve_gsi(gsi, &base, &pin)) {
        serial_printf("IOAPIC: no IOAPIC owns GSI %u\n", gsi);
        return;
    }

    uint32_t low = vector | flags;
    uint32_t high = (uint32_t)lapic_get_id() << 24;

    uint32_t redir_reg = IOAPIC_REDIR_START + pin * 2;

    ioapic_write(base, redir_reg, low | IOAPIC_INT_MASKED);
    ioapic_write(base, redir_reg + 1, high);
    ioapic_write(base, redir_reg, low);

    serial_printf("IOAPIC: GSI %u (pin %u) redirected to vector 0x%x\n", gsi, pin, vector);
}

void ioapic_mask_irq(uint32_t gsi) {
    uintptr_t base;
    uint32_t pin;
    if (!ioapic_resolve_gsi(gsi, &base, &pin)) return;

    uint32_t redir_reg = IOAPIC_REDIR_START + pin * 2;
    uint32_t current = ioapic_read(base, redir_reg);
    ioapic_write(base, redir_reg, current | IOAPIC_INT_MASKED);
}

void ioapic_unmask_irq(uint32_t gsi) {
    uintptr_t base;
    uint32_t pin;
    if (!ioapic_resolve_gsi(gsi, &base, &pin)) return;

    uint32_t redir_reg = IOAPIC_REDIR_START + pin * 2;
    uint32_t current = ioapic_read(base, redir_reg);
    ioapic_write(base, redir_reg, current & ~IOAPIC_INT_MASKED);
}
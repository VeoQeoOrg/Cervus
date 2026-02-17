#include "../../include/acpi/acpi.h"
#include "../../include/io/serial.h"
#include "../../include/io/ports.h"
#include "../../include/memory/pmm.h"
#include <string.h>

static acpi_rsdp2_t *rsdp;
static acpi_xsdt_t *xsdt;
static acpi_rsdt_t *rsdt;

static inline void *phys_to_virt(uintptr_t phys) {
    return (void *)(phys + pmm_get_hhdm_offset());
}

static bool checksum(void *base, size_t len) {
    uint8_t sum = 0;
    uint8_t *b = base;
    for (size_t i = 0; i < len; i++)
        sum += b[i];
    return sum == 0;
}

void acpi_init(void) {
    if (!rsdp_request.response) {
        serial_writestring("ACPI: no RSDP\n");
        return;
    }

    rsdp = (acpi_rsdp2_t *)rsdp_request.response->address;

    if (!checksum(&rsdp->rsdp_v1, sizeof(acpi_rsdp_t))) {
        serial_writestring("ACPI: bad RSDP checksum\n");
        return;
    }

    if (rsdp->rsdp_v1.revision >= 2 && rsdp->xsdt_address) {
        xsdt = phys_to_virt(rsdp->xsdt_address);
        if (!checksum(xsdt, xsdt->header.length))
            xsdt = NULL;
    }

    if (!xsdt && rsdp->rsdp_v1.rsdt_address) {
        rsdt = phys_to_virt(rsdp->rsdp_v1.rsdt_address);
        if (!checksum(rsdt, rsdt->header.length))
            rsdt = NULL;
    }

    if (!xsdt && !rsdt)
        return;
}

void *acpi_find_table(const char *sig, uint64_t index) {
    uint64_t count = 0;

    if (xsdt) {
        size_t n = (xsdt->header.length - sizeof(acpi_sdt_header_t)) / 8;
        for (size_t i = 0; i < n; i++) {
            acpi_sdt_header_t *h = phys_to_virt(xsdt->sdt_pointers[i]);
            if (!memcmp(h->signature, sig, 4)) {
                if (count++ == index)
                    return h;
            }
        }
    } else {
        size_t n = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
        for (size_t i = 0; i < n; i++) {
            acpi_sdt_header_t *h = phys_to_virt(rsdt->sdt_pointers[i]);
            if (!memcmp(h->signature, sig, 4)) {
                if (count++ == index)
                    return h;
            }
        }
    }

    return NULL;
}

void acpi_print_tables(void) {
    serial_writestring("ACPI tables:\n");

    for (uint64_t i = 0;; i++) {
        acpi_sdt_header_t *h = acpi_find_table("APIC", i);
        if (!h) break;
        serial_writestring("  - APIC (MADT)\n");
    }

    for (uint64_t i = 0;; i++) {
        acpi_sdt_header_t *h = acpi_find_table("HPET", i);
        if (!h) break;
        serial_writestring("  - HPET\n");
    }

    for (uint64_t i = 0;; i++) {
        acpi_sdt_header_t *h = acpi_find_table("MCFG", i);
        if (!h) break;
        serial_writestring("  - MCFG (PCIe)\n");
    }
}

/*static void acpi_write_gas(const uint8_t *gas, uint8_t value) {
    uint8_t space_id = gas[0];
    uint64_t address = *(uint64_t*)(gas + 4);

    if (space_id == 0x01) {
        volatile uint8_t *mmio = phys_to_virt(address);
        *mmio = value;
    } else if (space_id == 0x00) {
        outb((uint16_t)address, value);
    }
}*/

static void acpi_send_pm1_command(acpi_fadt_t *fadt, uint8_t slp_typ, uint8_t slp_bit) {
    uint16_t cmd = (1 << 13) | (slp_typ << slp_bit);
    outw(fadt->pm1a_control_block, cmd);
    if (fadt->pm1b_control_block) {
        outw(fadt->pm1b_control_block, cmd);
    }
}

void acpi_shutdown(void) {
    acpi_fadt_t *fadt = (acpi_fadt_t *)acpi_find_table("FACP", 0);
    if (!fadt) {
        serial_writestring("ACPI shutdown: FADT not found\n");
        return;
    }

    const uint8_t candidates[] = {5, 0, 7, 1, 2, 3, 4, 6};
    size_t n = sizeof(candidates) / sizeof(candidates[0]);

    serial_writestring("ACPI shutdown: trying S5 candidates...\n");

    for (size_t i = 0; i < n; i++) {
        serial_printf("  Trying SLP_TYP=%d for S5...\n", candidates[i]);
        acpi_send_pm1_command(fadt, candidates[i], 10);
    }
}
#include "../../include/acpi/acpi.h"
#include "../../include/io/serial.h"
#include "../../include/io/ports.h"
#include "../../include/memory/pmm.h"
#include <string.h>

static acpi_rsdp2_t *rsdp;
static acpi_xsdt_t *xsdt;
static acpi_rsdt_t *rsdt;

static uint8_t  s5_slp_typa = 0;
static uint8_t  s5_slp_typb = 0;
static bool     s5_parsed   = false;

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

static bool parse_s5_from_aml(const uint8_t *aml, size_t aml_len) {
    if (!aml || aml_len < 40) return false;

    for (size_t i = 0; i + 20 < aml_len; i++) {
        if (aml[i] == '_' && aml[i+1] == 'S' &&
            aml[i+2] == '5' && aml[i+3] == '_') {

            size_t pos = i + 4;

            for (size_t j = pos; j < pos + 4 && j < aml_len; j++) {
                if (aml[j] == 0x12) {
                    size_t pkg = j + 1;

                    if (pkg >= aml_len) continue;
                    uint8_t lead = aml[pkg];
                    size_t pkg_len_bytes = 1;
                    if (lead & 0xC0) {
                        pkg_len_bytes = (lead >> 6) + 1;
                    }
                    pkg += pkg_len_bytes;

                    if (pkg >= aml_len) continue;

                    pkg++;

                    if (pkg >= aml_len) continue;

                    if (aml[pkg] == 0x0A) {
                        pkg++;
                        if (pkg >= aml_len) continue;
                        s5_slp_typa = aml[pkg];
                        pkg++;
                    } else if (aml[pkg] == 0x00) {
                        s5_slp_typa = 0;
                        pkg++;
                    } else if (aml[pkg] == 0x01) {
                        s5_slp_typa = 1;
                        pkg++;
                    } else {
                        s5_slp_typa = aml[pkg];
                        pkg++;
                    }

                    if (pkg >= aml_len) continue;

                    if (aml[pkg] == 0x0A) {
                        pkg++;
                        if (pkg >= aml_len) continue;
                        s5_slp_typb = aml[pkg];
                    } else if (aml[pkg] == 0x00) {
                        s5_slp_typb = 0;
                    } else if (aml[pkg] == 0x01) {
                        s5_slp_typb = 1;
                    } else {
                        s5_slp_typb = aml[pkg];
                    }

                    serial_printf("ACPI: parsed S5 from AML: SLP_TYPa=%u SLP_TYPb=%u\n",
                                  s5_slp_typa, s5_slp_typb);
                    return true;
                }
            }
        }
    }
    return false;
}

static void parse_s5(void) {
    if (s5_parsed) return;
    s5_parsed = true;

    acpi_fadt_t *fadt = (acpi_fadt_t *)acpi_find_table("FACP", 0);
    if (!fadt) {
        serial_writestring("ACPI: no FADT, cannot parse S5\n");
        return;
    }

    uintptr_t dsdt_phys = 0;
    if (fadt->header.length >= 148 && fadt->x_dsdt)
        dsdt_phys = (uintptr_t)fadt->x_dsdt;
    if (!dsdt_phys)
        dsdt_phys = (uintptr_t)fadt->dsdt;

    if (!dsdt_phys) {
        serial_writestring("ACPI: no DSDT address in FADT\n");
        return;
    }

    acpi_sdt_header_t *dsdt = phys_to_virt(dsdt_phys);

    if (memcmp(dsdt->signature, "DSDT", 4) != 0) {
        serial_writestring("ACPI: invalid DSDT signature\n");
        return;
    }

    serial_printf("ACPI: DSDT at phys=0x%llx length=%u\n",
                  (unsigned long long)dsdt_phys, dsdt->length);

    const uint8_t *aml = (const uint8_t *)dsdt + sizeof(acpi_sdt_header_t);
    size_t aml_len = dsdt->length - sizeof(acpi_sdt_header_t);

    if (parse_s5_from_aml(aml, aml_len))
        return;

    for (uint64_t idx = 0; ; idx++) {
        acpi_sdt_header_t *ssdt = acpi_find_table("SSDT", idx);
        if (!ssdt) break;

        const uint8_t *s_aml = (const uint8_t *)ssdt + sizeof(acpi_sdt_header_t);
        size_t s_len = ssdt->length - sizeof(acpi_sdt_header_t);

        if (parse_s5_from_aml(s_aml, s_len))
            return;
    }

    serial_writestring("ACPI: S5 object not found in DSDT/SSDT, using defaults\n");
    s5_slp_typa = 5;
    s5_slp_typb = 0;
}

static void acpi_enable_mode(acpi_fadt_t *fadt) {
    if (!fadt->smi_command_port || !fadt->acpi_enable)
        return;

    uint16_t pm1a_val = inw(fadt->pm1a_control_block);
    if (pm1a_val & 1) {
        serial_writestring("ACPI: already in ACPI mode\n");
        return;
    }

    serial_writestring("ACPI: enabling ACPI mode...\n");
    outb((uint16_t)fadt->smi_command_port, fadt->acpi_enable);

    for (int i = 0; i < 3000; i++) {
        pm1a_val = inw(fadt->pm1a_control_block);
        if (pm1a_val & 1) {
            serial_writestring("ACPI: ACPI mode enabled\n");
            return;
        }
        for (volatile int j = 0; j < 10000; j++)
            asm volatile("pause");
    }
    serial_writestring("ACPI: WARNING - timeout enabling ACPI mode\n");
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

    parse_s5();
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
    } else if (rsdt) {
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

    for (uint64_t i = 0;; i++) {
        acpi_sdt_header_t *h = acpi_find_table("FACP", i);
        if (!h) break;
        serial_writestring("  - FACP (FADT)\n");
    }
}

static void acpi_write_gas(const acpi_gas_t *gas, uint64_t value) {
    if (gas->address == 0) return;

    switch (gas->address_space_id) {
        case 0x00:
        {
            volatile uint8_t *mmio = phys_to_virt(gas->address);
            switch (gas->register_bit_width) {
                case 8:  *(volatile uint8_t *)mmio = (uint8_t)value; break;
                case 16: *(volatile uint16_t *)mmio = (uint16_t)value; break;
                case 32: *(volatile uint32_t *)mmio = (uint32_t)value; break;
                case 64: *(volatile uint64_t *)mmio = value; break;
            }
            break;
        }
        case 0x01:
        {
            uint16_t port = (uint16_t)gas->address;
            switch (gas->register_bit_width) {
                case 8:  outb(port, (uint8_t)value); break;
                case 16: outw(port, (uint16_t)value); break;
                case 32: outl(port, (uint32_t)value); break;
            }
            break;
        }
    }
}

void acpi_shutdown(void) {
    acpi_fadt_t *fadt = (acpi_fadt_t *)acpi_find_table("FACP", 0);
    if (!fadt) {
        serial_writestring("ACPI shutdown: FADT not found\n");
        goto fallback;
    }

    acpi_enable_mode(fadt);

    parse_s5();

    serial_printf("ACPI shutdown: SLP_TYPa=%u SLP_TYPb=%u\n",
                  s5_slp_typa, s5_slp_typb);
    serial_printf("ACPI shutdown: PM1a_CNT=0x%x PM1b_CNT=0x%x\n",
                  fadt->pm1a_control_block, fadt->pm1b_control_block);

    asm volatile("cli");

    uint16_t pm1a_value = (s5_slp_typa << 10) | (1 << 13);
    outw(fadt->pm1a_control_block, pm1a_value);

    if (fadt->pm1b_control_block) {
        uint16_t pm1b_value = (s5_slp_typb << 10) | (1 << 13);
        outw(fadt->pm1b_control_block, pm1b_value);
    }

    for (volatile int i = 0; i < 100000; i++)
        asm volatile("pause");

    if (fadt->header.length >= 244) {
        if (fadt->x_pm1a_control_block.address) {
            serial_writestring("ACPI shutdown: trying extended PM1a...\n");
            acpi_write_gas(&fadt->x_pm1a_control_block, pm1a_value);
        }
        if (fadt->x_pm1b_control_block.address) {
            uint16_t pm1b_value = (s5_slp_typb << 10) | (1 << 13);
            acpi_write_gas(&fadt->x_pm1b_control_block, pm1b_value);
        }

        for (volatile int i = 0; i < 100000; i++)
            asm volatile("pause");
    }

fallback:
    serial_writestring("ACPI shutdown: trying QEMU/Bochs port 0x604...\n");
    outw(0x604, 0x2000);

    for (volatile int i = 0; i < 100000; i++)
        asm volatile("pause");

    serial_writestring("ACPI shutdown: trying VirtualBox port 0x4004...\n");
    outw(0x4004, 0x3400);

    for (volatile int i = 0; i < 100000; i++)
        asm volatile("pause");

    serial_writestring("ACPI shutdown: trying legacy APM...\n");
    outw(0xB004, 0x2000);

    serial_writestring("ACPI shutdown: all methods failed, halting CPU\n");
    for (;;)
        asm volatile("cli; hlt");
}

void acpi_reboot(void) {
    acpi_fadt_t *fadt = (acpi_fadt_t *)acpi_find_table("FACP", 0);
    asm volatile("cli");

    if (fadt && fadt->header.length >= 129) {
        if ((fadt->flags & (1 << 10)) && fadt->reset_reg.address) {
            serial_printf("ACPI reboot: using FADT reset register "
                          "(space=%u addr=0x%llx val=0x%x)\n",
                          fadt->reset_reg.address_space_id,
                          (unsigned long long)fadt->reset_reg.address,
                          fadt->reset_value);

            acpi_write_gas(&fadt->reset_reg, fadt->reset_value);

            for (volatile int i = 0; i < 100000; i++)
                asm volatile("pause");
        }
    }

    serial_writestring("ACPI reboot: trying 8042 keyboard controller reset...\n");

    for (int i = 0; i < 10000; i++) {
        if (!(inb(0x64) & 0x02))
            break;
        for (volatile int j = 0; j < 100; j++)
            asm volatile("pause");
    }

    outb(0x64, 0xFE);

    for (volatile int i = 0; i < 100000; i++)
        asm volatile("pause");

    serial_writestring("ACPI reboot: triggering triple fault...\n");

    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idt = { 0, 0 };

    asm volatile(
        "lidt %0\n"
        "int $3\n"
        :: "m"(null_idt)
    );

    for (;;)
        asm volatile("cli; hlt");
}
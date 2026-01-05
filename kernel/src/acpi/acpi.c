#include "../../include/acpi/acpi.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/ports.h"
#include <string.h>
#include <stdio.h>

static acpi_rsdp2_t* rsdp = NULL;
static acpi_xsdt_t* xsdt = NULL;
static acpi_rsdt_t* rsdt = NULL;
static bool acpi_available = false;

static inline void* phys_to_virt(uintptr_t phys_addr) {
    return (void*)(phys_addr + pmm_get_hhdm_offset());
}

static bool validate_table_checksum(acpi_sdt_header_t* header) {
    if (!header) return false;
    
    uint8_t sum = 0;
    uint8_t* bytes = (uint8_t*)header;
    
    for (size_t i = 0; i < header->length; i++) {
        sum += bytes[i];
    }
    
    return (sum == 0);
}

void acpi_init(void) {
    serial_writestring(COM1, "\n[ACPI] Initializing ACPI...\n");
    
    if (!rsdp_request.response || !rsdp_request.response->address) {
        serial_writestring(COM1, "ERROR: RSDP not found via Limine\n");
        return;
    }
    
    rsdp = (acpi_rsdp2_t*)rsdp_request.response->address;
    
    char sig[9] = {0};
    memcpy(sig, rsdp->rsdp_v1.signature, 8);
    
    serial_printf(COM1, "RSDP found at: 0x%llx\n", (uint64_t)rsdp);
    serial_printf(COM1, "Signature: %s\n", sig);
    serial_printf(COM1, "Revision: %u\n", rsdp->rsdp_v1.revision);
    
    uint8_t sum = 0;
    uint8_t* bytes = (uint8_t*)&rsdp->rsdp_v1;
    for (size_t i = 0; i < sizeof(acpi_rsdp_t); i++) {
        sum += bytes[i];
    }
    
    if (sum != 0) {
        serial_writestring(COM1, "WARNING: RSDP v1 checksum invalid\n");
    } else {
        serial_writestring(COM1, "RSDP v1 checksum: OK\n");
    }
    
    uint64_t hhdm_offset = pmm_get_hhdm_offset();
    serial_printf(COM1, "HHDM offset: 0x%llx\n", hhdm_offset);
    
    if (rsdp->rsdp_v1.revision >= 2 && rsdp->xsdt_address != 0) {
        xsdt = (acpi_xsdt_t*)phys_to_virt(rsdp->xsdt_address);
        serial_printf(COM1, "XSDT physical: 0x%llx, virtual: 0x%llx\n", 
                     rsdp->xsdt_address, (uint64_t)xsdt);
        
        if (!validate_table_checksum(&xsdt->header)) {
            serial_writestring(COM1, "WARNING: XSDT checksum invalid\n");
            xsdt = NULL;
        } else {
            serial_writestring(COM1, "XSDT checksum: OK\n");
        }
    }
    
    if (!xsdt && rsdp->rsdp_v1.rsdt_address != 0) {
        rsdt = (acpi_rsdt_t*)phys_to_virt(rsdp->rsdp_v1.rsdt_address);
        serial_printf(COM1, "RSDT physical: 0x%x, virtual: 0x%llx\n", 
                     rsdp->rsdp_v1.rsdt_address, (uint64_t)rsdt);
        
        if (!validate_table_checksum(&rsdt->header)) {
            serial_writestring(COM1, "WARNING: RSDT checksum invalid\n");
            rsdt = NULL;
        } else {
            serial_writestring(COM1, "RSDT checksum: OK\n");
        }
    }
    
    if (!xsdt && !rsdt) {
        serial_writestring(COM1, "ERROR: No valid RSDT/XSDT found\n");
        return;
    }
    
    acpi_available = true;
    serial_writestring(COM1, "[ACPI] ACPI initialized successfully\n");
}

bool acpi_is_available(void) {
    return acpi_available;
}

void* acpi_find_table(const char* signature, uint64_t index) {
    if (!acpi_available) {
        return NULL;
    }
    
    uint64_t count = 0;
    
    if (xsdt) {
        size_t num_tables = (xsdt->header.length - sizeof(acpi_sdt_header_t)) / 8;
        
        for (size_t i = 0; i < num_tables; i++) {
            uint64_t table_phys = xsdt->sdt_pointers[i];
            acpi_sdt_header_t* header = (acpi_sdt_header_t*)phys_to_virt(table_phys);
            
            if (!header) continue;
            
            bool match = true;
            for (int j = 0; j < 4; j++) {
                if (header->signature[j] != signature[j]) {
                    match = false;
                    break;
                }
            }
            
            if (match) {
                if (count == index) {
                    if (validate_table_checksum(header)) {
                        return header;
                    } else {
                        serial_printf(COM1, "Table %.4s has invalid checksum\n", signature);
                        return NULL;
                    }
                }
                count++;
            }
        }
    } else if (rsdt) {
        size_t num_tables = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
        
        for (size_t i = 0; i < num_tables; i++) {
            uint32_t table_phys = rsdt->sdt_pointers[i];
            acpi_sdt_header_t* header = (acpi_sdt_header_t*)phys_to_virt(table_phys);
            
            if (!header) continue;
            
            bool match = true;
            for (int j = 0; j < 4; j++) {
                if (header->signature[j] != signature[j]) {
                    match = false;
                    break;
                }
            }
            
            if (match) {
                if (count == index) {
                    if (validate_table_checksum(header)) {
                        return header;
                    } else {
                        serial_printf(COM1, "Table %.4s has invalid checksum\n", signature);
                        return NULL;
                    }
                }
                count++;
            }
        }
    }
    
    return NULL;
}

void acpi_print_tables(void) {
    if (!acpi_available) {
        serial_writestring(COM1, "ACPI not available\n");
        return;
    }
    
    serial_writestring(COM1, "\nACPI TABLES:\n");
    
    if (xsdt) {
        size_t num_tables = (xsdt->header.length - sizeof(acpi_sdt_header_t)) / 8;
        serial_printf(COM1, "XSDT contains %zu tables:\n", num_tables);
        
        for (size_t i = 0; i < num_tables; i++) {
            uint64_t table_phys = xsdt->sdt_pointers[i];
            acpi_sdt_header_t* header = (acpi_sdt_header_t*)phys_to_virt(table_phys);
            
            if (header) {
                bool checksum_ok = validate_table_checksum(header);
                serial_printf(COM1, "  [%zu] %.4s (phys: 0x%llx, virt: 0x%llx) Rev: %u Len: %u %s\n",
                             i, header->signature, table_phys, (uint64_t)header,
                             header->revision, header->length,
                             checksum_ok ? "" : "[BAD CHECKSUM]");
            } else {
                serial_printf(COM1, "  [%zu] INVALID (phys: 0x%llx)\n", i, table_phys);
            }
        }
    } else if (rsdt) {
        size_t num_tables = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
        serial_printf(COM1, "RSDT contains %zu tables:\n", num_tables);
        
        for (size_t i = 0; i < num_tables; i++) {
            uint32_t table_phys = rsdt->sdt_pointers[i];
            acpi_sdt_header_t* header = (acpi_sdt_header_t*)phys_to_virt(table_phys);
            
            if (header) {
                bool checksum_ok = validate_table_checksum(header);
                serial_printf(COM1, "  [%zu] %.4s (phys: 0x%x, virt: 0x%llx) Rev: %u Len: %u %s\n",
                             i, header->signature, table_phys, (uint64_t)header,
                             header->revision, header->length,
                             checksum_ok ? "" : "[BAD CHECKSUM]");
            } else {
                serial_printf(COM1, "  [%zu] INVALID (phys: 0x%x)\n", i, table_phys);
            }
        }
    }
}
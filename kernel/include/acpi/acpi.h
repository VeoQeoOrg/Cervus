#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    acpi_rsdp_t rsdp_v1;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp2_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t sdt_pointers[];
} __attribute__((packed)) acpi_rsdt_t;

typedef struct {
    acpi_sdt_header_t header;
    uint64_t sdt_pointers[];
} __attribute__((packed)) acpi_xsdt_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_power_management_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command_port;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pm_timer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t pm1_event_length;
    uint8_t pm1_control_length;
    uint8_t pm2_control_length;
    uint8_t pm_timer_length;
    uint8_t gpe0_length;
    uint8_t gpe1_length;
    uint8_t gpe1_base;
    uint8_t cstate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t month_alarm;
    uint8_t century;
    uint16_t boot_architecture_flags;
    uint8_t reserved2;
    uint32_t flags;
    uint8_t reset_reg[12];
    uint8_t reset_value;
    uint16_t arm_boot_architecture_flags;
    uint8_t fadt_minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    uint8_t x_pm1a_event_block[12];
    uint8_t x_pm1b_event_block[12];
    uint8_t x_pm1a_control_block[12];
    uint8_t x_pm1b_control_block[12];
    uint8_t x_pm2_control_block[12];
    uint8_t x_pm_timer_block[12];
    uint8_t x_gpe0_block[12];
    uint8_t x_gpe1_block[12];
} __attribute__((packed)) acpi_fadt_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed)) acpi_madt_t;

#define MADT_ENTRY_LAPIC         0
#define MADT_ENTRY_IOAPIC        1
#define MADT_ENTRY_ISO           2
#define MADT_ENTRY_NMI           4
#define MADT_ENTRY_LAPIC_ADDR    5
#define MADT_ENTRY_IOAPIC_MMIO   6

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) madt_entry_header_t;

typedef struct {
    madt_entry_header_t header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed)) madt_lapic_entry_t;

typedef struct {
    madt_entry_header_t header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed)) madt_ioapic_entry_t;

typedef struct {
    madt_entry_header_t header;
    uint8_t bus;
    uint8_t source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed)) madt_iso_entry_t;

typedef struct {
    acpi_sdt_header_t header;
    uint8_t hardware_rev_id;
    uint8_t comparator_count : 5;
    uint8_t counter_size : 1;
    uint8_t reserved : 1;
    uint8_t legacy_replacement : 1;
    uint16_t pci_vendor_id;
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t reserved2;
    uint64_t address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} __attribute__((packed)) acpi_hpet_t;

typedef struct {
    acpi_sdt_header_t header;
    uint64_t reserved;
} __attribute__((packed)) acpi_mcfg_t;

typedef struct {
    uint64_t base_address;
    uint16_t pci_segment_group;
    uint8_t start_pci_bus;
    uint8_t end_pci_bus;
    uint32_t reserved;
} __attribute__((packed)) mcfg_entry_t;

typedef struct {
    acpi_sdt_header_t header;
    uint8_t definition_block[];
} __attribute__((packed)) acpi_ssdt_t;

void acpi_init(void);
bool acpi_is_available(void);
void* acpi_find_table(const char* signature, uint64_t index);
void acpi_print_tables(void);

extern volatile struct limine_rsdp_request rsdp_request;

void acpi_shutdown(void);

#endif
#ifndef _KERNEL_DRIVERS_PCI_H
#define _KERNEL_DRIVERS_PCI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PCI_MAX_DEVICES   256

#define PCI_VENDOR_ID     0x00
#define PCI_DEVICE_ID     0x02
#define PCI_COMMAND       0x04
#define PCI_STATUS        0x06
#define PCI_REVISION_ID   0x08
#define PCI_PROG_IF       0x09
#define PCI_SUBCLASS      0x0A
#define PCI_CLASS         0x0B
#define PCI_HEADER_TYPE   0x0E
#define PCI_BAR0          0x10
#define PCI_SECONDARY_BUS 0x19
#define PCI_CAPABILITY_PTR 0x34
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_INTERRUPT_PIN  0x3D

#define PCI_COMMAND_IO        0x0001
#define PCI_COMMAND_MEMORY    0x0002
#define PCI_COMMAND_MASTER    0x0004
#define PCI_COMMAND_INTX_DIS  0x0400

#define PCI_STATUS_CAPS       0x0010

#define PCI_HEADER_MULTIFUNC  0x80
#define PCI_HEADER_TYPE_NORMAL 0x00
#define PCI_HEADER_TYPE_BRIDGE 0x01
#define PCI_HEADER_TYPE_CARDBUS 0x02

#define PCI_CAP_ID_MSI    0x05
#define PCI_CAP_ID_MSIX   0x11

#define PCI_BAR_TYPE_MEM  0
#define PCI_BAR_TYPE_IO   1

typedef struct {
    uint64_t base;
    uint64_t size;
    uint8_t  type;
    uint8_t  is_64bit;
    uint8_t  prefetchable;
    uint8_t  _pad;
} pci_bar_t;

typedef struct {
    uint16_t segment;
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
    uint8_t  irq_line;
    uint8_t  irq_pin;
    uint8_t  _pad;

    pci_bar_t bars[6];

    uint8_t  cap_msi_off;
    uint8_t  cap_msix_off;
    uint16_t msix_table_size;
} pci_device_t;

typedef struct pci_driver pci_driver_t;

struct pci_driver {
    const char *name;

    int16_t  match_vendor;
    int16_t  match_device;
    int16_t  match_class;
    int16_t  match_subclass;

    int (*probe)(pci_device_t *dev);
};

void pci_init(void);

uint32_t pci_config_read32 (uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off);
uint16_t pci_config_read16 (uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off);
uint8_t  pci_config_read8  (uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off);
void     pci_config_write32(uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off, uint32_t v);
void     pci_config_write16(uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off, uint16_t v);
void     pci_config_write8 (uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off, uint8_t v);

int                pci_device_count(void);
pci_device_t      *pci_device_at(int index);
const pci_device_t *pci_find_by_id(uint16_t vendor, uint16_t device);
const pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass);

void pci_register_driver(const pci_driver_t *drv);

int pci_enable_msi(pci_device_t *dev, uint8_t vector, uint32_t apic_lapic_id);
int pci_enable_msix(pci_device_t *dev, uint8_t base_vector, uint32_t apic_lapic_id);

const char *pci_class_name(uint8_t class_code);

#endif

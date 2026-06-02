#ifndef _KERNEL_DRIVERS_AHCI_H
#define _KERNEL_DRIVERS_AHCI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define AHCI_MAX_PORTS    32
#define AHCI_MAX_DEVICES  AHCI_MAX_PORTS
#define AHCI_SECTOR_SIZE  512
#define ATAPI_SECTOR_SIZE 2048

typedef struct ahci_hba ahci_hba_t;

typedef struct {
    bool          present;
    bool          atapi;
    uint8_t       port_idx;
    ahci_hba_t   *hba;
    uint64_t      sectors;
    uint64_t      size_bytes;
    char          model[41];
    char          serial[21];
    char          firmware[9];

    void         *clb_virt;
    void         *fb_virt;
    void         *ctba_virt;
    uintptr_t     clb_phys;
    uintptr_t     fb_phys;
    uintptr_t     ctba_phys;
} ahci_device_t;

struct ahci_hba {
    volatile uint8_t *abar;
    uint16_t          seg;
    uint8_t           bus, dev, func;
    uint32_t          cap;
    uint32_t          pi;
    uint8_t           ncs;
    bool              s64a;
    ahci_device_t     ports[AHCI_MAX_PORTS];
    int               port_count;
};

void ahci_init(void);
int  ahci_device_count(void);
ahci_device_t *ahci_get_device(int index);

int ahci_read_sectors(ahci_device_t *d, uint64_t lba, uint32_t count, void *buf);
int ahci_write_sectors(ahci_device_t *d, uint64_t lba, uint32_t count, const void *buf);
int ahci_flush(ahci_device_t *d);

int ahci_eject(ahci_device_t *d);

#endif

#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define ATA_PRIMARY_IO       0x1F0
#define ATA_PRIMARY_CTRL     0x3F6
#define ATA_SECONDARY_IO     0x170
#define ATA_SECONDARY_CTRL   0x376

#define ATA_REG_DATA         0x00
#define ATA_REG_ERROR        0x01
#define ATA_REG_FEATURES     0x01
#define ATA_REG_SECCOUNT     0x02
#define ATA_REG_LBA_LO       0x03
#define ATA_REG_LBA_MID      0x04
#define ATA_REG_LBA_HI       0x05
#define ATA_REG_DRIVE        0x06
#define ATA_REG_STATUS       0x07
#define ATA_REG_COMMAND      0x07

#define ATA_REG_ALT_STATUS   0x00
#define ATA_REG_DEV_CTRL     0x00

#define ATA_SR_BSY           0x80
#define ATA_SR_DRDY          0x40
#define ATA_SR_DF            0x20
#define ATA_SR_DSC           0x10
#define ATA_SR_DRQ           0x08
#define ATA_SR_CORR          0x04
#define ATA_SR_IDX           0x02
#define ATA_SR_ERR           0x01

#define ATA_CMD_READ_PIO     0x20
#define ATA_CMD_READ_PIO_EXT 0x24
#define ATA_CMD_WRITE_PIO    0x30
#define ATA_CMD_WRITE_PIO_EXT 0x34
#define ATA_CMD_CACHE_FLUSH  0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1

#define ATA_DRIVE_MASTER     0xA0
#define ATA_DRIVE_SLAVE      0xB0

#define ATA_LBA_BIT          0x40

#define ATA_MAX_DRIVES       4

#define ATA_SECTOR_SIZE      512

typedef struct {
    bool     present;
    bool     is_atapi;
    bool     lba48;
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t  drive_select;
    uint8_t  irq;
    uint64_t sectors;
    uint64_t size_bytes;
    char     model[41];
    char     serial[21];
    char     firmware[9];
    uint16_t identify[256];
} ata_drive_t;

void ata_init(void);
ata_drive_t *ata_get_drive(int index);
int ata_get_drive_count(void);
int ata_read_sectors(ata_drive_t *drive, uint64_t lba, uint32_t count, void *buffer);
int ata_write_sectors(ata_drive_t *drive, uint64_t lba, uint32_t count, const void *buffer);
int ata_flush(ata_drive_t *drive);

#endif
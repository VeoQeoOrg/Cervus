#ifndef PARTITION_H
#define PARTITION_H

#include <stdint.h>
#include <stdbool.h>
#include "blkdev.h"

#define MBR_SIGNATURE       0xAA55
#define MBR_SIGNATURE_OFF   510
#define MBR_PARTITION_OFF   446
#define MBR_MAX_PARTITIONS  4

#define MBR_TYPE_EMPTY      0x00
#define MBR_TYPE_FAT12      0x01
#define MBR_TYPE_FAT16_S    0x04
#define MBR_TYPE_EXTENDED   0x05
#define MBR_TYPE_FAT16      0x06
#define MBR_TYPE_FAT32_CHS  0x0B
#define MBR_TYPE_FAT32_LBA  0x0C
#define MBR_TYPE_FAT16_LBA  0x0E
#define MBR_TYPE_LINUX      0x83
#define MBR_TYPE_ESP        0xEF

typedef struct __attribute__((packed)) {
    uint8_t  boot_flag;
    uint8_t  chs_start[3];
    uint8_t  type;
    uint8_t  chs_end[3];
    uint32_t lba_start;
    uint32_t sector_count;
} mbr_partition_t;

typedef struct __attribute__((packed)) {
    uint8_t         bootstrap[440];
    uint32_t        disk_signature;
    uint16_t        reserved;
    mbr_partition_t partitions[4];
    uint16_t        signature;
} mbr_t;

_Static_assert(sizeof(mbr_t) == 512, "mbr size must be 512");

int partition_scan(blkdev_t *disk);

int partition_write_mbr(blkdev_t *disk, const mbr_partition_t parts[4],
                        uint32_t disk_signature);

int partition_read_mbr(blkdev_t *disk, mbr_t *out);

blkdev_t *partition_get(const char *name);

#endif
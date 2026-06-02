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

#define GPT_SIGNATURE_BYTES { 'E','F','I',' ','P','A','R','T' }
#define MBR_TYPE_GPT_PROT   0xEE

typedef struct __attribute__((packed)) {
    uint8_t  signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t entry_array_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entry_array_crc32;
} gpt_header_t;

_Static_assert(sizeof(gpt_header_t) == 92, "gpt_header size");

typedef struct __attribute__((packed)) {
    uint8_t  type_guid[16];
    uint8_t  part_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name_utf16[36];
} gpt_entry_t;

_Static_assert(sizeof(gpt_entry_t) == 128, "gpt_entry size");

int partition_scan(blkdev_t *disk);

int partition_write_mbr(blkdev_t *disk, const mbr_partition_t parts[4],
                        uint32_t disk_signature);

int partition_read_mbr(blkdev_t *disk, mbr_t *out);

blkdev_t *partition_get(const char *name);
int partition_remove_children(blkdev_t *parent);

const char *gpt_type_guid_name(const uint8_t guid[16]);

typedef struct {
    uint8_t  type_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    char     name[36];
} gpt_partition_spec_t;

int partition_write_gpt(blkdev_t *disk,
                        const gpt_partition_spec_t *specs, size_t count,
                        const uint8_t disk_guid[16]);

#endif
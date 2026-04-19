#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../drivers/blkdev.h"
#include "vfs.h"

#define FAT32_SECTOR_SIZE         512
#define FAT32_EOC                 0x0FFFFFFF
#define FAT32_EOC_MIN             0x0FFFFFF8
#define FAT32_FREE_CLUSTER        0x00000000
#define FAT32_BAD_CLUSTER         0x0FFFFFF7
#define FAT32_CLUSTER_MASK        0x0FFFFFFF

#define FAT32_FSINFO_LEAD_SIG     0x41615252
#define FAT32_FSINFO_STRUCT_SIG   0x61417272
#define FAT32_FSINFO_TRAIL_SIG    0xAA550000

#define FAT_ATTR_READ_ONLY        0x01
#define FAT_ATTR_HIDDEN           0x02
#define FAT_ATTR_SYSTEM           0x04
#define FAT_ATTR_VOLUME_ID        0x08
#define FAT_ATTR_DIRECTORY        0x10
#define FAT_ATTR_ARCHIVE          0x20
#define FAT_ATTR_LONG_NAME        0x0F

typedef struct __attribute__((packed)) {
    uint8_t  bs_jmp[3];
    uint8_t  bs_oem[8];
    uint16_t bpb_bytes_per_sector;
    uint8_t  bpb_sectors_per_cluster;
    uint16_t bpb_reserved_sectors;
    uint8_t  bpb_num_fats;
    uint16_t bpb_root_entries;
    uint16_t bpb_total_sectors_16;
    uint8_t  bpb_media;
    uint16_t bpb_fat_size_16;
    uint16_t bpb_sectors_per_track;
    uint16_t bpb_num_heads;
    uint32_t bpb_hidden_sectors;
    uint32_t bpb_total_sectors_32;
    uint32_t bpb_fat_size_32;
    uint16_t bpb_ext_flags;
    uint16_t bpb_fs_version;
    uint32_t bpb_root_cluster;
    uint16_t bpb_fs_info;
    uint16_t bpb_backup_boot;
    uint8_t  bpb_reserved[12];
    uint8_t  bs_drive_num;
    uint8_t  bs_reserved1;
    uint8_t  bs_boot_sig;
    uint32_t bs_vol_id;
    uint8_t  bs_vol_label[11];
    uint8_t  bs_fs_type[8];
    uint8_t  bs_boot_code[420];
    uint16_t bs_signature;
} fat32_bpb_t;

_Static_assert(sizeof(fat32_bpb_t) == 512, "fat32 bpb size");

typedef struct __attribute__((packed)) {
    uint32_t lead_sig;
    uint8_t  reserved1[480];
    uint32_t struct_sig;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;
} fat32_fsinfo_t;

_Static_assert(sizeof(fat32_fsinfo_t) == 512, "fat32 fsinfo size");

typedef struct __attribute__((packed)) {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_lo;
    uint32_t file_size;
} fat32_dirent_t;

_Static_assert(sizeof(fat32_dirent_t) == 32, "fat32 dirent size");

typedef struct __attribute__((packed)) {
    uint8_t  order;
    uint8_t  name1[10];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint8_t  name2[12];
    uint16_t cluster_lo;
    uint8_t  name3[4];
} fat32_lfn_t;

_Static_assert(sizeof(fat32_lfn_t) == 32, "fat32 lfn size");

typedef struct {
    blkdev_t *dev;
    uint32_t  bytes_per_sector;
    uint32_t  sectors_per_cluster;
    uint32_t  bytes_per_cluster;
    uint32_t  reserved_sectors;
    uint32_t  num_fats;
    uint32_t  fat_size_sectors;
    uint32_t  total_sectors;
    uint32_t  root_cluster;
    uint32_t  first_fat_sector;
    uint32_t  first_data_sector;
    uint32_t  total_clusters;
    uint32_t  fsinfo_sector;
    uint32_t  next_free_hint;
    uint32_t  free_count;
    bool      dirty;
    bool      readonly;
    uint8_t   *shared_buf;
} fat32_t;

typedef struct {
    fat32_t  *fs;
    uint32_t  first_cluster;
    uint32_t  dir_cluster;
    uint32_t  dir_entry_offset;
    uint32_t  file_size;
    uint8_t   attr;

    uint32_t  cached_cluster;
    uint32_t  cached_index;
    bool      size_dirty;

    uint8_t  *io_buf;
} fat32_vdata_t;

int      fat32_format(blkdev_t *dev, const char *label);
vnode_t *fat32_mount(blkdev_t *dev);
void     fat32_unmount(fat32_t *fs);
int      fat32_sync(fat32_t *fs);

#endif
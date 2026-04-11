#ifndef CERVUSFS_H
#define CERVUSFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../drivers/blkdev.h"
#include "vfs.h"

#define CERVUSFS_MAGIC      0x43455256
#define CERVUSFS_VERSION     1
#define CERVUSFS_BLOCK_SIZE  512
#define CERVUSFS_NAME_MAX    56

#define CERVUSFS_INODE_FILE  0
#define CERVUSFS_INODE_DIR   1

#define CERVUSFS_INODES_PER_SECTOR  8
#define CERVUSFS_INODE_SIZE         64

#define CERVUSFS_DIRECT_BLOCKS  12

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t total_sectors;
    uint32_t inode_count;
    uint32_t data_block_count;
    uint32_t inode_bitmap_start;
    uint32_t inode_bitmap_sectors;
    uint32_t data_bitmap_start;
    uint32_t data_bitmap_sectors;
    uint32_t inode_table_start;
    uint32_t inode_table_sectors;
    uint32_t data_start;
    uint32_t free_inodes;
    uint32_t free_blocks;
    uint32_t root_inode;
    uint8_t  label[16];
    uint8_t  _pad[512 - 15*4 - 16];
} cervusfs_super_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  _pad0;
    uint16_t mode;
    uint32_t size;
    uint32_t blocks[12];
    uint32_t _reserved[2];
} cervusfs_inode_t;

_Static_assert(sizeof(cervusfs_inode_t) == CERVUSFS_INODE_SIZE, "inode size");

#define CERVUSFS_DIRENTS_PER_BLOCK  8

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint8_t  type;
    uint8_t  name_len;
    char     name[CERVUSFS_NAME_MAX];
    uint8_t  _pad[2];
} cervusfs_dirent_disk_t;

_Static_assert(sizeof(cervusfs_dirent_disk_t) == 64, "dirent size");

typedef struct {
    blkdev_t          *dev;
    cervusfs_super_t   sb;
    uint8_t           *inode_bitmap;
    uint8_t           *data_bitmap;
    bool               dirty;
} cervusfs_t;

int cervusfs_format(blkdev_t *dev, const char *label);
vnode_t *cervusfs_mount(blkdev_t *dev);
void cervusfs_unmount(cervusfs_t *fs);
void cervusfs_sync(cervusfs_t *fs);

#endif
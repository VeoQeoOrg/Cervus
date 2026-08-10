#ifndef EXT4_H
#define EXT4_H

#include <stdint.h>
#include "ext2.h"

#define EXT4_FEATURE_INCOMPAT_EXTENTS   0x0040
#define EXT4_FEATURE_INCOMPAT_64BIT     0x0080
#define EXT4_FEATURE_INCOMPAT_FLEX_BG   0x0200

#define EXT4_EXTENT_MAGIC               0xF30A
#define EXT4_INIT_MAX_LEN               32768

typedef struct __attribute__((packed)) {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} ext4_extent_header_t;

typedef struct __attribute__((packed)) {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} ext4_extent_idx_t;

typedef struct __attribute__((packed)) {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} ext4_extent_t;

int32_t ext4_extent_lookup(ext2_t *fs, ext2_inode_t *di, uint32_t file_block);
void ext4_inode_set_extent(ext2_inode_t *di, uint64_t phys_block, uint16_t num_blocks);

#endif

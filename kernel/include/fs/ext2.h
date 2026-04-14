#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../drivers/blkdev.h"
#include "vfs.h"

#define EXT2_SUPER_MAGIC    0xEF53
#define EXT2_SUPER_OFFSET   1024
#define EXT2_ROOT_INO       2
#define EXT2_GOOD_OLD_REV   0
#define EXT2_DYNAMIC_REV    1
#define EXT2_VALID_FS       1
#define EXT2_ERROR_FS       2

#define EXT2_S_IFSOCK       0xC000
#define EXT2_S_IFLNK        0xA000
#define EXT2_S_IFREG        0x8000
#define EXT2_S_IFBLK        0x6000
#define EXT2_S_IFDIR        0x4000
#define EXT2_S_IFCHR        0x2000
#define EXT2_S_IFIFO        0x1000

#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

#define EXT2_NDIR_BLOCKS    12
#define EXT2_IND_BLOCK      12
#define EXT2_DIND_BLOCK     13
#define EXT2_TIND_BLOCK     14
#define EXT2_N_BLOCKS       15
#define EXT2_NAME_LEN       255

typedef struct __attribute__((packed)) {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_padding1;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_reserved_pad[3];
    uint32_t s_default_mount_options;
    uint32_t s_first_meta_bg;
    uint8_t  s_reserved[760];
} ext2_superblock_t;

_Static_assert(sizeof(ext2_superblock_t) == 1024, "ext2 superblock size");

typedef struct __attribute__((packed)) {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} ext2_group_desc_t;

_Static_assert(sizeof(ext2_group_desc_t) == 32, "ext2 group desc size");

typedef struct __attribute__((packed)) {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT2_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} ext2_inode_t;

_Static_assert(sizeof(ext2_inode_t) == 128, "ext2 inode size");

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[EXT2_NAME_LEN];
} ext2_dir_entry_t;

typedef struct {
    blkdev_t          *dev;
    ext2_superblock_t  sb;
    ext2_group_desc_t *gdt;
    uint32_t           block_size;
    uint32_t           groups_count;
    uint32_t           inodes_per_block;
    uint32_t           inode_size;
    uint32_t           ptrs_per_block;
    bool               dirty;
} ext2_t;

typedef struct {
    ext2_t  *fs;
    uint32_t ino;
} ext2_vdata_t;

int ext2_format(blkdev_t *dev, const char *label);
vnode_t *ext2_mount(blkdev_t *dev);
void ext2_unmount(ext2_t *fs);
void ext2_sync(ext2_t *fs);

#endif
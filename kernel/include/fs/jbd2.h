#ifndef JBD2_H
#define JBD2_H

#include <stdint.h>
#include "ext2.h"

#define JBD2_MAGIC_NUMBER 0xc03b3998U

#define JBD2_DESCRIPTOR_BLOCK 1
#define JBD2_COMMIT_BLOCK     2
#define JBD2_SUPERBLOCK_V1    3
#define JBD2_SUPERBLOCK_V2    4
#define JBD2_REVOKE_BLOCK     5

#define JBD2_FLAG_ESCAPE     1
#define JBD2_FLAG_SAME_UUID  2
#define JBD2_FLAG_DELETED    4
#define JBD2_FLAG_LAST_TAG   8

#define JBD2_FEATURE_INCOMPAT_REVOKE       0x00000001
#define JBD2_FEATURE_INCOMPAT_64BIT        0x00000002
#define JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT 0x00000004
#define JBD2_FEATURE_INCOMPAT_CSUM_V2      0x00000008
#define JBD2_FEATURE_INCOMPAT_CSUM_V3      0x00000010

#define EXT3_FEATURE_COMPAT_HAS_JOURNAL 0x0004
#define EXT3_FEATURE_INCOMPAT_RECOVER   0x0004
#define EXT2_JOURNAL_INO 8

typedef struct __attribute__((packed)) {
    uint32_t h_magic;
    uint32_t h_blocktype;
    uint32_t h_sequence;
} journal_header_t;

typedef struct __attribute__((packed)) {
    journal_header_t s_header;
    uint32_t s_blocksize;
    uint32_t s_maxlen;
    uint32_t s_first;
    uint32_t s_sequence;
    uint32_t s_start;
    uint32_t s_errno;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    uint32_t s_nr_users;
    uint32_t s_dynsuper;
    uint32_t s_max_transaction;
    uint32_t s_max_trans_data;
    uint8_t  s_checksum_type;
    uint8_t  s_padding2[3];
    uint32_t s_num_fc_blks;
    uint32_t s_padding[41];
    uint32_t s_checksum;
    uint8_t  s_users[16 * 48];
} journal_superblock_t;

_Static_assert(sizeof(journal_superblock_t) == 1024, "jbd2 superblock size");

int jbd2_recover(ext2_t *fs);

void jbd2_txn_begin(ext2_t *fs);
void jbd2_txn_end(ext2_t *fs);
int  jbd2_txn_stage(ext2_t *fs, uint32_t block, const void *data);
int  jbd2_txn_stage_patch(ext2_t *fs, uint32_t block, uint32_t off, const void *data, uint32_t len);
int  jbd2_txn_read(ext2_t *fs, uint32_t block, void *buf);

#endif

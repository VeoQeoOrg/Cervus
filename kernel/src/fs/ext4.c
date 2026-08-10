#include "../../include/fs/ext4.h"
#include "../../include/memory/pmm.h"
#include "../../include/syscall/errno.h"
#include <stdint.h>

int32_t ext4_extent_lookup(ext2_t *fs, ext2_inode_t *di, uint32_t file_block) {
    uint8_t *heap = NULL;
    const uint8_t *node = (const uint8_t *)di->i_block;
    for (;;) {
        const ext4_extent_header_t *eh = (const ext4_extent_header_t *)node;
        if (eh->eh_magic != EXT4_EXTENT_MAGIC) { if (heap) kfree(heap); return -EINVAL; }
        if (eh->eh_depth == 0) {
            const ext4_extent_t *ex = (const ext4_extent_t *)(node + sizeof(ext4_extent_header_t));
            int32_t ret = 0;
            for (uint16_t i = 0; i < eh->eh_entries; i++) {
                uint32_t start = ex[i].ee_block;
                uint32_t len   = ex[i].ee_len;
                if (len > EXT4_INIT_MAX_LEN) len -= EXT4_INIT_MAX_LEN;
                if (file_block >= start && file_block < start + len) {
                    uint64_t phys = ((uint64_t)ex[i].ee_start_hi << 32) | ex[i].ee_start_lo;
                    ret = (int32_t)(phys + (file_block - start));
                    break;
                }
            }
            if (heap) kfree(heap);
            return ret;
        }
        const ext4_extent_idx_t *ix = (const ext4_extent_idx_t *)(node + sizeof(ext4_extent_header_t));
        uint64_t child = 0;
        for (uint16_t i = 0; i < eh->eh_entries; i++) {
            if (i + 1u == eh->eh_entries || file_block < ix[i + 1].ei_block) {
                child = ((uint64_t)ix[i].ei_leaf_hi << 32) | ix[i].ei_leaf_lo;
                break;
            }
        }
        if (child == 0) { if (heap) kfree(heap); return 0; }
        if (!heap) { heap = kmalloc(fs->block_size); if (!heap) return -ENOMEM; }
        ext2_block_read(fs, (uint32_t)child, heap);
        node = heap;
    }
}

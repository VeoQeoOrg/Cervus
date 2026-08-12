#include "../../include/fs/jbd2.h"
#include "../../include/fs/ext2.h"
#include "../../include/drivers/disk/blkdev.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include "../../include/syscall/errno.h"
#include <string.h>

static inline uint32_t b32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t b16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint64_t b64(uint64_t x) { return __builtin_bswap64(x); }

enum { PASS_SCAN, PASS_REVOKE, PASS_REPLAY };

typedef struct { uint64_t block; uint32_t seq; } revoke_ent_t;
typedef struct { revoke_ent_t *e; uint32_t n, cap; } revoke_tbl_t;

static int tid_geq(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

static void rev_set(revoke_tbl_t *t, uint64_t block, uint32_t seq) {
    for (uint32_t i = 0; i < t->n; i++)
        if (t->e[i].block == block) { if (tid_geq(seq, t->e[i].seq)) t->e[i].seq = seq; return; }
    if (t->n == t->cap) {
        uint32_t nc = t->cap ? t->cap * 2 : 64;
        revoke_ent_t *ne = kmalloc(nc * sizeof(revoke_ent_t));
        if (!ne) return;
        if (t->e) { memcpy(ne, t->e, t->n * sizeof(revoke_ent_t)); kfree(t->e); }
        t->e = ne; t->cap = nc;
    }
    t->e[t->n].block = block; t->e[t->n].seq = seq; t->n++;
}

static int rev_test(revoke_tbl_t *t, uint64_t block, uint32_t seq) {
    for (uint32_t i = 0; i < t->n; i++)
        if (t->e[i].block == block) return tid_geq(t->e[i].seq, seq) ? 1 : 0;
    return 0;
}

static uint32_t wrapb(uint32_t b, uint32_t first, uint32_t maxlen) {
    return (b >= maxlen) ? (b - (maxlen - first)) : b;
}

static int jbd2_read_log(ext2_t *fs, ext2_inode_t *jino, uint32_t jblk, void *buf) {
    int32_t phys = ext2_bmap(fs, jino, jblk);
    if (phys <= 0) return -EIO;
    return ext2_block_read(fs, (uint32_t)phys, buf);
}

static int jbd2_write_log(ext2_t *fs, ext2_inode_t *jino, uint32_t jblk, const void *buf) {
    int32_t phys = ext2_bmap(fs, jino, jblk);
    if (phys <= 0) return -EIO;
    return blkdev_write(fs->dev, (uint64_t)phys * fs->block_size, buf, fs->block_size);
}

static uint32_t tag_flags(const uint8_t *tagp, int csum3) {
    uint32_t v;
    if (csum3) { memcpy(&v, tagp + 4, 4); return b32(v); }
    uint16_t f; memcpy(&f, tagp + 6, 2); return b16(f);
}

static uint64_t tag_block(const uint8_t *tagp, int is64) {
    uint32_t lo, hi = 0;
    memcpy(&lo, tagp + 0, 4);
    if (is64) memcpy(&hi, tagp + 8, 4);
    return ((uint64_t)b32(hi) << 32) | b32(lo);
}

static void scan_revoke(const uint8_t *blk, uint32_t bs, uint32_t seq, int is64, revoke_tbl_t *rev) {
    uint32_t count;
    memcpy(&count, blk + sizeof(journal_header_t), 4);
    count = b32(count);
    if (count > bs) count = bs;
    uint32_t off = sizeof(journal_header_t) + 4;
    uint32_t rsz = is64 ? 8 : 4;
    while (off + rsz <= count) {
        uint64_t block;
        if (is64) { uint64_t v; memcpy(&v, blk + off, 8); block = b64(v); }
        else      { uint32_t v; memcpy(&v, blk + off, 4); block = b32(v); }
        rev_set(rev, block, seq);
        off += rsz;
    }
}

typedef struct {
    ext2_t       *fs;
    ext2_inode_t *jino;
    uint32_t      first, maxlen, tag_bytes;
    int           csum3, is64;
} jctx_t;

static uint32_t jbd2_pass(jctx_t *c, int pass, uint32_t start, uint32_t start_seq,
                          uint32_t end_seq, revoke_tbl_t *rev) {
    uint32_t bs = c->fs->block_size;
    uint8_t *blk  = kmalloc(bs);
    uint8_t *dbuf = (pass == PASS_REPLAY) ? kmalloc(bs) : NULL;
    if (!blk || (pass == PASS_REPLAY && !dbuf)) { if (blk) kfree(blk); if (dbuf) kfree(dbuf); return start_seq; }

    uint32_t next_log = start;
    uint32_t next_seq = start_seq;
    uint32_t applied = 0;

    for (;;) {
        if (pass != PASS_SCAN && tid_geq(next_seq, end_seq)) break;
        if (jbd2_read_log(c->fs, c->jino, next_log, blk) < 0) break;

        journal_header_t h;
        memcpy(&h, blk, sizeof(h));
        if (b32(h.h_magic) != JBD2_MAGIC_NUMBER) break;
        uint32_t btype = b32(h.h_blocktype);
        if (b32(h.h_sequence) != next_seq) break;

        next_log = wrapb(next_log + 1, c->first, c->maxlen);

        if (btype == JBD2_DESCRIPTOR_BLOCK) {
            uint8_t *tagp = blk + sizeof(journal_header_t);
            while (tagp + c->tag_bytes <= blk + bs) {
                uint32_t tf = tag_flags(tagp, c->csum3);
                uint64_t blocknr = tag_block(tagp, c->is64);
                if (pass == PASS_REPLAY) {
                    if (jbd2_read_log(c->fs, c->jino, next_log, dbuf) == 0 &&
                        !rev_test(rev, blocknr, next_seq)) {
                        if (tf & JBD2_FLAG_ESCAPE) {
                            uint32_t mg = b32(JBD2_MAGIC_NUMBER);
                            memcpy(dbuf, &mg, 4);
                        }
                        blkdev_write(c->fs->dev, blocknr * (uint64_t)bs, dbuf, bs);
                        applied++;
                    }
                }
                next_log = wrapb(next_log + 1, c->first, c->maxlen);
                tagp += c->tag_bytes;
                if (!(tf & JBD2_FLAG_SAME_UUID)) tagp += 16;
                if (tf & JBD2_FLAG_LAST_TAG) break;
            }
        } else if (btype == JBD2_COMMIT_BLOCK) {
            next_seq++;
        } else if (btype == JBD2_REVOKE_BLOCK) {
            if (pass == PASS_REVOKE) scan_revoke(blk, bs, next_seq, c->is64, rev);
        } else {
            break;
        }
    }

    if (pass == PASS_REPLAY)
        serial_printf("[jbd2] replay applied %u blocks (seq %u..%u)\n", applied, start_seq, next_seq);
    kfree(blk);
    if (dbuf) kfree(dbuf);
    return next_seq;
}

int jbd2_recover(ext2_t *fs) {
    ext2_inode_t jino;
    if (ext2_inode_read(fs, EXT2_JOURNAL_INO, &jino) < 0) return -EIO;
    uint32_t bs = fs->block_size;
    uint8_t *sbblk = kmalloc(bs);
    if (!sbblk) return -ENOMEM;

    if (jbd2_read_log(fs, &jino, 0, sbblk) < 0) { kfree(sbblk); return -EIO; }
    journal_superblock_t *jsb = (journal_superblock_t *)sbblk;
    if (b32(jsb->s_header.h_magic) != JBD2_MAGIC_NUMBER) { kfree(sbblk); return 0; }

    uint32_t maxlen = b32(jsb->s_maxlen);
    uint32_t first  = b32(jsb->s_first);
    uint32_t seq    = b32(jsb->s_sequence);
    uint32_t start  = b32(jsb->s_start);
    uint32_t feat   = b32(jsb->s_feature_incompat);

    fs->journal_writable = ((feat & ~(uint32_t)JBD2_FEATURE_INCOMPAT_REVOKE) == 0);

    if (start == 0) {
        kfree(sbblk);
        if (fs->sb.s_feature_incompat & EXT3_FEATURE_INCOMPAT_RECOVER) {
            fs->sb.s_feature_incompat &= ~EXT3_FEATURE_INCOMPAT_RECOVER;
            blkdev_write(fs->dev, EXT2_SUPER_OFFSET, &fs->sb, sizeof(fs->sb));
        }
        return 0;
    }

    jctx_t c = { .fs = fs, .jino = &jino, .first = first, .maxlen = maxlen };
    if (feat & JBD2_FEATURE_INCOMPAT_CSUM_V3) c.tag_bytes = 16, c.csum3 = 1;
    else { c.tag_bytes = 8; if (feat & JBD2_FEATURE_INCOMPAT_64BIT) c.tag_bytes += 4; }
    c.is64 = (feat & JBD2_FEATURE_INCOMPAT_64BIT) ? 1 : 0;

    serial_printf("[jbd2] recovering: maxlen=%u first=%u seq=%u start=%u incompat=0x%x\n",
                  maxlen, first, seq, start, feat);

    uint32_t end_seq = jbd2_pass(&c, PASS_SCAN, start, seq, 0, NULL);
    revoke_tbl_t rev = { 0 };
    jbd2_pass(&c, PASS_REVOKE, start, seq, end_seq, &rev);
    jbd2_pass(&c, PASS_REPLAY, start, seq, end_seq, &rev);
    if (rev.e) kfree(rev.e);

    jsb->s_start = 0;
    jsb->s_sequence = b32(end_seq);
    jbd2_write_log(fs, &jino, 0, sbblk);
    kfree(sbblk);

    fs->sb.s_feature_incompat &= ~EXT3_FEATURE_INCOMPAT_RECOVER;
    blkdev_write(fs->dev, EXT2_SUPER_OFFSET, &fs->sb, sizeof(fs->sb));
    if (fs->dev->ops && fs->dev->ops->flush) fs->dev->ops->flush(fs->dev);
    return 1;
}

/* ---- write-side journaling (synchronous, ordered-data) ---- */

typedef struct { uint32_t block; uint8_t *data; } txblk_t;
struct jbd2_txn {
    ext2_t   *fs;
    txblk_t  *b;
    uint32_t  n, cap;
    int       depth;
};

static void put32be(uint8_t *p, uint32_t v) { v = b32(v); memcpy(p, &v, 4); }
static void put16be(uint8_t *p, uint16_t v) { v = b16(v); memcpy(p, &v, 2); }

static txblk_t *txn_find(struct jbd2_txn *t, uint32_t block) {
    for (uint32_t i = 0; i < t->n; i++)
        if (t->b[i].block == block) return &t->b[i];
    return NULL;
}

static txblk_t *txn_get(struct jbd2_txn *t, uint32_t block, int read_existing) {
    txblk_t *e = txn_find(t, block);
    if (e) return e;
    if (t->n == t->cap) {
        uint32_t nc = t->cap ? t->cap * 2 : 16;
        txblk_t *nb = kmalloc(nc * sizeof(txblk_t));
        if (!nb) return NULL;
        if (t->b) { memcpy(nb, t->b, t->n * sizeof(txblk_t)); kfree(t->b); }
        t->b = nb; t->cap = nc;
    }
    uint8_t *d = kmalloc(t->fs->block_size);
    if (!d) return NULL;
    if (read_existing) ext2_block_read(t->fs, block, d);
    else memset(d, 0, t->fs->block_size);
    t->b[t->n].block = block; t->b[t->n].data = d;
    return &t->b[t->n++];
}

static void txn_clear(struct jbd2_txn *t) {
    for (uint32_t i = 0; i < t->n; i++) kfree(t->b[i].data);
    t->n = 0;
}

static uint32_t txn_max_data(ext2_t *fs) {
    uint32_t cap = (fs->block_size - sizeof(journal_header_t) - 16) / 8;
    if (cap > 500) cap = 500;
    return cap - 8;
}

static void txn_stage_meta(struct jbd2_txn *t) {
    ext2_t *fs = t->fs;
    uint32_t bs = fs->block_size;
    ext2_csum_finalize(fs);
    uint32_t sb_block = (bs == 1024) ? 1 : 0;
    uint32_t sb_off   = (bs == 1024) ? 0 : EXT2_SUPER_OFFSET;
    txblk_t *e = txn_get(t, sb_block, 1);
    if (e) memcpy(e->data + sb_off, &fs->sb, sizeof(fs->sb));

    uint32_t gdt_block = (bs == 1024) ? 2 : 1;
    uint32_t gdt_bytes = fs->groups_count * fs->desc_size;
    uint32_t gdt_blocks = (gdt_bytes + bs - 1) / bs;
    for (uint32_t i = 0; i < gdt_blocks; i++) {
        txblk_t *g = txn_get(t, gdt_block + i, 1);
        if (g) memcpy(g->data, (uint8_t *)fs->gdt + (uint64_t)i * bs, bs);
    }
}

static void txn_flush(struct jbd2_txn *t) {
    ext2_t *fs = t->fs;
    uint32_t bs = fs->block_size;

    txn_stage_meta(t);
    if (t->n == 0) return;

    ext2_inode_t jino;
    uint8_t *jsbblk = kmalloc(bs);
    if (ext2_inode_read(fs, EXT2_JOURNAL_INO, &jino) < 0 || !jsbblk ||
        jbd2_read_log(fs, &jino, 0, jsbblk) < 0) {
        for (uint32_t i = 0; i < t->n; i++)
            blkdev_write(fs->dev, (uint64_t)t->b[i].block * bs, t->b[i].data, bs);
        if (jsbblk) kfree(jsbblk);
        txn_clear(t);
        return;
    }
    journal_superblock_t *jsb = (journal_superblock_t *)jsbblk;
    uint32_t maxlen = b32(jsb->s_maxlen);
    uint32_t first  = b32(jsb->s_first);
    uint32_t seq    = b32(jsb->s_sequence);

    if (2 + t->n > maxlen - first) {
        for (uint32_t i = 0; i < t->n; i++)
            blkdev_write(fs->dev, (uint64_t)t->b[i].block * bs, t->b[i].data, bs);
        kfree(jsbblk);
        txn_clear(t);
        return;
    }

    ext2_superblock_t rsb = fs->sb;
    rsb.s_feature_incompat |= EXT3_FEATURE_INCOMPAT_RECOVER;
    blkdev_write(fs->dev, EXT2_SUPER_OFFSET, &rsb, sizeof(rsb));

    uint8_t *desc = kzalloc(bs);
    journal_header_t *dh = (journal_header_t *)desc;
    dh->h_magic = b32(JBD2_MAGIC_NUMBER);
    dh->h_blocktype = b32(JBD2_DESCRIPTOR_BLOCK);
    dh->h_sequence = b32(seq);
    uint8_t *tagp = desc + sizeof(journal_header_t);
    uint8_t *dbuf = kmalloc(bs);

    for (uint32_t i = 0; i < t->n; i++) {
        uint32_t flags = 0;
        if (i > 0) flags |= JBD2_FLAG_SAME_UUID;
        if (i == t->n - 1) flags |= JBD2_FLAG_LAST_TAG;
        memcpy(dbuf, t->b[i].data, bs);
        uint32_t head;
        memcpy(&head, dbuf, 4);
        if (head == b32(JBD2_MAGIC_NUMBER)) { flags |= JBD2_FLAG_ESCAPE; memset(dbuf, 0, 4); }
        put32be(tagp, t->b[i].block);
        put16be(tagp + 4, 0);
        put16be(tagp + 6, (uint16_t)flags);
        tagp += 8;
        if (i == 0) { memcpy(tagp, jsb->s_uuid, 16); tagp += 16; }
        jbd2_write_log(fs, &jino, first + 1 + i, dbuf);
    }
    jbd2_write_log(fs, &jino, first, desc);
    kfree(desc);
    kfree(dbuf);

    uint8_t *cmt = kzalloc(bs);
    journal_header_t *ch = (journal_header_t *)cmt;
    ch->h_magic = b32(JBD2_MAGIC_NUMBER);
    ch->h_blocktype = b32(JBD2_COMMIT_BLOCK);
    ch->h_sequence = b32(seq);
    jbd2_write_log(fs, &jino, first + 1 + t->n, cmt);
    kfree(cmt);
    if (fs->dev->ops && fs->dev->ops->flush) fs->dev->ops->flush(fs->dev);

    jsb->s_start = b32(first);
    jsb->s_sequence = b32(seq);
    jbd2_write_log(fs, &jino, 0, jsbblk);
    if (fs->dev->ops && fs->dev->ops->flush) fs->dev->ops->flush(fs->dev);

    for (uint32_t i = 0; i < t->n; i++)
        blkdev_write(fs->dev, (uint64_t)t->b[i].block * bs, t->b[i].data, bs);
    if (fs->dev->ops && fs->dev->ops->flush) fs->dev->ops->flush(fs->dev);

    jsb->s_start = 0;
    jsb->s_sequence = b32(seq + 1);
    jbd2_write_log(fs, &jino, 0, jsbblk);
    if (fs->dev->ops && fs->dev->ops->flush) fs->dev->ops->flush(fs->dev);

    kfree(jsbblk);
    txn_clear(t);
    fs->dirty = false;
}

void jbd2_txn_begin(ext2_t *fs) {
    if (!(fs->sb.s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL) || !fs->sb.s_journal_inum)
        return;
    if (!fs->journal_writable)
        return;
    if (fs->cur_txn) { ((struct jbd2_txn *)fs->cur_txn)->depth++; return; }
    struct jbd2_txn *t = kzalloc(sizeof(*t));
    if (!t) return;
    t->fs = fs; t->depth = 1;
    fs->cur_txn = t;
}

void jbd2_txn_end(ext2_t *fs) {
    struct jbd2_txn *t = fs->cur_txn;
    if (!t) return;
    if (t->depth > 1) { t->depth--; return; }
    fs->cur_txn = NULL;
    txn_flush(t);
    if (t->b) kfree(t->b);
    kfree(t);
}

int jbd2_txn_read(ext2_t *fs, uint32_t block, void *buf) {
    struct jbd2_txn *t = fs->cur_txn;
    if (!t) return 0;
    txblk_t *e = txn_find(t, block);
    if (!e) return 0;
    memcpy(buf, e->data, fs->block_size);
    return 1;
}

int jbd2_txn_stage(ext2_t *fs, uint32_t block, const void *data) {
    struct jbd2_txn *t = fs->cur_txn;
    if (t->n >= txn_max_data(fs) && !txn_find(t, block)) txn_flush(t);
    txblk_t *e = txn_get(t, block, 0);
    if (!e) return -ENOMEM;
    memcpy(e->data, data, fs->block_size);
    return 0;
}

int jbd2_txn_stage_patch(ext2_t *fs, uint32_t block, uint32_t off, const void *data, uint32_t len) {
    struct jbd2_txn *t = fs->cur_txn;
    if (t->n >= txn_max_data(fs) && !txn_find(t, block)) txn_flush(t);
    txblk_t *e = txn_get(t, block, 1);
    if (!e) return -ENOMEM;
    memcpy(e->data + off, data, len);
    return 0;
}

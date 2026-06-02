#include "../../../../include/drivers/usb/ehci.h"
#include "../../../../include/drivers/disk/blkdev.h"
#include "../../../../include/drivers/disk/partition.h"
#include "../../../../include/fs/vfs.h"
#include "../../../../include/memory/dma.h"
#include "../../../../include/memory/pmm.h"
#include "../../../../include/apic/apic.h"
#include "../../../../include/io/serial.h"
#include "../../../../include/syscall/errno.h"
#include <string.h>
#include <stdio.h>

extern void devfs_register(const char *name, vnode_t *node);

#define EHCI_MAX_MSC          4
#define EHCI_MSC_MAX_SECT     32

#define CBW_SIGNATURE         0x43425355u
#define CSW_SIGNATURE         0x53425355u

typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_length;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cb_length;
    uint8_t  cb[16];
} ehci_cbw_t;
_Static_assert(sizeof(ehci_cbw_t) == 31, "ehci cbw size");

typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t  status;
} ehci_csw_t;
_Static_assert(sizeof(ehci_csw_t) == 13, "ehci csw size");

typedef struct ehci_msc {
    ehci_controller_t *ctl;
    uint8_t   addr;
    uint8_t   speed;
    uint8_t   intf;
    uint8_t   in_ep, out_ep;
    uint16_t  in_mps, out_mps;
    uint32_t  in_dt_bit, out_dt_bit;

    ehci_qh_t *in_qh, *out_qh;
    uintptr_t  in_qh_phys, out_qh_phys;

    uint32_t  tag;
    uint64_t  lba_count;
    uint32_t  block_size;
    char      vendor[9];
    char      product[17];

    blkdev_t  blkdev;
    vnode_t   vnode;
    char      name[16];
    bool      active;
    bool      ready;
    bool      registered;
    int       slot_idx;
} ehci_msc_t;

static ehci_msc_t g_msc_devs[EHCI_MAX_MSC];
static int g_msc_count = 0;
static uint64_t g_msc_ino_base = 0x40000;

static int link_bulk_qh(ehci_msc_t *m, bool in_dir) {
    uintptr_t qh_phys;
    ehci_qh_t *qh = (ehci_qh_t *)dma_alloc_coherent_low(4096, &qh_phys);
    if (!qh || qh_phys >= 0xFFFFFFFFULL) return -ENOMEM;
    memset(qh, 0, sizeof(*qh));

    uint8_t  ep_num = in_dir ? m->in_ep : m->out_ep;
    uint16_t mps    = in_dir ? m->in_mps : m->out_mps;
    uint32_t c_flag = (m->speed == EHCI_SPEED_HS) ? 0u : (1u << 27);

    qh->ep_chars = ((uint32_t)m->addr & 0x7F)
                 | ((uint32_t)ep_num << 8)
                 | ((uint32_t)m->speed << 12)
                 | ((uint32_t)mps << 16)
                 | c_flag
                 | (0u << 28);
    qh->ep_caps  = (1u << 30);
    qh->cur_qtd  = 0;
    qh->overlay_next  = QTD_T;
    qh->overlay_alt_next = QTD_T;
    qh->overlay_token = QTD_STATUS_HALTED;

    uint32_t old_cmd;
    if (ehci_async_pause(m->ctl, &old_cmd) < 0) {
        dma_free_coherent(qh, 4096);
        return -EIO;
    }
    qh->hlp = m->ctl->async_head->hlp;
    asm volatile("" ::: "memory");
    m->ctl->async_head->hlp = ((uint32_t)qh_phys) | QH_TYPE_QH;
    ehci_async_resume(m->ctl, old_cmd);

    if (in_dir) { m->in_qh = qh;  m->in_qh_phys  = qh_phys; }
    else        { m->out_qh = qh; m->out_qh_phys = qh_phys; }
    return 0;
}

static int bulk_xfer(ehci_msc_t *m, bool dir_in,
                     uintptr_t buf_phys, uint32_t len, uint32_t timeout_ms)
{
    ehci_qh_t *qh    = dir_in ? m->in_qh   : m->out_qh;
    uint32_t  *dt_ptr= dir_in ? &m->in_dt_bit : &m->out_dt_bit;
    if (!qh) return -EIO;

    uintptr_t tp;
    ehci_qtd_t *t = alloc_qtd(&tp);
    if (!t) return -ENOMEM;
    t->next     = QTD_T;
    t->alt_next = QTD_T;
    t->token    = QTD_STATUS_ACTIVE | QTD_CERR_3 | QTD_IOC
                | ((uint32_t)len << QTD_TBT_SHIFT)
                | (dir_in ? QTD_PID_IN : QTD_PID_OUT);
    qtd_set_buf(t, buf_phys, len);

    uint32_t old_cmd;
    if (ehci_async_pause(m->ctl, &old_cmd) < 0) {
        dma_free_coherent(t, 4096);
        return -EIO;
    }
    qh->cur_qtd          = 0;
    qh->overlay_next     = (uint32_t)tp;
    qh->overlay_alt_next = QTD_T;
    qh->overlay_token    = *dt_ptr;
    qh->overlay_buf[0]   = 0;
    qh->overlay_buf[1]   = 0;
    qh->overlay_buf[2]   = 0;
    qh->overlay_buf[3]   = 0;
    qh->overlay_buf[4]   = 0;
    asm volatile("" ::: "memory");
    ehci_async_resume(m->ctl, old_cmd);

    uint64_t deadline = hpet_elapsed_ns() + (uint64_t)timeout_ms * 1000000ULL;
    int r = -ETIMEDOUT;
    for (;;) {
        uint32_t st = t->token & 0xFF;
        if (!(st & QTD_STATUS_ACTIVE)) {
            if (st & (QTD_STATUS_HALTED | QTD_STATUS_DBE | QTD_STATUS_BABBLE |
                      QTD_STATUS_XACTERR | QTD_STATUS_MISSED)) {
                serial_printf("[ehci-msc] bulk %s ERR token=0x%08x len=%u qh_tok=0x%08x\n",
                       dir_in ? "IN" : "OUT", t->token, len, qh->overlay_token);
                r = -EIO;
            } else {
                r = 0;
            }
            break;
        }
        if (hpet_elapsed_ns() > deadline) {
            serial_printf("[ehci-msc] bulk %s TIMEOUT len=%u token=0x%08x qh_tok=0x%08x usbsts=0x%x\n",
                   dir_in ? "IN" : "OUT", len, t->token, qh->overlay_token,
                   op_r32(m->ctl, EHCI_OP_USBSTS));
            break;
        }
        asm volatile("pause");
    }

    *dt_ptr = qh->overlay_token & QTD_DT;

    {
        uint32_t old_cmd2;
        if (ehci_async_pause(m->ctl, &old_cmd2) == 0) {
            qh->cur_qtd          = 0;
            qh->overlay_next     = QTD_T;
            qh->overlay_alt_next = QTD_T;
            qh->overlay_token    = (*dt_ptr) | QTD_STATUS_HALTED;
            asm volatile("" ::: "memory");
            ehci_async_resume(m->ctl, old_cmd2);
        }
    }

    dma_free_coherent(t, 4096);
    return r;
}

static void msc_reset_recovery(ehci_msc_t *m) {
    uint8_t clr_in[8]  = { 0x02, 0x01, 0x00, 0x00,
                           (uint8_t)(m->in_ep | 0x80), 0x00, 0x00, 0x00 };
    (void)ehci_control_xfer(m->ctl, m->addr, m->speed, 64, clr_in, NULL, 0, false);

    uint8_t clr_out[8] = { 0x02, 0x01, 0x00, 0x00,
                           m->out_ep, 0x00, 0x00, 0x00 };
    (void)ehci_control_xfer(m->ctl, m->addr, m->speed, 64, clr_out, NULL, 0, false);

    m->in_dt_bit  = 0;
    m->out_dt_bit = 0;
    hpet_sleep_ms(20);
    serial_printf("[ehci-msc] clear-halt recovery done (addr=%u)\n", m->addr);
}

static int msc_scsi(ehci_msc_t *m, const uint8_t *cdb, uint8_t cdb_len,
                    bool data_in, uintptr_t data_phys, uint32_t data_len,
                    uint8_t *scsi_status)
{
    if (cdb_len == 0 || cdb_len > 16) return -EINVAL;

    uintptr_t cbw_phys, csw_phys;
    ehci_cbw_t *cbw = (ehci_cbw_t *)dma_alloc_coherent_low(64, &cbw_phys);
    if (!cbw || cbw_phys >= 0xFFFFFFFFULL) return -ENOMEM;
    ehci_csw_t *csw = (ehci_csw_t *)dma_alloc_coherent_low(64, &csw_phys);
    if (!csw || csw_phys >= 0xFFFFFFFFULL) { dma_free_coherent(cbw, 64); return -ENOMEM; }

    memset(cbw, 0, sizeof(*cbw));
    memset(csw, 0, sizeof(*csw));

    uint32_t tag = ++m->tag;
    cbw->signature   = CBW_SIGNATURE;
    cbw->tag         = tag;
    cbw->data_length = data_len;
    cbw->flags       = data_in ? 0x80 : 0x00;
    cbw->lun         = 0;
    cbw->cb_length   = cdb_len;
    memcpy(cbw->cb, cdb, cdb_len);

    int r = bulk_xfer(m, false, cbw_phys, sizeof(*cbw), 5000);
    if (r < 0) goto out;

    if (data_len > 0 && data_phys != 0) {
        r = bulk_xfer(m, data_in, data_phys, data_len, 60000);
        if (r < 0) goto out;
    }

    r = bulk_xfer(m, true, csw_phys, sizeof(*csw), 60000);
    if (r < 0) goto out;

    if (csw->signature != CSW_SIGNATURE || csw->tag != tag) {
        serial_printf("[ehci-msc] bad CSW sig=0x%x tag=%u (expected %u)\n",
                      csw->signature, csw->tag, tag);
        r = -EIO;
        goto out;
    }
    if (scsi_status) *scsi_status = csw->status;
    r = 0;
out:
    if (r == -EIO) msc_reset_recovery(m);
    dma_free_coherent(cbw, 64);
    dma_free_coherent(csw, 64);
    return r;
}

static int msc_inquiry(ehci_msc_t *m) {
    uintptr_t bp;
    uint8_t *buf = (uint8_t *)dma_alloc_coherent_low(64, &bp);
    if (!buf || bp >= 0xFFFFFFFFULL) return -ENOMEM;
    memset(buf, 0, 64);
    uint8_t cdb[6] = { 0x12, 0, 0, 0, 36, 0 };
    uint8_t st = 0xFF;
    int r = msc_scsi(m, cdb, 6, true, bp, 36, &st);
    if (r == 0 && st == 0) {
        memcpy(m->vendor,  buf + 8,  8);  m->vendor[8]  = 0;
        memcpy(m->product, buf + 16, 16); m->product[16] = 0;
        for (int i = 7;  i >= 0 && m->vendor[i]  == ' '; i--) m->vendor[i]  = 0;
        for (int i = 15; i >= 0 && m->product[i] == ' '; i--) m->product[i] = 0;
    } else {
        r = (r < 0) ? r : -EIO;
    }
    dma_free_coherent(buf, 64);
    return r;
}

static int msc_test_unit_ready(ehci_msc_t *m) {
    uint8_t cdb[6] = { 0x00, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 5; i++) {
        uint8_t st = 0xFF;
        int r = msc_scsi(m, cdb, 6, true, 0, 0, &st);
        if (r == 0 && st == 0) return 0;
        if (r == 0 && st != 0) {
            uint8_t rs_cdb[6] = { 0x03, 0, 0, 0, 18, 0 };
            uintptr_t rsp;
            uint8_t *rs = (uint8_t *)dma_alloc_coherent_low(64, &rsp);
            if (rs && rsp < 0xFFFFFFFFULL) {
                memset(rs, 0, 64);
                uint8_t rss = 0;
                msc_scsi(m, rs_cdb, 6, true, rsp, 18, &rss);
                dma_free_coherent(rs, 64);
            }
        }
        hpet_sleep_ms(100);
    }
    return -EIO;
}

static int msc_read_capacity(ehci_msc_t *m) {
    uintptr_t bp;
    uint8_t *buf = (uint8_t *)dma_alloc_coherent_low(64, &bp);
    if (!buf || bp >= 0xFFFFFFFFULL) return -ENOMEM;
    memset(buf, 0, 64);
    uint8_t cdb[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t st = 0xFF;
    int r = msc_scsi(m, cdb, 10, true, bp, 8, &st);
    if (r == 0 && st == 0) {
        uint32_t last_lba = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                            ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
        uint32_t bs       = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                            ((uint32_t)buf[6] << 8)  |  (uint32_t)buf[7];
        m->lba_count  = (uint64_t)last_lba + 1;
        m->block_size = bs ? bs : 512;
    } else {
        r = (r < 0) ? r : -EIO;
    }
    dma_free_coherent(buf, 64);
    return r;
}

static int msc_rw10(ehci_msc_t *m, uint64_t lba, uint32_t count,
                    void *buf, bool is_write)
{
    if (!m->ready) return -EIO;
    if (count == 0) return 0;
    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > EHCI_MSC_MAX_SECT) chunk = EHCI_MSC_MAX_SECT;
        uint32_t bytes = chunk * m->block_size;

        uintptr_t dph;
        uint8_t *dbuf = (uint8_t *)dma_alloc_coherent_low(bytes, &dph);
        if (!dbuf) return -ENOMEM;
        if (dph >= 0xFFFFFFFFULL) { dma_free_coherent(dbuf, bytes); return -ENOMEM; }

        if (is_write) memcpy(dbuf, (uint8_t *)buf + (size_t)done * m->block_size, bytes);

        uint64_t lba_cur = lba + done;
        uint8_t cdb[10] = {
            (uint8_t)(is_write ? 0x2A : 0x28), 0,
            (uint8_t)(lba_cur >> 24), (uint8_t)(lba_cur >> 16),
            (uint8_t)(lba_cur >> 8),  (uint8_t)lba_cur,
            0,
            (uint8_t)(chunk >> 8), (uint8_t)chunk,
            0
        };
        uint8_t st = 0xFF;
        int r = 0;
        for (int attempt = 0; attempt < 3; attempt++) {
            st = 0xFF;
            r = msc_scsi(m, cdb, 10, !is_write, dph, bytes, &st);
            if (r == 0 && st == 0) break;
            if (r < 0) break;
            hpet_sleep_ms(10);
        }
        if (r < 0 || st != 0) {
            serial_printf("[ehci-msc] %s LBA %llu failed after retries (r=%d st=%u)\n",
                          is_write ? "WRITE" : "READ",
                          (unsigned long long)lba_cur, r, st);
            dma_free_coherent(dbuf, bytes);
            return (r < 0) ? r : -EIO;
        }
        if (!is_write) memcpy((uint8_t *)buf + (size_t)done * m->block_size, dbuf, bytes);
        dma_free_coherent(dbuf, bytes);
        done += chunk;
    }
    return 0;
}

static int msc_sync_cache(ehci_msc_t *m) {
    if (!m || !m->ready) return 0;
    uint8_t cdb[10] = { 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t st = 0xFF;
    int r = msc_scsi(m, cdb, 10, true, 0, 0, &st);
    if (r < 0) return r;
    return (st == 0) ? 0 : -EIO;
}

static int msc_blk_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    return msc_rw10((ehci_msc_t *)dev->priv, lba, count, buf, false);
}
static int msc_blk_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    return msc_rw10((ehci_msc_t *)dev->priv, lba, count, (void *)buf, true);
}
static int msc_blk_flush(blkdev_t *dev) {
    return msc_sync_cache((ehci_msc_t *)dev->priv);
}
static const blkdev_ops_t g_ehci_msc_blkdev_ops = {
    .read_sectors  = msc_blk_read,
    .write_sectors = msc_blk_write,
    .flush         = msc_blk_flush,
};

static int64_t msc_vn_read(vnode_t *n, void *b, size_t l, uint64_t o) {
    blkdev_t *d = (blkdev_t *)n->fs_data;
    if (!d) return -EIO;
    int r = blkdev_read(d, o, b, l);
    return (r < 0) ? r : (int64_t)l;
}
static int64_t msc_vn_write(vnode_t *n, const void *b, size_t l, uint64_t o) {
    blkdev_t *d = (blkdev_t *)n->fs_data;
    if (!d) return -EIO;
    int r = blkdev_write(d, o, b, l);
    return (r < 0) ? r : (int64_t)l;
}
static int msc_vn_stat(vnode_t *n, vfs_stat_t *out) {
    blkdev_t *d = (blkdev_t *)n->fs_data;
    memset(out, 0, sizeof(*out));
    out->st_ino  = n->ino;
    out->st_type = VFS_NODE_BLKDEV;
    out->st_mode = 0660;
    out->st_size = d ? d->size_bytes : 0;
    return 0;
}
static void msc_vn_noop(vnode_t *n) { (void)n; }
static const vnode_ops_t g_ehci_msc_vn_ops = {
    .read  = msc_vn_read,
    .write = msc_vn_write,
    .stat  = msc_vn_stat,
    .ref   = msc_vn_noop,
    .unref = msc_vn_noop,
};

int ehci_msc_setup(ehci_controller_t *c, uint8_t addr, uint8_t speed,
                   const ehci_msc_info_t *info)
{
    ehci_msc_t *m = NULL;
    int idx = -1;
    for (int i = 0; i < g_msc_count; i++) {
        if (!g_msc_devs[i].active) { m = &g_msc_devs[i]; idx = i; break; }
    }
    if (!m) {
        if (g_msc_count >= EHCI_MAX_MSC) return -ENOMEM;
        idx = g_msc_count++;
        m = &g_msc_devs[idx];
    }
    bool was_registered = m->registered;
    memset(m, 0, sizeof(*m));
    m->ctl      = c;
    m->addr     = addr;
    m->speed    = speed;
    m->intf     = info->intf;
    m->in_ep    = info->in_ep;
    m->out_ep   = info->out_ep;
    m->in_mps   = info->in_mps;
    m->out_mps  = info->out_mps;
    m->slot_idx = idx;

    if (link_bulk_qh(m, true)  < 0) return -ENOMEM;
    if (link_bulk_qh(m, false) < 0) return -ENOMEM;

    m->active = true;

    if (msc_inquiry(m) < 0) {
        serial_printf("[ehci-msc] INQUIRY failed for addr=%u\n", addr);
        return -EIO;
    }
    if (msc_test_unit_ready(m) < 0) {
        serial_printf("[ehci-msc] TEST UNIT READY failed for addr=%u\n", addr);
        return -EIO;
    }
    if (msc_read_capacity(m) < 0) {
        serial_printf("[ehci-msc] READ CAPACITY failed for addr=%u\n", addr);
        return -EIO;
    }

    snprintf(m->name, sizeof(m->name), "uhd%d", idx);

    blkdev_t *bd = &m->blkdev;
    memset(bd, 0, sizeof(*bd));
    strncpy(bd->name, m->name, BLKDEV_NAME_MAX - 1);
    snprintf(bd->model, BLKDEV_MODEL_MAX, "%s %s",
             m->vendor[0]  ? m->vendor  : "USB",
             m->product[0] ? m->product : "Mass Storage");
    bd->present      = true;
    bd->is_partition = false;
    bd->sector_count = m->lba_count;
    bd->sector_size  = m->block_size;
    bd->size_bytes   = m->lba_count * (uint64_t)m->block_size;
    bd->ops          = &g_ehci_msc_blkdev_ops;
    bd->priv         = m;

    vnode_t *vn = &m->vnode;
    memset(vn, 0, sizeof(*vn));
    vn->type     = VFS_NODE_BLKDEV;
    vn->mode     = 0660;
    vn->ino      = g_msc_ino_base + (uint64_t)idx;
    vn->ops      = &g_ehci_msc_vn_ops;
    vn->fs_data  = bd;
    vn->size     = bd->size_bytes;
    vn->refcount = 1;

    if (!was_registered) {
        blkdev_register(bd);
        devfs_register(m->name, vn);
    }
    m->registered = true;
    m->ready = true;
    serial_printf("[ehci-msc] /dev/%s: %s %s — %llu sectors x %u (%llu MB)\n",
                  m->name, m->vendor, m->product,
                  (unsigned long long)m->lba_count, m->block_size,
                  (unsigned long long)(bd->size_bytes / (1024 * 1024)));

    partition_scan(bd);
    return 0;
}

void ehci_msc_deactivate_addr(uint8_t addr) {
    for (int j = 0; j < g_msc_count; j++) {
        if (g_msc_devs[j].active && g_msc_devs[j].addr == addr) {
            g_msc_devs[j].active = false;
            g_msc_devs[j].ready  = false;
            g_msc_devs[j].blkdev.present = false;
            partition_remove_children(&g_msc_devs[j].blkdev);
            serial_printf("[ehci-hp] /dev/%s removed\n", g_msc_devs[j].name);
        }
    }
}

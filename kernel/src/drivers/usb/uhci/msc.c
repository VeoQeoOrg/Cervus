#include "../../../../include/drivers/usb/uhci.h"
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

#define UHCI_MAX_MSC          4
#define UHCI_MSC_MAX_SECT     8
#define UHCI_MSC_TIMEOUT_MS   30000

#define CBW_SIGNATURE 0x43425355u
#define CSW_SIGNATURE 0x53425355u

typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_length;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cb_length;
    uint8_t  cb[16];
} uhci_cbw_t;
_Static_assert(sizeof(uhci_cbw_t) == 31, "cbw size");

typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t  status;
} uhci_csw_t;
_Static_assert(sizeof(uhci_csw_t) == 13, "csw size");

typedef struct uhci_msc {
    uhci_controller_t *ctl;
    uint8_t  addr;
    uint8_t  intf;
    uint8_t  in_ep, out_ep;
    uint16_t in_mps, out_mps;
    uint8_t  low_speed;
    uint32_t in_dt, out_dt;
    uint32_t tag;

    uhci_qh_t *in_qh;
    uintptr_t  in_qh_phys;
    uhci_qh_t *out_qh;
    uintptr_t  out_qh_phys;

    uint64_t lba_count;
    uint32_t block_size;
    char     vendor[9];
    char     product[17];

    blkdev_t blkdev;
    vnode_t  vnode;
    char     name[16];
    bool     active;
    bool     ready;
    bool     registered;
    int      slot_idx;
} uhci_msc_t;

static uhci_msc_t g_msc_devs[UHCI_MAX_MSC];
static int        g_msc_count = 0;
static uint64_t   g_msc_ino_base = 0x50000;

static int msc_link_bulk_qh(uhci_msc_t *m, bool in_dir) {
    uintptr_t qh_phys;
    uhci_qh_t *qh = (uhci_qh_t *)dma_alloc_coherent_low(4096, &qh_phys);
    if (!qh || qh_phys >= 0xFFFFFFFFULL) return -ENOMEM;
    memset(qh, 0, sizeof(*qh));
    qh->qhlp = m->ctl->ctrl_qh->qhlp;
    qh->qelp = LP_T;
    asm volatile("" ::: "memory");
    m->ctl->ctrl_qh->qhlp = ((uint32_t)qh_phys) | LP_Q;
    asm volatile("" ::: "memory");
    if (in_dir) { m->in_qh  = qh; m->in_qh_phys  = qh_phys; }
    else        { m->out_qh = qh; m->out_qh_phys = qh_phys; }
    return 0;
}

static int msc_bulk_xfer(uhci_msc_t *m, bool in_dir, void *data, uint16_t len,
                         uint32_t timeout_ms)
{
    uhci_qh_t *qh   = in_dir ? m->in_qh   : m->out_qh;
    uint32_t  *dt   = in_dir ? &m->in_dt  : &m->out_dt;
    uint8_t    ep   = in_dir ? m->in_ep   : m->out_ep;
    uint16_t   mps  = in_dir ? m->in_mps  : m->out_mps;
    uint8_t    pid  = in_dir ? UHCI_PID_IN     : UHCI_PID_OUT;
    uint32_t   ls   = m->low_speed ? TD_LS_DEV : 0;
    if (!qh || mps == 0) return -EIO;

    uintptr_t buf_phys = 0;
    void *buf = NULL;
    if (len > 0) {
        buf = dma_alloc_coherent_low(len < 64 ? 64 : len, &buf_phys);
        if (!buf || buf_phys >= 0xFFFFFFFFULL) return -ENOMEM;
        if (!in_dir && data) memcpy(buf, data, len);
        else memset(buf, 0, len);
    }

    int n_tds = (len == 0) ? 1 : ((len + mps - 1) / mps);
    if (n_tds > 16) {
        if (buf) dma_free_coherent(buf, len < 64 ? 64 : len);
        return -EINVAL;
    }
    uhci_td_t *tds[16] = {0};
    uintptr_t  tdp[16] = {0};
    for (int i = 0; i < n_tds; i++) {
        tds[i] = uhci_alloc_td(&tdp[i]);
        if (!tds[i]) {
            for (int j = 0; j < i; j++) uhci_free_td(tds[j]);
            if (buf) dma_free_coherent(buf, len < 64 ? 64 : len);
            return -ENOMEM;
        }
    }

    uintptr_t cur = buf_phys;
    uint16_t rem = len;
    uint32_t cur_dt = *dt;
    for (int i = 0; i < n_tds; i++) {
        uint16_t chunk = (rem > mps) ? mps : rem;
        bool last = (i == n_tds - 1);
        tds[i]->link   = last ? LP_T : (tdp[i + 1] | LP_VF);
        tds[i]->status = TD_STATUS_ACTIVE | TD_CERR(3) | TD_SPD | ls
                       | (last ? TD_IOC : 0);
        if (len == 0)
            tds[i]->token = TOKEN_NODATA(cur_dt, ep, m->addr, pid);
        else
            tds[i]->token = TOKEN(chunk, cur_dt, ep, m->addr, pid);
        tds[i]->buffer = (uint32_t)cur;
        cur_dt ^= 1;
        cur    += chunk;
        rem    -= chunk;
    }

    asm volatile("" ::: "memory");
    qh->qelp = (uint32_t)tdp[0];
    asm volatile("" ::: "memory");

    uint64_t deadline = hpet_elapsed_ns() + (uint64_t)timeout_ms * 1000000ULL;
    int r = -ETIMEDOUT;
    uhci_td_t *last_td = tds[n_tds - 1];
    while (1) {
        uint32_t st = last_td->status;
        if (!(st & TD_STATUS_ACTIVE)) {
            r = (st & (TD_STATUS_STALLED | TD_STATUS_BABBLE | TD_STATUS_DBE |
                       TD_STATUS_CRC | TD_STATUS_BITSTUF)) ? -EIO : 0;
            break;
        }
        if (hpet_elapsed_ns() > deadline) break;
        asm volatile("pause");
    }

    qh->qelp = LP_T;
    asm volatile("" ::: "memory");

    uint16_t got = 0;
    for (int i = 0; i < n_tds; i++) {
        uint32_t st = tds[i]->status;
        if (st & TD_STATUS_ACTIVE) break;
        uint32_t act = TD_ACT_LEN(st);
        if (act != TD_ACT_LEN_NULL) got += (uint16_t)(act + 1);
        else if (st & (TD_STATUS_STALLED | TD_STATUS_BABBLE | TD_STATUS_DBE |
                       TD_STATUS_CRC | TD_STATUS_BITSTUF)) { r = -EIO; break; }
    }
    *dt = cur_dt;

    if (r == 0 && in_dir && len > 0 && got > 0) {
        if (got > len) got = len;
        memcpy(data, buf, got);
    }

    for (int i = 0; i < n_tds; i++) uhci_free_td(tds[i]);
    if (buf) dma_free_coherent(buf, len < 64 ? 64 : len);
    return r;
}

static void msc_clear_halt(uhci_msc_t *m, bool in_dir) {
    uint8_t ep = in_dir ? (m->in_ep | 0x80) : m->out_ep;
    uint8_t setup[8] = { 0x02, 0x01, 0x00, 0x00, ep, 0x00, 0x00, 0x00 };
    (void)uhci_control_xfer(m->ctl, m->addr, m->low_speed,
                            m->low_speed ? 8 : 64, setup, NULL, 0, false);
    if (in_dir) m->in_dt = 0; else m->out_dt = 0;
    hpet_sleep_ms(10);
}

static int msc_scsi(uhci_msc_t *m, const uint8_t *cdb, uint8_t cdb_len,
                    bool data_in, void *data, uint32_t data_len,
                    uint8_t *scsi_status)
{
    if (cdb_len == 0 || cdb_len > 16) return -EINVAL;

    uhci_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    uint32_t tag = ++m->tag;
    cbw.signature   = CBW_SIGNATURE;
    cbw.tag         = tag;
    cbw.data_length = data_len;
    cbw.flags       = data_in ? 0x80 : 0x00;
    cbw.lun         = 0;
    cbw.cb_length   = cdb_len;
    memcpy(cbw.cb, cdb, cdb_len);

    int r = msc_bulk_xfer(m, false, &cbw, sizeof(cbw), 5000);
    if (r < 0) { msc_clear_halt(m, false); return r; }

    if (data_len > 0 && data) {
        r = msc_bulk_xfer(m, data_in, data, (uint16_t)data_len, UHCI_MSC_TIMEOUT_MS);
        if (r < 0) {
            msc_clear_halt(m, data_in);
            return r;
        }
    }

    uhci_csw_t csw;
    memset(&csw, 0, sizeof(csw));
    r = msc_bulk_xfer(m, true, &csw, sizeof(csw), UHCI_MSC_TIMEOUT_MS);
    if (r < 0) { msc_clear_halt(m, true); return r; }
    if (csw.signature != CSW_SIGNATURE || csw.tag != tag) return -EIO;
    if (scsi_status) *scsi_status = csw.status;
    return 0;
}

static int msc_inquiry(uhci_msc_t *m) {
    uint8_t cdb[6] = { 0x12, 0x00, 0x00, 0x00, 36, 0x00 };
    uint8_t buf[36] = {0};
    uint8_t st = 0xFF;
    int r = msc_scsi(m, cdb, 6, true, buf, 36, &st);
    if (r < 0 || st != 0) return r ? r : -EIO;
    memcpy(m->vendor, buf + 8, 8);  m->vendor[8]  = 0;
    memcpy(m->product, buf + 16, 16); m->product[16] = 0;
    for (int i = 7; i >= 0 && m->vendor[i]  == ' '; i--) m->vendor[i]  = 0;
    for (int i = 15; i >= 0 && m->product[i] == ' '; i--) m->product[i] = 0;
    return 0;
}

static int msc_test_unit_ready(uhci_msc_t *m) {
    uint8_t cdb[6] = { 0x00, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 5; i++) {
        uint8_t st = 0xFF;
        int r = msc_scsi(m, cdb, 6, true, 0, 0, &st);
        if (r == 0 && st == 0) return 0;
        if (st == 2) {
            uint8_t rs_cdb[6] = { 0x03, 0, 0, 0, 18, 0 };
            uint8_t rsp[18] = {0};
            uint8_t rss = 0xFF;
            msc_scsi(m, rs_cdb, 6, true, rsp, 18, &rss);
        }
        hpet_sleep_ms(100);
    }
    return -EIO;
}

static int msc_read_capacity(uhci_msc_t *m) {
    uint8_t cdb[10] = { 0x25, 0,0,0,0,0,0,0,0,0 };
    uint8_t buf[8] = {0};
    uint8_t st = 0xFF;
    int r = msc_scsi(m, cdb, 10, true, buf, 8, &st);
    if (r < 0 || st != 0) return r ? r : -EIO;
    uint32_t last_lba = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                        ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];
    uint32_t blksz    = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                        ((uint32_t)buf[6] << 8)  | (uint32_t)buf[7];
    m->lba_count  = (uint64_t)last_lba + 1;
    m->block_size = blksz ? blksz : 512;
    return 0;
}

static int msc_rw10(uhci_msc_t *m, uint64_t lba, uint32_t count,
                    void *buf, bool is_write)
{
    if (!m->ready || m->block_size == 0) return -EIO;
    uint8_t *p = (uint8_t *)buf;
    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > UHCI_MSC_MAX_SECT) chunk = UHCI_MSC_MAX_SECT;
        uint32_t bytes = chunk * m->block_size;
        uint64_t lba_cur = lba + done;
        uint8_t cdb[10] = {
            is_write ? 0x2A : 0x28, 0,
            (uint8_t)(lba_cur >> 24), (uint8_t)(lba_cur >> 16),
            (uint8_t)(lba_cur >> 8),  (uint8_t)lba_cur,
            0,
            (uint8_t)(chunk >> 8), (uint8_t)chunk, 0
        };
        uint8_t st = 0xFF;
        int r = -1;
        for (int attempt = 0; attempt < 3; attempt++) {
            st = 0xFF;
            r = msc_scsi(m, cdb, 10, !is_write, p, bytes, &st);
            if (r == 0 && st == 0) break;
            if (r < 0) break;
            hpet_sleep_ms(10);
        }
        if (r < 0 || st != 0) return (r < 0) ? r : -EIO;
        p    += bytes;
        done += chunk;
    }
    return 0;
}

static int msc_sync_cache(uhci_msc_t *m) {
    uint8_t cdb[10] = { 0x35, 0,0,0,0,0,0,0,0,0 };
    uint8_t st = 0xFF;
    int r = msc_scsi(m, cdb, 10, true, 0, 0, &st);
    if (r < 0) return r;
    return st == 0 ? 0 : -EIO;
}

static int msc_blk_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    return msc_rw10((uhci_msc_t *)dev->priv, lba, count, buf, false);
}
static int msc_blk_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    return msc_rw10((uhci_msc_t *)dev->priv, lba, count, (void *)buf, true);
}
static int msc_blk_flush(blkdev_t *dev) {
    return msc_sync_cache((uhci_msc_t *)dev->priv);
}

static const blkdev_ops_t g_uhci_msc_blkdev_ops = {
    .read_sectors  = msc_blk_read,
    .write_sectors = msc_blk_write,
    .flush         = msc_blk_flush,
};

static int64_t msc_vn_read(vnode_t *n, void *b, size_t l, uint64_t o) {
    blkdev_t *bd = (blkdev_t *)n->fs_data;
    if (!bd) return -EIO;
    int r = blkdev_read(bd, o, b, l);
    return r < 0 ? (int64_t)r : (int64_t)l;
}
static int64_t msc_vn_write(vnode_t *n, const void *b, size_t l, uint64_t o) {
    blkdev_t *bd = (blkdev_t *)n->fs_data;
    if (!bd) return -EIO;
    int r = blkdev_write(bd, o, b, l);
    return r < 0 ? (int64_t)r : (int64_t)l;
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
static const vnode_ops_t g_uhci_msc_vn_ops = {
    .read  = msc_vn_read,
    .write = msc_vn_write,
    .stat  = msc_vn_stat,
    .ref   = msc_vn_noop,
    .unref = msc_vn_noop,
};

int uhci_msc_setup(uhci_controller_t *c, uint8_t addr, bool low_speed,
                   const uhci_msc_info_t *info)
{
    uhci_msc_t *m = NULL;
    int idx = -1;
    for (int i = 0; i < g_msc_count; i++) {
        if (!g_msc_devs[i].active) { m = &g_msc_devs[i]; idx = i; break; }
    }
    if (!m) {
        if (g_msc_count >= UHCI_MAX_MSC) return -ENOMEM;
        idx = g_msc_count++;
        m = &g_msc_devs[idx];
    }
    bool was_registered = m->registered;
    memset(m, 0, sizeof(*m));
    m->ctl       = c;
    m->addr      = addr;
    m->low_speed = low_speed ? 1 : 0;
    m->intf      = info->intf;
    m->in_ep     = info->in_ep;
    m->out_ep    = info->out_ep;
    m->in_mps    = info->in_mps ? info->in_mps : (low_speed ? 8 : 64);
    m->out_mps   = info->out_mps ? info->out_mps : (low_speed ? 8 : 64);
    m->slot_idx  = idx;

    if (msc_link_bulk_qh(m, true)  < 0) return -ENOMEM;
    if (msc_link_bulk_qh(m, false) < 0) return -ENOMEM;

    m->active = true;

    if (msc_inquiry(m) < 0) {
        serial_printf("[uhci-msc] INQUIRY failed addr=%u\n", addr);
        return -EIO;
    }
    if (msc_test_unit_ready(m) < 0) {
        serial_printf("[uhci-msc] TEST_UNIT_READY failed addr=%u\n", addr);
        return -EIO;
    }
    if (msc_read_capacity(m) < 0) {
        serial_printf("[uhci-msc] READ_CAPACITY failed addr=%u\n", addr);
        return -EIO;
    }

    snprintf(m->name, sizeof(m->name), "uhd%d", 100 + idx);

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
    bd->ops          = &g_uhci_msc_blkdev_ops;
    bd->priv         = m;

    vnode_t *vn = &m->vnode;
    memset(vn, 0, sizeof(*vn));
    vn->type     = VFS_NODE_BLKDEV;
    vn->mode     = 0660;
    vn->ino      = g_msc_ino_base + (uint64_t)idx;
    vn->ops      = &g_uhci_msc_vn_ops;
    vn->fs_data  = bd;
    vn->size     = bd->size_bytes;
    vn->refcount = 1;

    if (!was_registered) {
        blkdev_register(bd);
        devfs_register(m->name, vn);
    }
    m->registered = true;
    m->ready = true;
    serial_printf("[uhci-msc] /dev/%s: %s %s — %llu sectors x %u (%llu MB)\n",
                  m->name, m->vendor, m->product,
                  (unsigned long long)m->lba_count, m->block_size,
                  (unsigned long long)(bd->size_bytes / (1024 * 1024)));

    partition_scan(bd);
    return 0;
}

void uhci_msc_deactivate_addr(uint8_t addr) {
    for (int j = 0; j < g_msc_count; j++) {
        if (g_msc_devs[j].active && g_msc_devs[j].addr == addr) {
            g_msc_devs[j].active = false;
            g_msc_devs[j].ready  = false;
            g_msc_devs[j].blkdev.present = false;
            partition_remove_children(&g_msc_devs[j].blkdev);
            serial_printf("[uhci-hp] /dev/%s removed\n", g_msc_devs[j].name);
        }
    }
}

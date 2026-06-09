#include "../../../../include/drivers/usb/ehci.h"
#include "../../../../include/drivers/usb/usb_msc.h"
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

typedef struct ehci_msc {
    usb_msc_dev_t mscdev;
    ehci_controller_t *ctl;
    uint8_t   addr;
    uint8_t   speed;
    uint8_t   intf;
    uint8_t   in_ep, out_ep;
    uint16_t  in_mps, out_mps;
    uint32_t  in_dt_bit, out_dt_bit;

    ehci_qh_t *in_qh, *out_qh;
    uintptr_t  in_qh_phys, out_qh_phys;

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

static int ehci_msc_bulk(void *dev, bool dir_in, void *virt, uintptr_t buf_phys,
                         uint32_t len, uint32_t timeout_ms)
{
    (void)virt;
    ehci_msc_t *m = (ehci_msc_t *)dev;
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

static void ehci_msc_clear_halt(void *dev, bool dir_in) {
    (void)dir_in;
    ehci_msc_t *m = (ehci_msc_t *)dev;
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

static int msc_blk_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    ehci_msc_t *m = (ehci_msc_t *)dev->priv;
    if (!m->ready) return -EIO;
    return usb_msc_rw10(&m->mscdev, lba, count, buf, false);
}
static int msc_blk_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    ehci_msc_t *m = (ehci_msc_t *)dev->priv;
    if (!m->ready) return -EIO;
    return usb_msc_rw10(&m->mscdev, lba, count, (void *)buf, true);
}
static int msc_blk_flush(blkdev_t *dev) {
    ehci_msc_t *m = (ehci_msc_t *)dev->priv;
    if (!m->ready) return 0;
    return usb_msc_sync_cache(&m->mscdev);
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

    m->mscdev.ops.dev        = m;
    m->mscdev.ops.bulk       = ehci_msc_bulk;
    m->mscdev.ops.clear_halt = ehci_msc_clear_halt;
    m->mscdev.max_sect       = EHCI_MSC_MAX_SECT;
    m->mscdev.timeout_ms     = 60000;

    if (link_bulk_qh(m, true)  < 0) return -ENOMEM;
    if (link_bulk_qh(m, false) < 0) return -ENOMEM;

    m->active = true;

    if (usb_msc_inquiry(&m->mscdev) < 0) {
        serial_printf("[ehci-msc] INQUIRY failed for addr=%u\n", addr);
        return -EIO;
    }
    if (usb_msc_test_unit_ready(&m->mscdev) < 0) {
        serial_printf("[ehci-msc] TEST UNIT READY failed for addr=%u\n", addr);
        return -EIO;
    }
    if (usb_msc_read_capacity(&m->mscdev) < 0) {
        serial_printf("[ehci-msc] READ CAPACITY failed for addr=%u\n", addr);
        return -EIO;
    }

    snprintf(m->name, sizeof(m->name), "uhd%d", idx);

    blkdev_t *bd = &m->blkdev;
    memset(bd, 0, sizeof(*bd));
    strncpy(bd->name, m->name, BLKDEV_NAME_MAX - 1);
    snprintf(bd->model, BLKDEV_MODEL_MAX, "%s %s",
             m->mscdev.vendor[0]  ? m->mscdev.vendor  : "USB",
             m->mscdev.product[0] ? m->mscdev.product : "Mass Storage");
    bd->present      = true;
    bd->is_partition = false;
    bd->sector_count = m->mscdev.lba_count;
    bd->sector_size  = m->mscdev.block_size;
    bd->size_bytes   = m->mscdev.lba_count * (uint64_t)m->mscdev.block_size;
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
    serial_printf("[ehci-msc] /dev/%s: %s %s - %llu sectors x %u (%llu MB)\n",
                  m->name, m->mscdev.vendor, m->mscdev.product,
                  (unsigned long long)m->mscdev.lba_count, m->mscdev.block_size,
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

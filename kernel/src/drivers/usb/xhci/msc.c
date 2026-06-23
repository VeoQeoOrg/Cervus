#include "../../../../include/drivers/usb/xhci.h"
#include "../../../../include/time/clocksource.h"
#include "../../../../include/drivers/usb/usb_msc.h"
#include "../../../../include/drivers/disk/blkdev.h"
#include "../../../../include/drivers/disk/partition.h"
#include "../../../../include/fs/vfs.h"
#include "../../../../include/memory/dma.h"
#include "../../../../include/apic/apic.h"
#include "../../../../include/io/serial.h"
#include "../../../../include/syscall/errno.h"
#include <string.h>
#include <stdio.h>

extern void devfs_register(const char *name, vnode_t *node);

#define XHCI_MAX_MSC        4
#define XHCI_MSC_RING_TRBS  32
#define XHCI_MSC_MAX_SECT   64

typedef struct xhci_msc {
    usb_msc_dev_t mscdev;
    xhci_controller_t *ctl;
    uint8_t   slot_id;
    uint8_t   port_id;
    uint8_t   speed;
    uint8_t   intf;
    uint8_t   in_ep, out_ep;
    uint8_t   in_dci, out_dci;
    uint16_t  in_mps, out_mps;

    xhci_trb_t *in_ring;
    uintptr_t   in_ring_phys;
    uint16_t    in_enq;
    uint8_t     in_cyc;

    xhci_trb_t *out_ring;
    uintptr_t   out_ring_phys;
    uint16_t    out_enq;
    uint8_t     out_cyc;

    volatile uintptr_t in_pending_trb;
    volatile uintptr_t out_pending_trb;
    volatile uint8_t   in_cc, out_cc;
    volatile uint32_t  in_resid, out_resid;

    blkdev_t  blkdev;
    vnode_t   vnode;
    char      name[16];
    bool      active;
    bool      ready;
} xhci_msc_t;

static xhci_msc_t g_msc_devs[XHCI_MAX_MSC];
static int        g_msc_count = 0;
static uint64_t   g_msc_ino_base = 4000;

static int xhci_msc_bulk(void *dev, bool dir_in, void *virt, uintptr_t buf_phys,
                         uint32_t len, uint32_t timeout_ms)
{
    (void)virt;
    xhci_msc_t *m = (xhci_msc_t *)dev;
    xhci_trb_t *ring;
    uintptr_t   ring_phys;
    uint16_t   *enq;
    uint8_t    *cyc;
    uint8_t     dci;
    volatile uintptr_t *pending;
    volatile uint8_t   *cc_p;

    if (dir_in) {
        ring = m->in_ring;   ring_phys = m->in_ring_phys;
        enq  = &m->in_enq;   cyc       = &m->in_cyc;
        dci  = m->in_dci;
        pending = &m->in_pending_trb; cc_p = &m->in_cc;
    } else {
        ring = m->out_ring;  ring_phys = m->out_ring_phys;
        enq  = &m->out_enq;  cyc       = &m->out_cyc;
        dci  = m->out_dci;
        pending = &m->out_pending_trb; cc_p = &m->out_cc;
    }

    uintptr_t trb_phys = ring_phys + (uintptr_t)(*enq) * sizeof(xhci_trb_t);
    *pending = trb_phys;
    asm volatile("" ::: "memory");

    xhci_trb_t *t = &ring[*enq];
    t->parameter = (uint64_t)buf_phys;
    t->status    = len & 0xFFFF;
    uint32_t ctl = TRB_TYPE(TRB_NORMAL) | TRB_IOC;
    if (*cyc) ctl |= TRB_CYCLE;
    asm volatile("" ::: "memory");
    t->control = ctl;

    (*enq)++;
    if (*enq == XHCI_MSC_RING_TRBS - 1) {
        xhci_trb_t *link = &ring[XHCI_MSC_RING_TRBS - 1];
        uint32_t lctl = TRB_TYPE(TRB_LINK) | TRB_TC;
        if (*cyc) lctl |= TRB_CYCLE;
        link->control = lctl;
        *enq = 0;
        *cyc ^= 1;
    }

    asm volatile("" ::: "memory");
    m->ctl->doorbells[m->slot_id] = dci;

    uint64_t start = clocksource_now_ns();
    uint64_t deadline_ns = timeout_ms * 1000000ULL;
    while (*pending != 0) {
        xhci_drain_events(m->ctl);
        if (*pending == 0) break;
        if (clocksource_now_ns() - start > deadline_ns) {
            serial_printf("[xhci-msc] timeout on bulk %s dci=%u\n",
                          dir_in ? "IN" : "OUT", dci);
            return -ETIMEDOUT;
        }
        asm volatile("pause");
    }

    uint8_t cc = *cc_p;
    if (cc != CC_SUCCESS && cc != 13) return -EIO;
    return 0;
}

static int msc_blk_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    xhci_msc_t *m = (xhci_msc_t *)dev->priv;
    if (!m->ready) return -EIO;
    return usb_msc_rw10(&m->mscdev, lba, count, buf, false);
}
static int msc_blk_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    xhci_msc_t *m = (xhci_msc_t *)dev->priv;
    if (!m->ready) return -EIO;
    return usb_msc_rw10(&m->mscdev, lba, count, (void *)buf, true);
}
static int msc_blk_flush(blkdev_t *dev) {
    xhci_msc_t *m = (xhci_msc_t *)dev->priv;
    if (!m->ready) return 0;
    return usb_msc_sync_cache(&m->mscdev);
}
static const blkdev_ops_t g_msc_blkdev_ops = {
    .read_sectors  = msc_blk_read,
    .write_sectors = msc_blk_write,
    .flush         = msc_blk_flush,
};

static int64_t msc_vnode_read(vnode_t *node, void *buf, size_t len, uint64_t off) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    if (!dev) return -EIO;
    int r = blkdev_read(dev, off, buf, len);
    return (r < 0) ? r : (int64_t)len;
}
static int64_t msc_vnode_write(vnode_t *node, const void *buf, size_t len, uint64_t off) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    if (!dev) return -EIO;
    int r = blkdev_write(dev, off, buf, len);
    return (r < 0) ? r : (int64_t)len;
}
static int msc_vnode_stat(vnode_t *node, vfs_stat_t *out) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_BLKDEV;
    out->st_mode = 0660;
    out->st_size = dev ? dev->size_bytes : 0;
    return 0;
}
static void msc_vnode_noop(vnode_t *n) { (void)n; }
static const vnode_ops_t g_msc_vnode_ops = {
    .read  = msc_vnode_read,
    .write = msc_vnode_write,
    .stat  = msc_vnode_stat,
    .ref   = msc_vnode_noop,
    .unref = msc_vnode_noop,
};

static int configure_bulk_eps(xhci_controller_t *c, uint8_t slot_id,
                              uint8_t port_id, uint8_t speed,
                              uint8_t in_dci, uint16_t in_mps, uintptr_t in_tr,
                              uint8_t out_dci, uint16_t out_mps, uintptr_t out_tr)
{
    uintptr_t inctx_phys;
    uint8_t *inctx = (uint8_t *)dma_alloc_coherent(4096, &inctx_phys);
    if (!inctx) return -ENOMEM;
    memset(inctx, 0, 4096);

    uint32_t *icc = (uint32_t *)inctx;
    icc[0] = 0;
    icc[1] = (1u << 0) | (1u << in_dci) | (1u << out_dci);

    uint8_t max_dci = (in_dci > out_dci) ? in_dci : out_dci;
    uint32_t *slot_ctx = (uint32_t *)(inctx + 32);
    slot_ctx[0] = ((uint32_t)max_dci << 27) | ((uint32_t)speed << 20);
    slot_ctx[1] = (uint32_t)port_id << 16;

    uint32_t *ic = (uint32_t *)(inctx + 32 + (uint32_t)in_dci * 32);
    ic[0] = 0;
    ic[1] = (3u << 1) | ((uint32_t)EP_TYPE_BULK_IN << 3) | ((uint32_t)in_mps << 16);
    ic[2] = (uint32_t)(in_tr | 1);
    ic[3] = (uint32_t)((uint64_t)in_tr >> 32);
    ic[4] = (uint32_t)in_mps;

    uint32_t *oc = (uint32_t *)(inctx + 32 + (uint32_t)out_dci * 32);
    oc[0] = 0;
    oc[1] = (3u << 1) | ((uint32_t)EP_TYPE_BULK_OUT << 3) | ((uint32_t)out_mps << 16);
    oc[2] = (uint32_t)(out_tr | 1);
    oc[3] = (uint32_t)((uint64_t)out_tr >> 32);
    oc[4] = (uint32_t)out_mps;

    xhci_trb_t ev;
    int r = xhci_send_cmd(c, (uint64_t)inctx_phys, 0,
                          TRB_TYPE(TRB_CONFIGURE_ENDPOINT) |
                          ((uint32_t)slot_id << 24),
                          &ev);
    if (r < 0) return r;
    uint8_t cc = (uint8_t)((ev.status >> 24) & 0xFFu);
    if (cc != CC_SUCCESS) {
        serial_printf("[xhci] CONFIGURE_ENDPOINT(MSC): cc=%u\n", cc);
        return -EIO;
    }
    return 0;
}

static int msc_finalize(xhci_msc_t *m, int idx) {
    if (!m->active || m->ready) return 0;

    if (usb_msc_inquiry(&m->mscdev) < 0) {
        serial_printf("[xhci-msc] INQUIRY failed for slot %u\n", m->slot_id);
        return -EIO;
    }
    if (usb_msc_test_unit_ready(&m->mscdev) < 0) {
        serial_printf("[xhci-msc] TEST UNIT READY failed for slot %u\n", m->slot_id);
        return -EIO;
    }
    if (usb_msc_read_capacity(&m->mscdev) < 0) {
        serial_printf("[xhci-msc] READ CAPACITY failed for slot %u\n", m->slot_id);
        return -EIO;
    }

    snprintf(m->name, sizeof(m->name), "usb%d", idx);

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
    bd->ops          = &g_msc_blkdev_ops;
    bd->priv         = m;
    blkdev_register(bd);

    vnode_t *vn = &m->vnode;
    memset(vn, 0, sizeof(*vn));
    vn->type     = VFS_NODE_BLKDEV;
    vn->mode     = 0660;
    vn->ino      = g_msc_ino_base + (uint64_t)idx;
    vn->ops      = &g_msc_vnode_ops;
    vn->fs_data  = bd;
    vn->size     = bd->size_bytes;
    vn->refcount = 1;
    devfs_register(m->name, vn);

    m->ready = true;
    serial_printf("[xhci-msc] /dev/%s: %s %s - %llu sectors x %u (%llu MB)\n",
                  m->name, m->mscdev.vendor, m->mscdev.product,
                  (unsigned long long)m->mscdev.lba_count, m->mscdev.block_size,
                  (unsigned long long)(bd->size_bytes / (1024 * 1024)));

    partition_scan(bd);
    return 0;
}

int xhci_msc_register(xhci_controller_t *c, uint8_t slot_id,
                      uint8_t port_id, uint8_t speed,
                      const usb_msc_match_t *match)
{
    if (g_msc_count >= XHCI_MAX_MSC) return -ENOMEM;

    xhci_msc_t *m = &g_msc_devs[g_msc_count];
    memset(m, 0, sizeof(*m));
    m->ctl     = c;
    m->slot_id = slot_id;
    m->port_id = port_id;
    m->speed   = speed;
    m->intf    = match->intf;
    m->in_ep   = match->in_addr  & 0x0F;
    m->out_ep  = match->out_addr & 0x0F;
    m->in_dci  = (uint8_t)(2 * m->in_ep  + 1);
    m->out_dci = (uint8_t)(2 * m->out_ep + 0);
    m->in_mps  = match->in_mps;
    m->out_mps = match->out_mps;

    m->mscdev.ops.dev        = m;
    m->mscdev.ops.bulk       = xhci_msc_bulk;
    m->mscdev.ops.clear_halt = NULL;
    m->mscdev.max_sect       = XHCI_MSC_MAX_SECT;
    m->mscdev.timeout_ms     = 5000;

    m->in_ring = (xhci_trb_t *)dma_alloc_coherent(
        XHCI_MSC_RING_TRBS * sizeof(xhci_trb_t), &m->in_ring_phys);
    if (!m->in_ring) return -ENOMEM;
    memset(m->in_ring, 0, XHCI_MSC_RING_TRBS * sizeof(xhci_trb_t));
    xhci_trb_t *il = &m->in_ring[XHCI_MSC_RING_TRBS - 1];
    il->parameter = (uint64_t)m->in_ring_phys;
    il->control   = TRB_TYPE(TRB_LINK) | TRB_TC;
    m->in_cyc = 1;

    m->out_ring = (xhci_trb_t *)dma_alloc_coherent(
        XHCI_MSC_RING_TRBS * sizeof(xhci_trb_t), &m->out_ring_phys);
    if (!m->out_ring) return -ENOMEM;
    memset(m->out_ring, 0, XHCI_MSC_RING_TRBS * sizeof(xhci_trb_t));
    xhci_trb_t *ol = &m->out_ring[XHCI_MSC_RING_TRBS - 1];
    ol->parameter = (uint64_t)m->out_ring_phys;
    ol->control   = TRB_TYPE(TRB_LINK) | TRB_TC;
    m->out_cyc = 1;

    int r = configure_bulk_eps(c, slot_id, port_id, speed,
                               m->in_dci,  m->in_mps,  m->in_ring_phys,
                               m->out_dci, m->out_mps, m->out_ring_phys);
    if (r < 0) return r;

    m->active = true;
    int idx = g_msc_count++;

    serial_printf("[xhci]   MSC skeleton: slot=%u in_ep=%u(dci=%u,mps=%u) out_ep=%u(dci=%u,mps=%u)\n",
                  slot_id, m->in_ep, m->in_dci, m->in_mps,
                  m->out_ep, m->out_dci, m->out_mps);

    if (c->ready) msc_finalize(m, idx);
    return 0;
}

void xhci_msc_post_init(void) {
    for (int i = 0; i < g_msc_count; i++) {
        msc_finalize(&g_msc_devs[i], i);
    }
}

void xhci_msc_disconnect_slot(uint8_t slot_id) {
    for (int i = 0; i < g_msc_count; i++) {
        if (g_msc_devs[i].active && g_msc_devs[i].slot_id == slot_id) {
            g_msc_devs[i].active = false;
            g_msc_devs[i].ready  = false;
            g_msc_devs[i].blkdev.present = false;
            partition_remove_children(&g_msc_devs[i].blkdev);
            serial_printf("[xhci-hp] /dev/%s removed\n", g_msc_devs[i].name);
        }
    }
}

bool xhci_msc_handle_xfer_event(uint8_t slot_id, uint8_t dci, uint8_t cc,
                                uint32_t resid, uint64_t param)
{
    for (int i = 0; i < g_msc_count; i++) {
        xhci_msc_t *m = &g_msc_devs[i];
        if (!m->active || m->slot_id != slot_id) continue;
        if (dci == m->in_dci) {
            if (m->in_pending_trb && param == m->in_pending_trb) {
                m->in_cc    = cc;
                m->in_resid = resid;
                asm volatile("" ::: "memory");
                m->in_pending_trb = 0;
            }
            return true;
        }
        if (dci == m->out_dci) {
            if (m->out_pending_trb && param == m->out_pending_trb) {
                m->out_cc    = cc;
                m->out_resid = resid;
                asm volatile("" ::: "memory");
                m->out_pending_trb = 0;
            }
            return true;
        }
    }
    return false;
}

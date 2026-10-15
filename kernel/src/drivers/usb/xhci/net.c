#include "../../../../include/drivers/usb/xhci.h"
#include "../../../../include/drivers/usb/usb_net.h"
#include "../../../../include/time/clocksource.h"
#include "../../../../include/memory/dma.h"
#include "../../../../include/memory/pmm.h"
#include "../../../../include/io/serial.h"
#include "../../../../include/syscall/errno.h"
#include <string.h>

#define XHCI_MAX_NET   4
#define NET_RING_TRBS  64

#define CC_SUCCESS_     1
#define CC_SHORT_PACKET 13

typedef struct {
    bool               active;
    bool               started;
    xhci_controller_t *ctl;
    uint8_t            slot_id;
    xhci_topology_t    topo;
    xhci_trb_t        *ep0_ring;
    uintptr_t          ep0_phys;
    uint16_t           ep0_enq;
    uint8_t            ep0_cyc;
    usb_net_iface_t    ifc;
    uint16_t           vid, pid;

    uint8_t            in_dci, out_dci;
    xhci_trb_t        *in_ring, *out_ring;
    uintptr_t          in_ring_phys, out_ring_phys;
    uint16_t           in_enq, out_enq;
    uint8_t            in_cyc, out_cyc;
    volatile uintptr_t in_trb[USBNET_RX_BUFS];
    uint32_t           in_len[USBNET_RX_BUFS];
    volatile bool      in_halted;
    volatile uintptr_t out_pending;
    volatile uint8_t   out_cc;

    usbnet_t          *un;
} xhci_net_t;

static xhci_net_t g_nets[XHCI_MAX_NET];

static xhci_trb_t *ring_alloc(uintptr_t *phys) {
    xhci_trb_t *r = dma_alloc_coherent(NET_RING_TRBS * sizeof(xhci_trb_t), phys);
    if (!r) return NULL;
    memset(r, 0, NET_RING_TRBS * sizeof(xhci_trb_t));
    r[NET_RING_TRBS - 1].parameter = (uint64_t)*phys;
    r[NET_RING_TRBS - 1].control = TRB_TYPE(TRB_LINK) | TRB_TC;
    return r;
}

static uintptr_t ring_push(xhci_trb_t *ring, uintptr_t ring_phys, uint16_t *enq, uint8_t *cyc,
                           uint64_t param, uint32_t status, uint32_t ctl) {
    uintptr_t phys = ring_phys + (uintptr_t)(*enq) * sizeof(xhci_trb_t);
    xhci_trb_t *t = &ring[*enq];
    t->parameter = param;
    t->status = status;
    asm volatile("" ::: "memory");
    t->control = ctl | (*cyc ? TRB_CYCLE : 0);
    (*enq)++;
    if (*enq == NET_RING_TRBS - 1) {
        xhci_trb_t *link = &ring[NET_RING_TRBS - 1];
        link->control = TRB_TYPE(TRB_LINK) | TRB_TC | (*cyc ? TRB_CYCLE : 0);
        *enq = 0;
        *cyc ^= 1;
    }
    return phys;
}

static int configure_eps(xhci_net_t *n) {
    xhci_controller_t *c = n->ctl;
    uintptr_t inctx_phys;
    uint8_t *inctx = dma_alloc_coherent(4096, &inctx_phys);
    if (!inctx) return -ENOMEM;
    memset(inctx, 0, 4096);

    uint32_t *icc = (uint32_t *)inctx;
    icc[1] = (1u << 0) | (1u << n->in_dci) | (1u << n->out_dci);

    const uint32_t *out_slot = (const uint32_t *)pmm_phys_to_virt((uintptr_t)c->dcbaa[n->slot_id]);
    uint32_t *slot = (uint32_t *)(inctx + 32);
    uint8_t max_dci = n->in_dci > n->out_dci ? n->in_dci : n->out_dci;
    uint32_t cur_entries = out_slot[0] >> 27;
    if (cur_entries > max_dci) max_dci = (uint8_t)cur_entries;
    slot[0] = (out_slot[0] & 0x07FFFFFFu) | ((uint32_t)max_dci << 27);
    slot[1] = out_slot[1];
    slot[2] = out_slot[2];
    slot[3] = 0;

    uint32_t *ic = (uint32_t *)(inctx + 32 + (uint32_t)n->in_dci * 32);
    ic[1] = (3u << 1) | ((uint32_t)EP_TYPE_BULK_IN << 3) | ((uint32_t)n->ifc.in_mps << 16);
    ic[2] = (uint32_t)(n->in_ring_phys | 1);
    ic[3] = (uint32_t)((uint64_t)n->in_ring_phys >> 32);
    ic[4] = 1024;

    uint32_t *oc = (uint32_t *)(inctx + 32 + (uint32_t)n->out_dci * 32);
    oc[1] = (3u << 1) | ((uint32_t)EP_TYPE_BULK_OUT << 3) | ((uint32_t)n->ifc.out_mps << 16);
    oc[2] = (uint32_t)(n->out_ring_phys | 1);
    oc[3] = (uint32_t)((uint64_t)n->out_ring_phys >> 32);
    oc[4] = 1024;

    xhci_trb_t ev;
    int r = xhci_send_cmd(c, (uint64_t)inctx_phys, 0,
                          TRB_TYPE(TRB_CONFIGURE_ENDPOINT) | ((uint32_t)n->slot_id << 24), &ev);
    dma_free_coherent(inctx, 4096);
    if (r < 0) return r;
    uint8_t cc = (uint8_t)((ev.status >> 24) & 0xFF);
    if (cc != CC_SUCCESS_) {
        serial_printf("[xhci-net] CONFIGURE_ENDPOINT slot %u: cc=%u\n", n->slot_id, cc);
        return -EIO;
    }
    return 0;
}

static int glue_control(void *dev, uint8_t type, uint8_t req, uint16_t value,
                        uint16_t index, void *data, uint16_t len) {
    xhci_net_t *n = dev;
    return xhci_control_xfer(n->ctl, n->slot_id, n->ep0_ring, n->ep0_phys,
                             &n->ep0_enq, &n->ep0_cyc, type, req, value, index, len, data);
}

static int glue_set_interface(void *dev, uint8_t intf, uint8_t alt) {
    return glue_control(dev, 0x01, 0x0B, alt, intf, NULL, 0);
}

static int glue_bulk_out(void *dev, uintptr_t phys, uint32_t len) {
    xhci_net_t *n = dev;
    n->out_pending = 1;
    uintptr_t trb = ring_push(n->out_ring, n->out_ring_phys, &n->out_enq, &n->out_cyc,
                              (uint64_t)phys, len & 0x1FFFF, TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    n->out_pending = trb;
    asm volatile("" ::: "memory");
    n->ctl->doorbells[n->slot_id] = n->out_dci;
    uint64_t start = clocksource_now_ns();
    while (n->out_pending) {
        xhci_drain_events(n->ctl);
        if (!n->out_pending) break;
        if (clocksource_now_ns() - start > 1000000000ULL) {
            serial_printf("[xhci-net] slot %u: bulk OUT timed out\n", n->slot_id);
            n->out_pending = 0;
            return -ETIMEDOUT;
        }
        asm volatile("pause");
    }
    return (n->out_cc == CC_SUCCESS_ || n->out_cc == CC_SHORT_PACKET) ? 0 : -EIO;
}

static int glue_rx_post(void *dev, int idx, uintptr_t phys, uint32_t len) {
    xhci_net_t *n = dev;
    if (!n->active) return -ENODEV;
    if (n->in_halted) {
        n->in_halted = false;
        uintptr_t deq = n->in_ring_phys + (uintptr_t)n->in_enq * sizeof(xhci_trb_t);
        if (xhci_reset_endpoint(n->ctl, n->slot_id, n->in_dci) == 0)
            xhci_set_tr_dequeue(n->ctl, n->slot_id, n->in_dci, deq, n->in_cyc);
    }
    n->in_len[idx] = len;
    n->in_trb[idx] = 1;
    uintptr_t trb = ring_push(n->in_ring, n->in_ring_phys, &n->in_enq, &n->in_cyc,
                              (uint64_t)phys, len & 0x1FFFF,
                              TRB_TYPE(TRB_NORMAL) | TRB_IOC | (1u << 2));
    n->in_trb[idx] = trb;
    asm volatile("" ::: "memory");
    n->ctl->doorbells[n->slot_id] = n->in_dci;
    return 0;
}

static void glue_poll(void *dev) {
    xhci_net_t *n = dev;
    xhci_drain_events(n->ctl);
}

bool xhci_net_handle_xfer_event(uint8_t slot_id, uint8_t dci, uint8_t cc,
                                uint32_t resid, uint64_t param) {
    for (int i = 0; i < XHCI_MAX_NET; i++) {
        xhci_net_t *n = &g_nets[i];
        if (!n->active || n->slot_id != slot_id) continue;
        if (dci == n->out_dci) {
            if (n->out_pending && param == n->out_pending) {
                n->out_cc = cc;
                asm volatile("" ::: "memory");
                n->out_pending = 0;
            }
            return true;
        }
        if (dci == n->in_dci) {
            bool ok = cc == CC_SUCCESS_ || cc == CC_SHORT_PACKET;
            if (!ok) {
                n->in_halted = true;
                for (int k = 0; k < USBNET_RX_BUFS; k++) {
                    if (n->in_trb[k] > 1) {
                        n->in_trb[k] = 0;
                        usbnet_rx_done(n->un, k, 0, false);
                    }
                }
                return true;
            }
            for (int k = 0; k < USBNET_RX_BUFS; k++) {
                if (n->in_trb[k] > 1 && n->in_trb[k] == param) {
                    uint32_t actual = resid <= n->in_len[k] ? n->in_len[k] - resid : 0;
                    n->in_trb[k] = 0;
                    usbnet_rx_done(n->un, k, actual, true);
                    break;
                }
            }
            return true;
        }
        return false;
    }
    return false;
}

static void net_start(xhci_net_t *n) {
    if (n->started || !n->un) return;
    n->started = true;
    if (usbnet_start(n->un) < 0) n->active = false;
}

int xhci_net_register(xhci_controller_t *c, uint8_t slot_id, const xhci_topology_t *topo,
                      xhci_trb_t *ep0_ring, uintptr_t ep0_phys, uint16_t enq, uint8_t cyc,
                      uint16_t vid, uint16_t pid, const void *net_iface) {
    const usb_net_iface_t *ifc = net_iface;
    xhci_net_t *n = NULL;
    for (int i = 0; i < XHCI_MAX_NET; i++)
        if (!g_nets[i].active) { n = &g_nets[i]; break; }
    if (!n) return -ENOMEM;
    memset(n, 0, sizeof *n);
    n->ctl = c;
    n->slot_id = slot_id;
    n->topo = *topo;
    n->ep0_ring = ep0_ring;
    n->ep0_phys = ep0_phys;
    n->ep0_enq = enq;
    n->ep0_cyc = cyc;
    n->ifc = *ifc;
    n->vid = vid;
    n->pid = pid;
    n->in_dci = (uint8_t)(ifc->in_ep * 2 + 1);
    n->out_dci = (uint8_t)(ifc->out_ep * 2);
    n->in_ring = ring_alloc(&n->in_ring_phys);
    n->out_ring = ring_alloc(&n->out_ring_phys);
    if (!n->in_ring || !n->out_ring) return -ENOMEM;
    n->in_cyc = n->out_cyc = 1;
    if (configure_eps(n) < 0) return -EIO;

    usbnet_glue_t g = {
        .dev = n,
        .control = glue_control,
        .set_interface = glue_set_interface,
        .bulk_out = glue_bulk_out,
        .rx_post = glue_rx_post,
        .poll = glue_poll,
    };
    n->un = usbnet_create(&g, ifc, vid, pid);
    if (!n->un) return -ENOMEM;
    n->active = true;
    serial_printf("[xhci-net] slot %u: %04x:%04x network function, in dci %u, out dci %u\n",
                  slot_id, vid, pid, n->in_dci, n->out_dci);
    if (c->ready) net_start(n);
    return 0;
}

void xhci_net_post_init(void) {
    for (int i = 0; i < XHCI_MAX_NET; i++)
        if (g_nets[i].active) net_start(&g_nets[i]);
}

void xhci_net_disconnect_slot(uint8_t slot_id) {
    for (int i = 0; i < XHCI_MAX_NET; i++) {
        xhci_net_t *n = &g_nets[i];
        if (n->active && n->slot_id == slot_id) {
            n->active = false;
            usbnet_stop(n->un);
            serial_printf("[xhci-net] slot %u disconnected\n", slot_id);
        }
    }
}

#include "../../../../include/drivers/usb/xhci.h"
#include "../../../../include/drivers/usb/usb_hid.h"
#include "../../../../include/memory/dma.h"
#include "../../../../include/io/serial.h"
#include "../../../../include/syscall/errno.h"
#include <string.h>

#define XHCI_HID_RING_TRBS  64
#define XHCI_HID_REPORT_LEN USB_HID_REPORT_LEN
#define XHCI_MAX_HID        8

typedef struct {
    xhci_controller_t *ctl;
    uint8_t   slot_id;
    uint8_t   port_id;
    uint8_t   speed;
    uint8_t   ep_dci;
    uint8_t   ep_interval;
    uint16_t  ep_mps;
    xhci_trb_t *ring;
    uintptr_t  ring_phys;
    uint16_t   enq;
    uint8_t    cyc;
    uint8_t   *buf;
    uintptr_t  buf_phys;
    usb_hid_kbd_state_t state;
    bool       active;
} xhci_hid_kbd_t;

static xhci_hid_kbd_t g_hid_kbds[XHCI_MAX_HID];
static int            g_hid_count = 0;

static void hid_post_normal(xhci_hid_kbd_t *k) {
    xhci_trb_t *t = &k->ring[k->enq];
    t->parameter = (uint64_t)k->buf_phys;
    t->status    = XHCI_HID_REPORT_LEN;
    uint32_t ctl = TRB_TYPE(TRB_NORMAL) | TRB_IOC;
    if (k->cyc) ctl |= TRB_CYCLE;
    asm volatile("" ::: "memory");
    t->control = ctl;

    k->enq++;
    if (k->enq == XHCI_HID_RING_TRBS - 1) {
        xhci_trb_t *link = &k->ring[XHCI_HID_RING_TRBS - 1];
        uint32_t lctl = TRB_TYPE(TRB_LINK) | TRB_TC;
        if (k->cyc) lctl |= TRB_CYCLE;
        link->control = lctl;
        k->enq = 0;
        k->cyc ^= 1;
    }

    asm volatile("" ::: "memory");
    k->ctl->doorbells[k->slot_id] = k->ep_dci;
}

static int configure_intr_ep(xhci_controller_t *c, uint8_t slot_id,
                             uint8_t port_id, uint8_t speed,
                             uint8_t ep_dci, uint16_t mps, uint8_t bInterval,
                             uintptr_t tr_phys)
{
    uintptr_t inctx_phys;
    uint8_t *inctx = (uint8_t *)dma_alloc_coherent(4096, &inctx_phys);
    if (!inctx) return -ENOMEM;
    memset(inctx, 0, 4096);

    uint32_t *icc = (uint32_t *)inctx;
    icc[0] = 0;
    icc[1] = (1u << 0) | (1u << ep_dci);

    uint32_t *slot_ctx = (uint32_t *)(inctx + 32);
    slot_ctx[0] = ((uint32_t)ep_dci << 27) | ((uint32_t)speed << 20);
    slot_ctx[1] = (uint32_t)port_id << 16;

    uint8_t interval = bInterval;
    if (speed == 1 || speed == 2) {
        uint32_t mframes = (uint32_t)bInterval * 8;
        uint8_t ivl = 0;
        while ((1u << ivl) < mframes) ivl++;
        if (ivl < 3)  ivl = 3;
        if (ivl > 10) ivl = 10;
        interval = ivl;
    } else {
        interval = bInterval ? (uint8_t)(bInterval - 1) : 0;
        if (interval > 15) interval = 15;
    }

    uint32_t *ep_ctx = (uint32_t *)(inctx + 32 + (uint32_t)ep_dci * 32);
    ep_ctx[0] = ((uint32_t)interval << 16);
    ep_ctx[1] = (3u << 1) | ((uint32_t)EP_TYPE_INTR_IN << 3) | ((uint32_t)mps << 16);
    ep_ctx[2] = (uint32_t)(tr_phys | 1);
    ep_ctx[3] = (uint32_t)((uint64_t)tr_phys >> 32);
    ep_ctx[4] = (uint32_t)mps;

    xhci_trb_t ev;
    int r = xhci_send_cmd(c, (uint64_t)inctx_phys, 0,
                          TRB_TYPE(TRB_CONFIGURE_ENDPOINT) |
                          ((uint32_t)slot_id << 24),
                          &ev);
    if (r < 0) return r;
    uint8_t cc = (uint8_t)((ev.status >> 24) & 0xFFu);
    if (cc != CC_SUCCESS) {
        serial_printf("[xhci] CONFIGURE_ENDPOINT: cc=%u\n", cc);
        return -EIO;
    }
    return 0;
}

static int hid_set_protocol(xhci_controller_t *c, uint8_t slot_id,
                            xhci_trb_t *ep0_ring, uintptr_t ep0_phys,
                            uint16_t *enq, uint8_t *cyc,
                            uint8_t intf, uint8_t protocol)
{
    return xhci_control_xfer(c, slot_id, ep0_ring, ep0_phys, enq, cyc,
                             0x21, 0x0B, (uint16_t)protocol, intf, 0, NULL);
}

static int hid_set_idle(xhci_controller_t *c, uint8_t slot_id,
                        xhci_trb_t *ep0_ring, uintptr_t ep0_phys,
                        uint16_t *enq, uint8_t *cyc,
                        uint8_t intf, uint8_t duration)
{
    uint16_t wValue = (uint16_t)duration << 8;
    return xhci_control_xfer(c, slot_id, ep0_ring, ep0_phys, enq, cyc,
                             0x21, 0x0A, wValue, intf, 0, NULL);
}

void xhci_hid_scan(const uint8_t *buf, uint16_t total, usb_kbd_match_t *m) {
    m->found = false;
    if (total < 9) return;
    uint16_t pos = 9;
    bool in_kbd = false;
    while (pos + 2 <= total) {
        uint8_t blen  = buf[pos];
        uint8_t btype = buf[pos + 1];
        if (blen < 2 || pos + blen > total) break;
        if (btype == USB_DESC_INTERFACE && blen >= 9) {
            uint8_t cls   = buf[pos + 5];
            uint8_t sub   = buf[pos + 6];
            uint8_t proto = buf[pos + 7];
            in_kbd = (cls == 0x03 && sub == 0x01 && proto == 0x01);
            if (in_kbd) m->intf = buf[pos + 2];
        } else if (btype == USB_DESC_ENDPOINT && blen >= 7 && in_kbd && !m->found) {
            uint8_t addr = buf[pos + 2];
            uint8_t attr = buf[pos + 3];
            uint16_t mps = (uint16_t)buf[pos + 4] | ((uint16_t)buf[pos + 5] << 8);
            if ((addr & 0x80) && (attr & 0x3) == 3) {
                m->ep_addr     = addr;
                m->ep_mps      = mps & 0x7FF;
                m->ep_interval = buf[pos + 6];
                m->found       = true;
                return;
            }
        }
        pos += blen;
    }
}

int xhci_hid_kbd_register(xhci_controller_t *c, uint8_t slot_id,
                          uint8_t port_id, uint8_t speed,
                          xhci_trb_t *ep0_ring, uintptr_t ep0_phys,
                          uint16_t *enq, uint8_t *cyc,
                          const usb_kbd_match_t *m)
{
    if (g_hid_count >= XHCI_MAX_HID) return -ENOMEM;
    uint8_t ep_num = m->ep_addr & 0x0F;
    uint8_t ep_dci = (uint8_t)(2 * ep_num + 1);

    uintptr_t ring_phys;
    xhci_trb_t *ring = (xhci_trb_t *)dma_alloc_coherent(
        XHCI_HID_RING_TRBS * sizeof(xhci_trb_t), &ring_phys);
    if (!ring) return -ENOMEM;
    memset(ring, 0, XHCI_HID_RING_TRBS * sizeof(xhci_trb_t));
    xhci_trb_t *link = &ring[XHCI_HID_RING_TRBS - 1];
    link->parameter = (uint64_t)ring_phys;
    link->control   = TRB_TYPE(TRB_LINK) | TRB_TC;

    int r = configure_intr_ep(c, slot_id, port_id, speed,
                              ep_dci, m->ep_mps, m->ep_interval, ring_phys);
    if (r < 0) return r;

    (void)hid_set_protocol(c, slot_id, ep0_ring, ep0_phys, enq, cyc, m->intf, 0);
    (void)hid_set_idle    (c, slot_id, ep0_ring, ep0_phys, enq, cyc, m->intf, 0);

    uintptr_t buf_phys;
    uint8_t *buf = (uint8_t *)dma_alloc_coherent(64, &buf_phys);
    if (!buf) return -ENOMEM;
    memset(buf, 0, 64);

    xhci_hid_kbd_t *k = &g_hid_kbds[g_hid_count++];
    memset(k, 0, sizeof(*k));
    k->ctl          = c;
    k->slot_id      = slot_id;
    k->port_id      = port_id;
    k->speed        = speed;
    k->ep_dci       = ep_dci;
    k->ep_mps       = m->ep_mps;
    k->ep_interval  = m->ep_interval;
    k->ring         = ring;
    k->ring_phys    = ring_phys;
    k->enq          = 0;
    k->cyc          = 1;
    k->buf          = buf;
    k->buf_phys     = buf_phys;
    k->active       = true;

    hid_post_normal(k);

    serial_printf("[xhci]   HID kbd attached: slot=%u dci=%u mps=%u ivl=%u\n",
                  slot_id, ep_dci, m->ep_mps, m->ep_interval);
    return 0;
}

void xhci_hid_kbd_tick(void) {
    usb_hid_kbd_state_t *states[XHCI_MAX_HID];
    int n = 0;
    for (int i = 0; i < g_hid_count; i++) {
        if (g_hid_kbds[i].active) states[n++] = &g_hid_kbds[i].state;
    }
    if (n > 0) usb_hid_kbd_tick_repeats(states, n);
}

void xhci_hid_kbd_disconnect_slot(uint8_t slot_id) {
    for (int i = 0; i < g_hid_count; i++) {
        if (g_hid_kbds[i].active && g_hid_kbds[i].slot_id == slot_id)
            g_hid_kbds[i].active = false;
    }
}

bool xhci_hid_kbd_handle_xfer_event(uint8_t slot_id, uint8_t dci) {
    for (int i = 0; i < g_hid_count; i++) {
        xhci_hid_kbd_t *k = &g_hid_kbds[i];
        if (k->active && k->slot_id == slot_id && k->ep_dci == dci) {
            usb_hid_kbd_process_report(&k->state, k->buf);
            hid_post_normal(k);
            return true;
        }
    }
    return false;
}

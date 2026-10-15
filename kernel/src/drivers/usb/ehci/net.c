#include "../../../../include/drivers/usb/ehci.h"
#include "../../../../include/drivers/usb/usb_net.h"
#include "../../../../include/time/clocksource.h"
#include "../../../../include/memory/dma.h"
#include "../../../../include/io/serial.h"
#include "../../../../include/syscall/errno.h"
#include <string.h>

#define EHCI_MAX_NET 4
#define POOL_QTDS    32
#define QTD_ERRORS   (QTD_STATUS_HALTED | QTD_STATUS_DBE | QTD_STATUS_BABBLE | \
                      QTD_STATUS_XACTERR | QTD_STATUS_MISSED)

typedef struct {
    bool               active;
    bool               started;
    ehci_controller_t *ctl;
    uint8_t            addr, speed;
    uint16_t           ep0_mps;
    usb_net_iface_t    ifc;
    uint16_t           vid, pid;

    ehci_qh_t         *in_qh, *out_qh;
    uintptr_t          in_qh_phys, out_qh_phys;
    uint32_t           out_dt;

    ehci_qtd_t        *pool;
    uintptr_t          pool_phys;
    bool               used[POOL_QTDS];
    int                tail;
    int                rx_qtd[USBNET_RX_BUFS];
    uint32_t           rx_len[USBNET_RX_BUFS];
    bool               halted;

    usbnet_t          *un;
} ehci_net_t;

static ehci_net_t g_nets[EHCI_MAX_NET];
static bool g_worker_running;

static void net_start(ehci_net_t *n) {
    if (n->started) return;
    n->started = true;
    if (usbnet_start(n->un) < 0) n->active = false;
}

static uintptr_t qtd_phys(ehci_net_t *n, int i) { return n->pool_phys + (uintptr_t)i * sizeof(ehci_qtd_t); }

static int pool_get(ehci_net_t *n) {
    for (int i = 0; i < POOL_QTDS; i++) {
        if (n->used[i] || i == n->tail) continue;
        if ((n->in_qh->cur_qtd & ~0x1Fu) == (uint32_t)qtd_phys(n, i)) continue;
        n->used[i] = true;
        memset(&n->pool[i], 0, sizeof(ehci_qtd_t));
        return i;
    }
    return -1;
}

static int link_qh(ehci_net_t *n, bool in_dir, ehci_qh_t **out, uintptr_t *out_phys) {
    uintptr_t phys;
    ehci_qh_t *qh = dma_alloc_coherent_low(4096, &phys);
    if (!qh || phys >= 0xFFFFFFFFULL) return -ENOMEM;
    memset(qh, 0, sizeof *qh);
    uint8_t ep = in_dir ? n->ifc.in_ep : n->ifc.out_ep;
    uint16_t mps = in_dir ? n->ifc.in_mps : n->ifc.out_mps;
    qh->ep_chars = ((uint32_t)n->addr & 0x7F) | ((uint32_t)ep << 8) | ((uint32_t)n->speed << 12) |
                   ((uint32_t)mps << 16) | (n->speed == EHCI_SPEED_HS ? 0u : (1u << 27));
    qh->ep_caps = 1u << 30;
    if (in_dir) {
        qh->overlay_next = (uint32_t)qtd_phys(n, n->tail);
        qh->overlay_alt_next = QTD_T;
        qh->overlay_token = 0;
    } else {
        qh->overlay_next = QTD_T;
        qh->overlay_alt_next = QTD_T;
        qh->overlay_token = QTD_STATUS_HALTED;
    }
    uint32_t old;
    if (ehci_async_pause(n->ctl, &old) < 0) return -EIO;
    qh->hlp = n->ctl->async_head->hlp;
    asm volatile("" ::: "memory");
    n->ctl->async_head->hlp = (uint32_t)phys | QH_TYPE_QH;
    ehci_async_resume(n->ctl, old);
    *out = qh;
    *out_phys = phys;
    return 0;
}

static int glue_control(void *dev, uint8_t type, uint8_t req, uint16_t value,
                        uint16_t index, void *data, uint16_t len) {
    ehci_net_t *n = dev;
    uint8_t setup[8] = { type, req, (uint8_t)value, (uint8_t)(value >> 8),
                         (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)len, (uint8_t)(len >> 8) };
    return ehci_control_xfer(n->ctl, n->addr, n->speed, n->ep0_mps, setup, data, len, (type & 0x80) != 0);
}

static int glue_set_interface(void *dev, uint8_t intf, uint8_t alt) {
    return glue_control(dev, 0x01, 0x0B, alt, intf, NULL, 0);
}

static int glue_bulk_out(void *dev, uintptr_t phys, uint32_t len) {
    ehci_net_t *n = dev;
    uintptr_t tp;
    ehci_qtd_t *t = alloc_qtd(&tp);
    if (!t) return -ENOMEM;
    t->next = QTD_T;
    t->alt_next = QTD_T;
    t->token = QTD_STATUS_ACTIVE | QTD_CERR_3 | QTD_IOC | ((uint32_t)len << QTD_TBT_SHIFT) | QTD_PID_OUT;
    qtd_set_buf(t, phys, len);
    uint32_t old;
    if (ehci_async_pause(n->ctl, &old) < 0) { dma_free_coherent(t, 4096); return -EIO; }
    ehci_qh_t *qh = n->out_qh;
    qh->cur_qtd = 0;
    qh->overlay_next = (uint32_t)tp;
    qh->overlay_alt_next = QTD_T;
    qh->overlay_token = n->out_dt;
    for (int i = 0; i < 5; i++) qh->overlay_buf[i] = 0;
    asm volatile("" ::: "memory");
    ehci_async_resume(n->ctl, old);

    uint64_t deadline = clocksource_now_ns() + 1000000000ULL;
    int r = -ETIMEDOUT;
    while (clocksource_now_ns() < deadline) {
        uint32_t st = t->token & 0xFF;
        if (!(st & QTD_STATUS_ACTIVE)) { r = (st & QTD_ERRORS) ? -EIO : 0; break; }
        asm volatile("pause");
    }
    n->out_dt = qh->overlay_token & QTD_DT;
    if (ehci_async_pause(n->ctl, &old) == 0) {
        qh->cur_qtd = 0;
        qh->overlay_next = QTD_T;
        qh->overlay_alt_next = QTD_T;
        qh->overlay_token = n->out_dt | QTD_STATUS_HALTED;
        asm volatile("" ::: "memory");
        ehci_async_resume(n->ctl, old);
    }
    dma_free_coherent(t, 4096);
    return r;
}

static void reset_in_queue(ehci_net_t *n) {
    for (int idx = 0; idx < USBNET_RX_BUFS; idx++) {
        if (n->rx_qtd[idx] >= 0) {
            n->rx_qtd[idx] = -1;
            usbnet_rx_done(n->un, idx, 0, false);
        }
    }
    for (int i = 0; i < POOL_QTDS; i++) n->used[i] = false;
    n->tail = 0;
    memset(&n->pool[0], 0, sizeof(ehci_qtd_t));
    n->pool[0].next = QTD_T;
    n->pool[0].alt_next = QTD_T;
    n->pool[0].token = QTD_STATUS_HALTED;
    uint32_t old;
    if (ehci_async_pause(n->ctl, &old) == 0) {
        n->in_qh->cur_qtd = 0;
        n->in_qh->overlay_next = (uint32_t)qtd_phys(n, 0);
        n->in_qh->overlay_alt_next = QTD_T;
        n->in_qh->overlay_token = 0;
        asm volatile("" ::: "memory");
        ehci_async_resume(n->ctl, old);
    }
    uint8_t clr[8] = { 0x02, 0x01, 0, 0, (uint8_t)(n->ifc.in_ep | 0x80), 0, 0, 0 };
    ehci_control_xfer(n->ctl, n->addr, n->speed, n->ep0_mps, clr, NULL, 0, false);
}

static int glue_rx_post(void *dev, int idx, uintptr_t phys, uint32_t len) {
    ehci_net_t *n = dev;
    if (!n->active) return -ENODEV;
    if (n->halted) {
        n->halted = false;
        reset_in_queue(n);
    }
    int nd = pool_get(n);
    if (nd < 0) return -ENOMEM;
    n->pool[nd].next = QTD_T;
    n->pool[nd].alt_next = QTD_T;
    n->pool[nd].token = QTD_STATUS_HALTED;

    int cur = n->tail;
    ehci_qtd_t *t = &n->pool[cur];
    n->used[cur] = true;
    t->next = (uint32_t)qtd_phys(n, nd);
    t->alt_next = QTD_T;
    qtd_set_buf(t, phys, len);
    n->rx_len[idx] = len;
    n->rx_qtd[idx] = cur;
    asm volatile("" ::: "memory");
    t->token = QTD_STATUS_ACTIVE | QTD_CERR_3 | QTD_IOC | ((uint32_t)len << QTD_TBT_SHIFT) | QTD_PID_IN;
    n->tail = nd;
    n->used[nd] = false;
    return 0;
}

static void glue_poll(void *dev) {
    ehci_net_t *n = dev;
    if (!n->active) return;
    for (int idx = 0; idx < USBNET_RX_BUFS; idx++) {
        int q = n->rx_qtd[idx];
        if (q < 0) continue;
        uint32_t tok = n->pool[q].token;
        if (tok & QTD_STATUS_ACTIVE) continue;
        n->rx_qtd[idx] = -1;
        n->used[q] = false;
        if (tok & QTD_ERRORS) {
            n->halted = true;
            usbnet_rx_done(n->un, idx, 0, false);
            continue;
        }
        uint32_t left = (tok >> QTD_TBT_SHIFT) & 0x7FFF;
        uint32_t actual = left <= n->rx_len[idx] ? n->rx_len[idx] - left : 0;
        usbnet_rx_done(n->un, idx, actual, true);
    }
}

int ehci_net_setup(ehci_controller_t *c, uint8_t addr, uint8_t speed, uint16_t ep0_mps,
                   uint16_t vid, uint16_t pid, const void *ifc_ptr) {
    const usb_net_iface_t *ifc = ifc_ptr;
    ehci_net_t *n = NULL;
    for (int i = 0; i < EHCI_MAX_NET; i++)
        if (!g_nets[i].active) { n = &g_nets[i]; break; }
    if (!n) return -ENOMEM;
    memset(n, 0, sizeof *n);
    n->ctl = c;
    n->addr = addr;
    n->speed = speed;
    n->ep0_mps = ep0_mps;
    n->ifc = *ifc;
    n->vid = vid;
    n->pid = pid;
    for (int i = 0; i < USBNET_RX_BUFS; i++) n->rx_qtd[i] = -1;
    n->pool = dma_alloc_coherent_low(POOL_QTDS * sizeof(ehci_qtd_t), &n->pool_phys);
    if (!n->pool || n->pool_phys >= 0xFFFFFFFFULL) return -ENOMEM;
    memset(n->pool, 0, POOL_QTDS * sizeof(ehci_qtd_t));
    n->tail = 0;
    n->pool[0].next = QTD_T;
    n->pool[0].alt_next = QTD_T;
    n->pool[0].token = QTD_STATUS_HALTED;
    if (link_qh(n, true, &n->in_qh, &n->in_qh_phys) < 0) return -EIO;
    if (link_qh(n, false, &n->out_qh, &n->out_qh_phys) < 0) return -EIO;

    usbnet_glue_t g = {
        .dev = n,
        .control = glue_control,
        .set_interface = glue_set_interface,
        .bulk_out = glue_bulk_out,
        .rx_post = glue_rx_post,
        .poll = glue_poll,
        .polled = true,
    };
    n->un = usbnet_create(&g, ifc, vid, pid);
    if (!n->un) return -ENOMEM;
    n->active = true;
    serial_printf("[ehci-net] addr %u: %04x:%04x network function\n", addr, vid, pid);
    if (g_worker_running) net_start(n);
    return 0;
}

void ehci_net_post_init(void) {
    g_worker_running = true;
    for (int i = 0; i < EHCI_MAX_NET; i++)
        if (g_nets[i].active) net_start(&g_nets[i]);
}

void ehci_net_deactivate_addr(uint8_t addr) {
    for (int i = 0; i < EHCI_MAX_NET; i++) {
        ehci_net_t *n = &g_nets[i];
        if (n->active && n->addr == addr) {
            n->active = false;
            usbnet_stop(n->un);
        }
    }
}

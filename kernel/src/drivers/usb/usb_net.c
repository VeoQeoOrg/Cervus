#include "../../../include/drivers/usb/usb_net.h"
#include "../../../include/net/netdev.h"
#include "../../../include/memory/dma.h"
#include "../../../include/sched/sched.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/waitq.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/io/serial.h"
#include "../../../include/syscall/errno.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TXQ   64
#define TXMAX 1600

#define CDC_SET_ETHERNET_PACKET_FILTER 0x43
#define CDC_GET_NTB_PARAMETERS         0x80
#define CDC_SET_NTB_INPUT_SIZE         0x86

#define NCM_NTH16_SIGN 0x484D434Eu
#define NCM_NDP16_SIGN 0x304D434Eu

#define RNDIS_PACKET_MSG     0x00000001u
#define RNDIS_INITIALIZE_MSG 0x00000002u
#define RNDIS_QUERY_MSG      0x00000004u
#define RNDIS_SET_MSG        0x00000005u
#define RNDIS_CMPLT          0x80000000u
#define OID_802_3_PERMANENT_ADDRESS   0x01010101u
#define OID_GEN_CURRENT_PACKET_FILTER 0x0001010Eu

struct usbnet {
    usbnet_glue_t   g;
    usb_net_iface_t ifc;
    uint16_t        vid, pid;
    netdev_t       *nd;
    uint8_t         mac[6];

    uint8_t        *rx[USBNET_RX_BUFS];
    uintptr_t       rx_phys[USBNET_RX_BUFS];
    uint32_t        rx_len[USBNET_RX_BUFS];
    uint8_t         rx_ok[USBNET_RX_BUFS];
    int             done[USBNET_RX_BUFS * 2];
    uint32_t        done_head, done_tail;
    uint32_t        rx_size;

    uint8_t        *tx;
    uintptr_t       tx_phys;
    uint8_t         txq[TXQ][TXMAX];
    uint16_t        txq_len[TXQ];
    uint32_t        txq_head, txq_tail;

    spinlock_t      lock;
    waitq_t         wq;
    volatile bool   alive;

    uint16_t        ncm_div, ncm_rem;
    uint32_t        ncm_out_max;
    uint16_t        seq;
    uint32_t        rndis_id;
};

static int g_usbnet_index;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static int net_proto_of(uint8_t cls, uint8_t sub, uint8_t proto) {
    if (cls == 0x02 && sub == 0x06) return USBNET_ECM;
    if (cls == 0x02 && sub == 0x0D) return USBNET_NCM;
    if (cls == 0xE0 && sub == 0x01 && proto == 0x03) return USBNET_RNDIS;
    if (cls == 0x02 && sub == 0x02 && proto == 0xFF) return USBNET_RNDIS;
    if (cls == 0xEF && sub == 0x04 && proto == 0x01) return USBNET_RNDIS;
    return 0;
}

bool usb_parse_net(const uint8_t *buf, uint16_t total, usb_net_iface_t *out) {
    memset(out, 0, sizeof *out);
    if (total < 9) return false;
    out->cfg_value = buf[5];

    int comm = -1, slave = -1, cur = -1, cur_alt = 0;
    for (uint16_t pos = 9; pos + 2 <= total; ) {
        uint8_t len = buf[pos], type = buf[pos + 1];
        if (len < 2 || pos + len > total) break;
        if (type == 0x04 && len >= 9) {
            cur = buf[pos + 2];
            cur_alt = buf[pos + 3];
            int p = net_proto_of(buf[pos + 5], buf[pos + 6], buf[pos + 7]);
            if (comm < 0 && p && cur_alt == 0) {
                comm = cur;
                out->proto = (uint8_t)p;
                out->comm_intf = (uint8_t)cur;
            }
        } else if (type == 0x24 && len >= 3 && cur == comm && comm >= 0) {
            uint8_t sub = buf[pos + 2];
            if (sub == 0x06 && len >= 5 && slave < 0) slave = buf[pos + 4];
            if (sub == 0x0F && len >= 13) {
                out->mac_str = buf[pos + 3];
                out->max_segment = rd16(buf + pos + 8);
            }
        } else if (type == 0x05 && len >= 7 && cur == comm && comm >= 0) {
            uint8_t addr = buf[pos + 2], attr = buf[pos + 3];
            if ((attr & 3) == 3 && (addr & 0x80) && !out->int_ep) {
                out->int_ep = addr & 0x0F;
                out->int_mps = rd16(buf + pos + 4) & 0x7FF;
                out->int_interval = buf[pos + 6];
            }
        }
        pos += len;
    }
    if (comm < 0) return false;
    out->data_intf = (uint8_t)(slave >= 0 ? slave : comm + 1);

    cur = -1;
    int in = 0, outp = 0;
    uint16_t in_mps = 0, out_mps = 0;
    for (uint16_t pos = 9; pos + 2 <= total; ) {
        uint8_t len = buf[pos], type = buf[pos + 1];
        if (len < 2 || pos + len > total) break;
        if (type == 0x04 && len >= 9) {
            cur = buf[pos + 2];
            cur_alt = buf[pos + 3];
            in = outp = 0;
        } else if (type == 0x05 && len >= 7 && cur == out->data_intf) {
            uint8_t addr = buf[pos + 2], attr = buf[pos + 3];
            if ((attr & 3) == 2) {
                uint16_t mps = rd16(buf + pos + 4) & 0x7FF;
                if (addr & 0x80) { in = addr & 0x0F; in_mps = mps; }
                else { outp = addr & 0x0F; out_mps = mps; }
            }
        }
        pos += len;
        if (cur == out->data_intf && in && outp) {
            out->data_alt = (uint8_t)cur_alt;
            out->in_ep = (uint8_t)in;
            out->out_ep = (uint8_t)outp;
            out->in_mps = in_mps;
            out->out_mps = out_mps;
            out->present = true;
            return true;
        }
    }
    return false;
}

static int ctl_out(usbnet_t *u, uint8_t req, uint16_t value, void *data, uint16_t len) {
    return u->g.control(u->g.dev, 0x21, req, value, u->ifc.comm_intf, data, len);
}

static int ctl_in(usbnet_t *u, uint8_t req, uint16_t value, void *data, uint16_t len) {
    return u->g.control(u->g.dev, 0xA1, req, value, u->ifc.comm_intf, data, len);
}

static int hexval(uint16_t ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int read_mac_string(usbnet_t *u) {
    if (!u->ifc.mac_str) return -ENOENT;
    uint8_t s[32];
    memset(s, 0, sizeof s);
    int r = u->g.control(u->g.dev, 0x80, 0x06, (uint16_t)(0x0300 | u->ifc.mac_str), 0x0409, s, 26);
    if (r < 0 || s[1] != 0x03 || s[0] < 26) return -EIO;
    for (int i = 0; i < 6; i++) {
        int hi = hexval(rd16(s + 2 + i * 4)), lo = hexval(rd16(s + 4 + i * 4));
        if (hi < 0 || lo < 0) return -EIO;
        u->mac[i] = (uint8_t)(hi << 4 | lo);
    }
    return 0;
}

static void make_local_mac(usbnet_t *u) {
    uint64_t t = sched_now_ns();
    u->mac[0] = 0x02;
    u->mac[1] = (uint8_t)(u->vid >> 8) ^ (uint8_t)t;
    u->mac[2] = (uint8_t)u->vid;
    u->mac[3] = (uint8_t)(u->pid >> 8);
    u->mac[4] = (uint8_t)u->pid;
    u->mac[5] = (uint8_t)(t >> 8);
}

static int ecm_init(usbnet_t *u) {
    if (read_mac_string(u) < 0) make_local_mac(u);
    if (u->g.set_interface(u->g.dev, u->ifc.data_intf, u->ifc.data_alt) < 0) return -EIO;
    ctl_out(u, CDC_SET_ETHERNET_PACKET_FILTER, 0x000E, NULL, 0);
    u->rx_size = USBNET_BUF;
    return 0;
}

static int ncm_init(usbnet_t *u) {
    if (read_mac_string(u) < 0) make_local_mac(u);
    uint8_t p[28];
    memset(p, 0, sizeof p);
    if (ctl_in(u, CDC_GET_NTB_PARAMETERS, 0, p, sizeof p) < 0) return -EIO;
    uint32_t in_max = rd32(p + 4);
    u->ncm_div = rd16(p + 20) ? rd16(p + 20) : 4;
    u->ncm_rem = rd16(p + 22);
    u->ncm_out_max = rd32(p + 16);
    if (u->ncm_rem >= u->ncm_div) u->ncm_rem = 0;
    u->rx_size = in_max < USBNET_BUF ? in_max : USBNET_BUF;
    if (in_max > USBNET_BUF) {
        uint8_t sz[4];
        wr32(sz, USBNET_BUF);
        if (ctl_out(u, CDC_SET_NTB_INPUT_SIZE, 0, sz, 4) < 0)
            serial_printf("[usbnet] NTB input size %u is larger than %u and cannot be lowered\n",
                          in_max, USBNET_BUF);
    }
    if (u->g.set_interface(u->g.dev, u->ifc.data_intf, u->ifc.data_alt) < 0) return -EIO;
    ctl_out(u, CDC_SET_ETHERNET_PACKET_FILTER, 0x000E, NULL, 0);
    return 0;
}

static int rndis_command(usbnet_t *u, uint8_t *msg, uint32_t len, uint8_t *reply, uint32_t cap) {
    uint32_t type = rd32(msg), id = rd32(msg + 8);
    if (ctl_out(u, 0x00, 0, msg, (uint16_t)len) < 0) return -EIO;
    for (int tries = 0; tries < 50; tries++) {
        memset(reply, 0, cap);
        int r = ctl_in(u, 0x01, 0, reply, (uint16_t)cap);
        if (r == 0 && rd32(reply) == (type | RNDIS_CMPLT) && rd32(reply + 8) == id) {
            if (rd32(reply + 12) != 0) return -EIO;
            return 0;
        }
        task_sleep_ms(20);
    }
    return -ETIMEDOUT;
}

static int rndis_init(usbnet_t *u) {
    uint8_t msg[64], rep[1025];
    memset(msg, 0, sizeof msg);
    wr32(msg + 0, RNDIS_INITIALIZE_MSG);
    wr32(msg + 4, 24);
    wr32(msg + 8, ++u->rndis_id);
    wr32(msg + 12, 1);
    wr32(msg + 16, 0);
    wr32(msg + 20, USBNET_BUF);
    if (rndis_command(u, msg, 24, rep, sizeof rep) < 0) return -EIO;

    memset(msg, 0, sizeof msg);
    wr32(msg + 0, RNDIS_QUERY_MSG);
    wr32(msg + 4, 28);
    wr32(msg + 8, ++u->rndis_id);
    wr32(msg + 12, OID_802_3_PERMANENT_ADDRESS);
    if (rndis_command(u, msg, 28, rep, sizeof rep) == 0) {
        uint32_t blen = rd32(rep + 16), boff = rd32(rep + 20);
        if (blen >= 6 && 8 + boff + 6 <= sizeof rep) memcpy(u->mac, rep + 8 + boff, 6);
        else make_local_mac(u);
    } else {
        make_local_mac(u);
    }

    memset(msg, 0, sizeof msg);
    wr32(msg + 0, RNDIS_SET_MSG);
    wr32(msg + 4, 32);
    wr32(msg + 8, ++u->rndis_id);
    wr32(msg + 12, OID_GEN_CURRENT_PACKET_FILTER);
    wr32(msg + 16, 4);
    wr32(msg + 20, 20);
    wr32(msg + 28, 0x0000000Fu);
    if (rndis_command(u, msg, 32, rep, sizeof rep) < 0) return -EIO;
    u->rx_size = USBNET_BUF;
    return 0;
}

static void deliver(usbnet_t *u, const uint8_t *frame, uint32_t len) {
    if (len < 14 || len > 1518 || !u->nd) return;
    net_rx(u->nd, frame, len);
}

static void rx_parse(usbnet_t *u, const uint8_t *b, uint32_t len) {
    switch (u->ifc.proto) {
    case USBNET_ECM:
        deliver(u, b, len);
        break;
    case USBNET_NCM: {
        if (len < 12 || rd32(b) != NCM_NTH16_SIGN) return;
        uint16_t block = rd16(b + 8);
        if (block > len || block == 0) block = (uint16_t)len;
        uint16_t ndp = rd16(b + 10);
        for (int guard = 0; ndp && guard < 8; guard++) {
            if ((uint32_t)ndp + 8 > block || rd32(b + ndp) != NCM_NDP16_SIGN) return;
            uint16_t ndp_len = rd16(b + ndp + 4);
            uint16_t next = rd16(b + ndp + 6);
            for (uint32_t e = 8; e + 4 <= ndp_len && ndp + e + 4 <= block; e += 4) {
                uint16_t off = rd16(b + ndp + e), dl = rd16(b + ndp + e + 2);
                if (!off || !dl) break;
                if ((uint32_t)off + dl <= block) deliver(u, b + off, dl);
            }
            ndp = next;
        }
        break;
    }
    case USBNET_RNDIS: {
        uint32_t pos = 0;
        while (pos + 44 <= len) {
            uint32_t type = rd32(b + pos), mlen = rd32(b + pos + 4);
            if (mlen < 8 || pos + mlen > len) break;
            if (type == RNDIS_PACKET_MSG) {
                uint32_t doff = rd32(b + pos + 8), dlen = rd32(b + pos + 12);
                if (8 + doff + dlen <= mlen) deliver(u, b + pos + 8 + doff, dlen);
            }
            pos += mlen;
        }
        break;
    }
    }
}

static int tx_frame(usbnet_t *u, const uint8_t *frame, uint32_t len) {
    uint8_t *t = u->tx;
    uint32_t total;
    switch (u->ifc.proto) {
    case USBNET_NCM: {
        uint32_t ndp = 12, off = ndp + 16;
        while (off % u->ncm_div != u->ncm_rem) off++;
        total = off + len;
        memset(t, 0, off);
        wr32(t + 0, NCM_NTH16_SIGN);
        wr16(t + 4, 12);
        wr16(t + 6, u->seq++);
        wr16(t + 8, (uint16_t)total);
        wr16(t + 10, (uint16_t)ndp);
        wr32(t + ndp, NCM_NDP16_SIGN);
        wr16(t + ndp + 4, 16);
        wr16(t + ndp + 6, 0);
        wr16(t + ndp + 8, (uint16_t)off);
        wr16(t + ndp + 10, (uint16_t)len);
        memcpy(t + off, frame, len);
        break;
    }
    case USBNET_RNDIS:
        total = 44 + len;
        memset(t, 0, 44);
        wr32(t + 0, RNDIS_PACKET_MSG);
        wr32(t + 4, total);
        wr32(t + 8, 36);
        wr32(t + 12, len);
        memcpy(t + 44, frame, len);
        break;
    default:
        total = len;
        memcpy(t, frame, len);
        break;
    }
    if (u->ifc.out_mps && total % u->ifc.out_mps == 0) t[total++] = 0;
    int r = u->g.bulk_out(u->g.dev, u->tx_phys, total);
    if (r < 0) { u->nd->tx_dropped++; return r; }
    u->nd->tx_packets++;
    u->nd->tx_bytes += len;
    return 0;
}

static int usbnet_transmit(netdev_t *nd, const void *frame, size_t len) {
    usbnet_t *u = nd->priv;
    if (!u || !u->alive || len > TXMAX) { nd->tx_dropped++; return -EIO; }
    uint64_t f = spinlock_acquire_irqsave(&u->lock);
    uint32_t next = (u->txq_tail + 1) % TXQ;
    if (next == u->txq_head) {
        spinlock_release_irqrestore(&u->lock, f);
        nd->tx_dropped++;
        return -ENOBUFS;
    }
    memcpy(u->txq[u->txq_tail], frame, len);
    u->txq_len[u->txq_tail] = (uint16_t)len;
    u->txq_tail = next;
    waitq_wake_all(&u->wq);
    spinlock_release_irqrestore(&u->lock, f);
    return 0;
}

void usbnet_rx_done(usbnet_t *u, int idx, uint32_t actual, bool ok) {
    if (!u || idx < 0 || idx >= USBNET_RX_BUFS) return;
    uint64_t f = spinlock_acquire_irqsave(&u->lock);
    u->rx_len[idx] = actual;
    u->rx_ok[idx] = ok ? 1 : 0;
    u->done[u->done_tail % (USBNET_RX_BUFS * 2)] = idx;
    u->done_tail++;
    waitq_wake_all(&u->wq);
    spinlock_release_irqrestore(&u->lock, f);
}

static int pop_done(usbnet_t *u) {
    uint64_t f = spinlock_acquire_irqsave(&u->lock);
    int idx = -1;
    if (u->done_head != u->done_tail) {
        idx = u->done[u->done_head % (USBNET_RX_BUFS * 2)];
        u->done_head++;
    }
    spinlock_release_irqrestore(&u->lock, f);
    return idx;
}

static int pop_tx(usbnet_t *u, uint8_t *frame) {
    uint64_t f = spinlock_acquire_irqsave(&u->lock);
    int len = 0;
    if (u->txq_head != u->txq_tail) {
        len = u->txq_len[u->txq_head];
        memcpy(frame, u->txq[u->txq_head], (size_t)len);
        u->txq_head = (u->txq_head + 1) % TXQ;
    }
    spinlock_release_irqrestore(&u->lock, f);
    return len;
}

static void usbnet_thread(void *arg) {
    usbnet_t *u = arg;
    uint8_t frame[TXMAX];
    while (u->alive) {
        bool work = false;
        int idx;
        if (u->g.polled && u->g.poll) u->g.poll(u->g.dev);
        while ((idx = pop_done(u)) >= 0) {
            work = true;
            if (u->rx_ok[idx]) {
                rx_parse(u, u->rx[idx], u->rx_len[idx]);
            } else {
                u->nd->rx_dropped++;
            }
            if (u->alive) u->g.rx_post(u->g.dev, idx, u->rx_phys[idx], u->rx_size);
        }
        int len;
        while (u->alive && (len = pop_tx(u, frame)) > 0) {
            work = true;
            tx_frame(u, frame, (uint32_t)len);
        }
        if (work) continue;
        if (u->g.poll) u->g.poll(u->g.dev);
        uint64_t f = spinlock_acquire_irqsave(&u->lock);
        if (!u->alive || u->done_head != u->done_tail || u->txq_head != u->txq_tail) {
            spinlock_release_irqrestore(&u->lock, f);
            continue;
        }
        waitq_block(&u->wq, &u->lock, f, sched_now_ns() + (u->g.polled ? 1000000ULL : 20000000ULL));
    }
    task_exit();
}

usbnet_t *usbnet_create(const usbnet_glue_t *g, const usb_net_iface_t *ifc,
                        uint16_t vid, uint16_t pid) {
    usbnet_t *u = calloc(1, sizeof *u);
    if (!u) return NULL;
    u->g = *g;
    u->ifc = *ifc;
    u->vid = vid;
    u->pid = pid;
    u->lock = (spinlock_t)SPINLOCK_INIT;
    for (int i = 0; i < USBNET_RX_BUFS; i++) {
        u->rx[i] = dma_alloc_coherent_low(USBNET_BUF, &u->rx_phys[i]);
        if (!u->rx[i] || u->rx_phys[i] >= 0xFFFFFFFFULL) return NULL;
    }
    u->tx = dma_alloc_coherent_low(USBNET_BUF, &u->tx_phys);
    if (!u->tx || u->tx_phys >= 0xFFFFFFFFULL) return NULL;
    return u;
}

static const char *proto_name(int p) {
    return p == USBNET_ECM ? "CDC-ECM" : p == USBNET_NCM ? "CDC-NCM" : "RNDIS";
}

int usbnet_start(usbnet_t *u) {
    int r;
    switch (u->ifc.proto) {
    case USBNET_ECM:   r = ecm_init(u); break;
    case USBNET_NCM:   r = ncm_init(u); break;
    case USBNET_RNDIS: r = rndis_init(u); break;
    default:           r = -ENODEV; break;
    }
    if (r < 0) {
        serial_printf("[usbnet] %04x:%04x: %s setup failed (%d)\n", u->vid, u->pid,
                      proto_name(u->ifc.proto), r);
        return r;
    }

    char name[NETDEV_NAME_MAX];
    netdev_t *nd = NULL;
    for (int i = 0; i < 16 && !nd; i++) {
        snprintf(name, sizeof name, "usbeth%d", i);
        netdev_t *old = netdev_get(name);
        if (old && !old->link_up && old->transmit == usbnet_transmit) {
            nd = old;
            memcpy(nd->mac, u->mac, 6);
            nd->priv = u;
        } else if (!old && i >= g_usbnet_index) {
            nd = netdev_register_named(name, u->mac, 1500, usbnet_transmit, u);
            g_usbnet_index = i + 1;
        }
    }
    if (!nd) return -ENOMEM;
    u->nd = nd;
    u->alive = true;
    for (int i = 0; i < USBNET_RX_BUFS; i++)
        u->g.rx_post(u->g.dev, i, u->rx_phys[i], u->rx_size);
    task_create("usbnet", usbnet_thread, u, IRQ_THREAD_PRIORITY);
    nd->link_up = 1;
    LOG_I("usbnet: %s %04x:%04x as %s, %02x:%02x:%02x:%02x:%02x:%02x\n",
           proto_name(u->ifc.proto), u->vid, u->pid, nd->name,
           u->mac[0], u->mac[1], u->mac[2], u->mac[3], u->mac[4], u->mac[5]);
    serial_printf("[usbnet] %s %04x:%04x as %s, rx %u bytes per transfer\n",
                  proto_name(u->ifc.proto), u->vid, u->pid, nd->name, u->rx_size);
    return 0;
}

void usbnet_stop(usbnet_t *u) {
    if (!u) return;
    u->alive = false;
    if (u->nd) u->nd->link_up = 0;
    uint64_t f = spinlock_acquire_irqsave(&u->lock);
    waitq_wake_all(&u->wq);
    spinlock_release_irqrestore(&u->lock, f);
}

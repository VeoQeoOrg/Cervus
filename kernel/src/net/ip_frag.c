#include "../../include/net/ip.h"
#include "../../include/net/icmp.h"
#include "../../include/net/net.h"
#include "../../include/net/netdev.h"
#include "../../include/sched/spinlock.h"
#include <string.h>
#include <stdlib.h>

extern uint64_t sched_now_ns(void);

#define FRAG_SLOTS       8
#define FRAG_MAXLEN      65535
#define FRAG_UNITS       (FRAG_MAXLEN / 8 + 1)
#define FRAG_BMAP        (FRAG_UNITS / 8)
#define FRAG_TIMEOUT_NS  30000000000ULL

typedef struct {
    int       used;
    netdev_t *dev;
    uint32_t  src, dst;
    uint16_t  id;
    uint8_t   proto;
    uint8_t   have_first;
    uint8_t   first_hdr[68];
    uint32_t  first_hdr_len;
    uint32_t  total;
    uint32_t  filled;
    uint64_t  deadline;
    uint8_t  *buf;
    uint8_t   bmap[FRAG_BMAP];
} frag_t;

static frag_t     g_frags[FRAG_SLOTS];
static spinlock_t g_lock = SPINLOCK_INIT;

static void slot_reset(frag_t *f)
{
    f->used = 0;
    f->have_first = 0;
    f->total = 0;
    f->filled = 0;
    f->first_hdr_len = 0;
    memset(f->bmap, 0, sizeof(f->bmap));
}

static frag_t *slot_find(uint32_t src, uint32_t dst, uint16_t id, uint8_t proto)
{
    for (int i = 0; i < FRAG_SLOTS; i++) {
        frag_t *f = &g_frags[i];
        if (f->used && f->src == src && f->dst == dst && f->id == id && f->proto == proto)
            return f;
    }
    return NULL;
}

static frag_t *slot_claim(uint64_t now)
{
    frag_t *oldest = NULL;
    for (int i = 0; i < FRAG_SLOTS; i++) {
        frag_t *f = &g_frags[i];
        if (!f->used) {
            if (!f->buf) {
                f->buf = malloc(FRAG_MAXLEN);
                if (!f->buf) return NULL;
            }
            slot_reset(f);
            f->used = 1;
            f->deadline = now + FRAG_TIMEOUT_NS;
            return f;
        }
        if (!oldest || f->deadline < oldest->deadline) oldest = f;
    }
    if (!oldest) return NULL;
    if (oldest->dev) oldest->dev->rx_frag_dropped++;
    slot_reset(oldest);
    oldest->used = 1;
    oldest->deadline = now + FRAG_TIMEOUT_NS;
    return oldest;
}

static int bmap_fill(frag_t *f, uint32_t off, uint32_t len, const uint8_t *data)
{
    uint32_t first = off / 8;
    uint32_t last  = (off + len + 7) / 8;
    uint32_t added = 0;

    for (uint32_t u = first; u < last; u++) {
        if (f->bmap[u >> 3] & (1u << (u & 7))) continue;
        f->bmap[u >> 3] |= (uint8_t)(1u << (u & 7));
        added++;

        uint32_t b = u * 8;
        uint32_t n = 8;
        if (b < off) { n -= off - b; b = off; }
        if (b + n > off + len) n = off + len - b;
        memcpy(f->buf + b, data + (b - off), n);
    }
    f->filled += added;
    return (int)added;
}

void ip_frag_input(netdev_t *dev, const uint8_t *pkt, size_t total)
{
    uint32_t ihl = (uint32_t)(pkt[0] & 0x0f) * 4;
    if (ihl < 20 || total < ihl) return;

    uint16_t frag  = rd16be(pkt + 6);
    uint32_t off   = (uint32_t)(frag & 0x1FFF) * 8;
    int      more  = (frag & 0x2000) != 0;
    uint32_t plen  = (uint32_t)total - ihl;

    if (plen == 0 && more) return;
    if (off + plen > FRAG_MAXLEN) { dev->rx_frag_dropped++; return; }
    if (more && (plen & 7)) { dev->rx_frag_dropped++; return; }

    uint32_t src   = rd32be(pkt + 12);
    uint32_t dst   = rd32be(pkt + 16);
    uint16_t id    = rd16be(pkt + 4);
    uint8_t  proto = pkt[9];

    uint64_t now = sched_now_ns();

    uint8_t *done_buf  = NULL;
    uint32_t done_len  = 0;

    uint64_t irq = spinlock_acquire_irqsave(&g_lock);

    frag_t *f = slot_find(src, dst, id, proto);
    if (!f) {
        f = slot_claim(now);
        if (!f) { spinlock_release_irqrestore(&g_lock, irq); dev->rx_frag_dropped++; return; }
        f->dev = dev; f->src = src; f->dst = dst; f->id = id; f->proto = proto;
    }

    if (!more) {
        uint32_t end = off + plen;
        if (f->total && f->total != end) {
            slot_reset(f);
            spinlock_release_irqrestore(&g_lock, irq);
            dev->rx_frag_dropped++;
            return;
        }
        f->total = end;
    }

    if (off == 0 && !f->have_first) {
        uint32_t keep = plen < 8 ? plen : 8;
        memcpy(f->first_hdr, pkt, ihl);
        memcpy(f->first_hdr + ihl, pkt + ihl, keep);
        f->first_hdr_len = ihl + keep;
        f->have_first = 1;
    }

    bmap_fill(f, off, plen, pkt + ihl);

    if (f->total && f->filled == (f->total + 7) / 8) {
        done_buf = f->buf;
        done_len = f->total;
        f->buf = NULL;
        slot_reset(f);
    }

    spinlock_release_irqrestore(&g_lock, irq);

    if (done_buf) {
        dev->rx_frag_reasm++;
        ip_deliver(dev, src, dst, proto, done_buf, done_len);
        free(done_buf);
    }
}

void ip_frag_tick(void)
{
    uint64_t now = sched_now_ns();

    for (int i = 0; i < FRAG_SLOTS; i++) {
        uint8_t   hdr[68];
        uint32_t  hdr_len = 0;
        netdev_t *dev = NULL;
        uint32_t  src = 0;

        uint64_t irq = spinlock_acquire_irqsave(&g_lock);
        frag_t *f = &g_frags[i];
        if (f->used && (int64_t)(now - f->deadline) >= 0) {
            if (f->have_first) {
                memcpy(hdr, f->first_hdr, f->first_hdr_len);
                hdr_len = f->first_hdr_len;
                dev = f->dev;
                src = f->src;
            }
            if (f->dev) f->dev->rx_frag_timeout++;
            slot_reset(f);
        }
        spinlock_release_irqrestore(&g_lock, irq);

        if (hdr_len && dev)
            icmp_send_error(dev, src, ICMP_TIME_EXCEEDED, ICMP_TE_REASSEMBLY,
                            hdr, hdr_len);
    }
}

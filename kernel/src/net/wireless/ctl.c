#include "wlan.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/devfs.h"
#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/security/auth.h"
#include "../../../include/drivers/timer.h"

#define SCAN_WAIT_NS 6000000000ULL
#define CMD_WAIT_NS  3000000000ULL

static int netdev_xmit(netdev_t *dev, const void *frame, size_t len)
{
    wlan_local_t *l = dev->priv;
    if (!l || !l->authorized || len < 14) return -1;
    const u8 *f = frame;
    u16 et = (u16)((f[12] << 8) | f[13]);
    return wlan_tx_data(l, f, f + 6, et, f + 14, len - 14, false) < 0 ? -1 : 0;
}

void wlan_netdev_rx(wlan_local_t *l, const u8 *frame, size_t len)
{
    if (l->ndev) net_rx(l->ndev, frame, len);
}

int wlan_netdev_register(wlan_local_t *l)
{
    l->ndev = netdev_register_named(l->name, l->wiphy.perm_addr, 1500, netdev_xmit, l);
    if (!l->ndev) return -ENOMEM;
    l->ndev->link_up = 0;
    return 0;
}

void wlan_cmd_finish(wlan_local_t *l, int result)
{
    l->cmd.result = result;
    __atomic_store_n(&l->cmd.done, 1, __ATOMIC_RELEASE);
    kpi_wake_up(&l->cmd.wait);
}

static int run_cmd(wlan_local_t *l, enum wlan_cmd_kind kind, const struct wlan_connect_req *c, u64 timeout)
{
    kmutex_lock(&l->cmd.serial);
    if (c) memcpy(&l->cmd.conn, c, sizeof(*c));
    else memset(&l->cmd.conn, 0, sizeof(l->cmd.conn));
    l->cmd.kind = kind;
    l->cmd.result = 0;
    __atomic_store_n(&l->cmd.done, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&l->cmd.busy, 1, __ATOMIC_RELEASE);
    wlan_kick(l);
    u64 end = sched_now_ns() + timeout;
    int r;
    for (;;) {
        u32 seq = __atomic_load_n(&l->cmd.wait.seq, __ATOMIC_ACQUIRE);
        if (__atomic_load_n(&l->cmd.done, __ATOMIC_ACQUIRE)) { r = l->cmd.result; break; }
        u64 now = sched_now_ns();
        if (now >= end) { r = -ETIMEDOUT; break; }
        kpi_wait_on(&l->cmd.wait, seq, end - now);
    }
    __atomic_store_n(&l->cmd.busy, 0, __ATOMIC_RELEASE);
    l->cmd.kind = WLAN_CMD_NONE;
    crypto_wipe(&l->cmd.conn, sizeof(l->cmd.conn));
    kmutex_unlock(&l->cmd.serial);
    return r;
}

static bool may_control(void)
{
    task_t *t = syscall_cur_task();
    if (!t || t->uid == 0) return true;
    return t->uid == seat_owner_uid();
}

static u32 state_code(wlan_local_t *l)
{
    if (!l->started) return WLAN_STATE_DOWN;
    switch (l->state) {
    case WLAN_ST_AUTH:      return WLAN_STATE_AUTHENTICATING;
    case WLAN_ST_ASSOC:     return WLAN_STATE_ASSOCIATING;
    case WLAN_ST_HANDSHAKE: return WLAN_STATE_HANDSHAKE;
    case WLAN_ST_CONNECTED: return WLAN_STATE_CONNECTED;
    default:                return l->scanning ? WLAN_STATE_SCANNING : WLAN_STATE_IDLE;
    }
}

static void fill_status(wlan_local_t *l, struct wlan_status *s)
{
    memset(s, 0, sizeof(*s));
    s->state = state_code(l);
    if (l->state != WLAN_ST_IDLE) {
        memcpy(s->bssid, l->cur.bssid, 6);
        s->ssid_len = l->cur.ssid_len;
        memcpy(s->ssid, l->cur.ssid, l->cur.ssid_len);
        s->channel = l->cur.channel;
        s->freq = l->cur.freq;
        s->security = l->cur.security;
        s->signal = l->last_signal ? l->last_signal : l->cur.signal;
    } else if (l->want_connected) {
        s->ssid_len = l->target.ssid_len;
        memcpy(s->ssid, l->target.ssid, l->target.ssid_len);
    }
    s->authorized = l->authorized;
    if (l->authorized) s->connected_ms = (u32)((sched_now_ns() - l->connected_ns) / 1000000ULL);
    s->last_reason = l->last_reason;
    s->last_error = l->last_error;
    s->tx_rate = l->last_rate;
    memcpy(s->message, l->last_msg, sizeof(s->message) - 1);
    memcpy(s->mac, l->vif ? l->vif->addr : l->wiphy.perm_addr, 6);
    strncpy(s->ifname, l->name, sizeof(s->ifname) - 1);
    strncpy(s->driver, l->hw.kpi_driver ? l->hw.kpi_driver : "wlan", sizeof(s->driver) - 1);
    strncpy(s->firmware, l->wiphy.fw_version, sizeof(s->firmware) - 1);
}

static int copy_results(wlan_local_t *l, struct wlan_scan_results *req)
{
    struct wlan_bss_info info;
    u32 n = 0;
    u64 now = sched_now_ns();
    for (int i = 0; i < WLAN_MAX_BSS; i++) {
        if (n >= req->max) break;
        uint64_t f = spinlock_acquire_irqsave(&l->bss_lock);
        wlan_bss_t *b = &l->bss[i];
        bool use = b->valid && now - b->last_seen_ns < WLAN_BSS_EXPIRE_NS;
        if (use) {
            memset(&info, 0, sizeof(info));
            memcpy(info.bssid, b->bssid, 6);
            info.ssid_len = b->ssid_len;
            memcpy(info.ssid, b->ssid, b->ssid_len);
            info.channel = b->channel;
            info.signal = b->signal;
            info.freq = b->freq;
            info.security = b->security;
            info.age_ms = (u32)((now - b->last_seen_ns) / 1000000ULL);
            info.capab = b->capab;
            info.beacon_int = b->beacon_int;
            if (wlan_find_ie(b->ies, b->ie_len, WLAN_EID_HT_CAPABILITY)) info.flags |= WLAN_BSS_HT;
            if (wlan_find_vendor_ie(b->ies, b->ie_len, 0x0050F2, 2)) info.flags |= WLAN_BSS_WMM;
            if (l->state != WLAN_ST_IDLE && ether_addr_equal(b->bssid, l->cur.bssid))
                info.flags |= WLAN_BSS_CONNECTED;
        }
        spinlock_release_irqrestore(&l->bss_lock, f);
        if (!use) continue;
        if (syscall_copy_to_user((u8 *)(uintptr_t)req->buf + (size_t)n * sizeof(info), &info, sizeof(info)) < 0)
            return -EFAULT;
        n++;
    }
    req->count = n;
    return 0;
}

static int64_t ctl_ioctl(vnode_t *node, uint64_t req, void *arg)
{
    wlan_local_t *l = node->fs_data;
    if (!l) return -ENODEV;
    switch (req) {
    case WLAN_IOC_STATUS:
        if (!arg) return -EFAULT;
        fill_status(l, arg);
        return 0;
    case WLAN_IOC_RESULTS:
        if (!arg) return -EFAULT;
        return copy_results(l, arg);
    case WLAN_IOC_SCAN: {
        if (!l->started) return -ENETDOWN;
        struct wlan_connect_req c;
        memset(&c, 0, sizeof(c));
        if (arg) {
            struct wlan_scan_req *s = arg;
            c.ssid_len = s->ssid_len > 32 ? 32 : s->ssid_len;
            memcpy(c.ssid, s->ssid, c.ssid_len);
            if (c.ssid_len && !may_control()) c.ssid_len = 0;
        }
        return run_cmd(l, WLAN_CMD_SCAN, &c, SCAN_WAIT_NS);
    }
    case WLAN_IOC_CONNECT: {
        if (!may_control()) return -EPERM;
        if (!arg) return -EFAULT;
        struct wlan_connect_req *c = arg;
        if (!c->ssid_len || c->ssid_len > 32) return -EINVAL;
        int r = run_cmd(l, WLAN_CMD_CONNECT, c, CMD_WAIT_NS);
        crypto_wipe(c, sizeof(*c));
        return r;
    }
    case WLAN_IOC_DISCONNECT:
        if (!may_control()) return -EPERM;
        return run_cmd(l, WLAN_CMD_DISCONNECT, NULL, CMD_WAIT_NS);
    default:
        return -ENOTTY;
    }
}

static int ctl_stat(vnode_t *n, vfs_stat_t *out)
{
    memset(out, 0, sizeof(*out));
    out->st_ino = n->ino;
    out->st_type = VFS_NODE_CHARDEV;
    out->st_mode = 0666 | 0020000;
    out->st_nlink = 1;
    out->st_rdev = n->rdev;
    return 0;
}

static void ctl_ref(vnode_t *n)   { (void)n; }
static void ctl_unref(vnode_t *n) { (void)n; }

static const vnode_ops_t g_ctl_ops = {
    .stat  = ctl_stat,
    .ref   = ctl_ref,
    .unref = ctl_unref,
    .ioctl = ctl_ioctl,
};

static vnode_t g_nodes[4];
static int     g_nnodes;

int wlan_ctl_register(wlan_local_t *l)
{
    if (g_nnodes >= 4) return -ENOSPC;
    vnode_t *n = &g_nodes[g_nnodes];
    memset(n, 0, sizeof(*n));
    n->type = VFS_NODE_CHARDEV;
    n->mode = 0666;
    n->ino = 760 + (uint64_t)g_nnodes;
    n->rdev = vfs_makedev(239, (uint32_t)g_nnodes);
    n->ops = &g_ctl_ops;
    n->fs_data = l;
    n->refcount = 1;
    g_nnodes++;
    devfs_register(l->name, n);
    return 0;
}

#include "wlan.h"
#include "../../../include/sched/sched.h"
#include <stdio.h>

static int g_nphy;

static const u8 g_tid_to_ac[8] = {
    IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_BK, IEEE80211_AC_BE,
    IEEE80211_AC_VI, IEEE80211_AC_VI, IEEE80211_AC_VO, IEEE80211_AC_VO,
};

int ieee80211_channel_to_frequency(int chan, enum nl80211_band band)
{
    if (chan <= 0) return 0;
    if (band == NL80211_BAND_2GHZ) {
        if (chan == 14) return 2484;
        if (chan < 14) return 2407 + chan * 5;
        return 0;
    }
    if (band == NL80211_BAND_5GHZ) {
        if (chan >= 182 && chan <= 196) return 4000 + chan * 5;
        return 5000 + chan * 5;
    }
    if (band == NL80211_BAND_6GHZ) {
        if (chan == 2) return 5935;
        if (chan <= 233) return 5950 + chan * 5;
    }
    return 0;
}

int ieee80211_frequency_to_channel(int freq)
{
    if (freq == 2484) return 14;
    if (freq < 2484) return (freq - 2407) / 5;
    if (freq >= 4910 && freq <= 4980) return (freq - 4000) / 5;
    if (freq < 5925) return (freq - 5000) / 5;
    if (freq == 5935) return 2;
    if (freq <= 45000) return (freq - 5950) / 5;
    return 0;
}

int wlan_freq_to_channel(int freq)
{
    return ieee80211_frequency_to_channel(freq);
}

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len, const struct ieee80211_ops *ops)
{
    wlan_local_t *l = kzalloc(sizeof(*l) + priv_data_len, GFP_KERNEL);
    if (!l) return NULL;
    l->ops = ops;
    l->hw.wiphy = &l->wiphy;
    l->hw.priv = l->priv;
    l->hw.kpi_ops = ops;
    l->hw.kpi_local = l;
    l->hw.queues = 4;
    l->hw.max_rates = 1;
    l->hw.max_listen_interval = 10;
    l->hw.conf.listen_interval = 10;
    l->hw.conf.long_frame_max_tx_count = 4;
    l->hw.conf.short_frame_max_tx_count = 7;
    l->wiphy.hw_back = &l->hw;
    l->wiphy.rts_threshold = (u32)-1;
    l->wiphy.priv = l;
    spin_lock_init(&l->lock);
    spin_lock_init(&l->bss_lock);
    kpi_waitq_init(&l->events);
    kpi_waitq_init(&l->cmd.wait);
    kmutex_init(&l->cmd.serial);
    skb_queue_head_init(&l->rx_q);
    skb_queue_head_init(&l->status_q);
    snprintf(l->name, sizeof(l->name), "wlan%d", g_nphy++);
    return &l->hw;
}

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
    if (!hw) return;
    kfree(hw_to_local(hw));
}

struct ieee80211_txq *wlan_txq_alloc(wlan_local_t *l, struct ieee80211_vif *vif,
                                       struct ieee80211_sta *sta, u8 tid)
{
    struct ieee80211_txq *q = kzalloc(sizeof(*q) + l->hw.txq_data_size, GFP_KERNEL);
    if (!q) return NULL;
    q->vif = vif;
    q->sta = sta;
    q->tid = tid;
    q->ac = tid < 8 ? g_tid_to_ac[tid] : IEEE80211_AC_VO;
    skb_queue_head_init(&q->frags);
    INIT_LIST_HEAD(&q->kpi_list);
    return q;
}

void wlan_txq_free(struct ieee80211_txq *q)
{
    if (!q) return;
    skb_queue_purge(&q->frags);
    kfree(q);
}

static void pick_channels(wlan_local_t *l)
{
    l->nchans = 0;
    for (int b = 0; b < NUM_NL80211_BANDS; b++) {
        struct ieee80211_supported_band *sb = l->wiphy.bands[b];
        if (!sb) continue;
        if (!l->band) l->band = sb;
        for (int i = 0; i < sb->n_channels && l->nchans < WLAN_MAX_CHANNELS; i++) {
            struct ieee80211_channel *c = &sb->channels[i];
            if (c->flags & IEEE80211_CHAN_DISABLED) continue;
            c->band = (enum nl80211_band)b;
            l->chans[l->nchans++] = c;
        }
    }
}

int kpi_regulatory_hint(struct wiphy *w, const char *alpha2)
{
    wlan_local_t *l = w->priv;
    static const char *const us_like[] = { "US", "CA", "TW", "MX", "PR", "PH", "CO", "BR", "AR", "UY" };
    bool limit11 = false, jp = false, world = false;
    if (!alpha2 || (alpha2[0] == '0' && alpha2[1] == '0')) world = true;
    for (size_t i = 0; !world && i < sizeof(us_like) / sizeof(us_like[0]); i++)
        if (alpha2[0] == us_like[i][0] && alpha2[1] == us_like[i][1]) limit11 = true;
    if (alpha2 && alpha2[0] == 'J' && alpha2[1] == 'P') jp = true;
    for (int b = 0; b < NUM_NL80211_BANDS; b++) {
        struct ieee80211_supported_band *sb = w->bands[b];
        if (!sb || b != NL80211_BAND_2GHZ) continue;
        for (int i = 0; i < sb->n_channels; i++) {
            struct ieee80211_channel *c = &sb->channels[i];
            int ch = c->hw_value;
            c->flags &= ~(IEEE80211_CHAN_DISABLED | IEEE80211_CHAN_NO_IR);
            if (ch == 14 && !jp) c->flags |= IEEE80211_CHAN_DISABLED;
            if (ch >= 12 && ch <= 13) {
                if (limit11) c->flags |= IEEE80211_CHAN_DISABLED;
                else if (world) c->flags |= IEEE80211_CHAN_NO_IR;
            }
        }
    }
    if (l) {
        WLAN_LOG(l, "%s: regulatory domain %c%c\n", l->name,
                 alpha2 ? alpha2[0] : '0', alpha2 ? alpha2[1] : '0');
        if (l->registered) pick_channels(l);
    }
    return 0;
}

static int bring_up(wlan_local_t *l)
{
    int r = l->ops->start(&l->hw);
    if (r) {
        WLAN_LOG(l, "%s: driver start failed: %d\n", l->name, r);
        return r;
    }
    l->started = true;
    struct ieee80211_vif *vif = l->vif;
    r = l->ops->add_interface(&l->hw, vif);
    if (r) {
        WLAN_LOG(l, "%s: add_interface failed: %d\n", l->name, r);
        l->ops->stop(&l->hw, false);
        l->started = false;
        return r;
    }
    unsigned int flags = 0;
    if (l->ops->configure_filter)
        l->ops->configure_filter(&l->hw, FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC, &flags, 0);
    if (l->ops->conf_tx) {
        for (u16 ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
            struct ieee80211_tx_queue_params p = { .txop = 0, .cw_min = 15, .cw_max = 1023, .aifs = 2 };
            l->ops->conf_tx(&l->hw, vif, 0, ac, &p);
        }
    }
    if (l->nchans) wlan_set_channel(l, l->chans[0]);
    return 0;
}

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
    wlan_local_t *l = hw_to_local(hw);
    if (l->registered) return -EBUSY;
    if (!l->ops->start || !l->ops->stop || !l->ops->add_interface || !l->ops->config) return -EINVAL;
    if (is_zero_ether_addr(l->wiphy.perm_addr)) return -EINVAL;
    bool hinted = false;
    for (int i = 0; i < NUM_NL80211_BANDS; i++) if (l->wiphy.bands[i]) hinted = true;
    if (!hinted) return -EINVAL;
    pick_channels(l);
    if (!l->nchans) return -EINVAL;
    struct ieee80211_vif *vif = kzalloc(sizeof(*vif) + hw->vif_data_size, GFP_KERNEL);
    if (!vif) return -ENOMEM;
    vif->type = NL80211_IFTYPE_STATION;
    vif->kpi_owner = l;
    ether_addr_copy(vif->addr, l->wiphy.perm_addr);
    vif->bss_conf.bssid = vif->bss_conf.bssid_storage;
    vif->txq = wlan_txq_alloc(l, vif, NULL, 0);
    if (!vif->txq) { kfree(vif); return -ENOMEM; }
    l->vif = vif;
    l->hw_wq = kpi_alloc_workqueue(l->name);
    l->registered = true;
    int r = bring_up(l);
    if (r) {
        l->registered = false;
        wlan_txq_free(vif->txq);
        kfree(vif);
        l->vif = NULL;
        return r;
    }
    r = wlan_netdev_register(l);
    if (r) return r;
    wlan_ctl_register(l);
    l->thread = task_create(l->name, wlan_worker, l, 17);
    u8 *m = l->wiphy.perm_addr;
    printf("%s: %s, address %02x:%02x:%02x:%02x:%02x:%02x, firmware %s\n", l->name,
           l->band && l->band->ht_cap.ht_supported ? "802.11bgn" : "802.11bg",
           m[0], m[1], m[2], m[3], m[4], m[5], l->wiphy.fw_version[0] ? l->wiphy.fw_version : "-");
    WLAN_LOG(l, "%s: registered, %d channels\n", l->name, l->nchans);
    return 0;
}

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
    wlan_local_t *l = hw_to_local(hw);
    if (!l->registered) return;
    wlan_mlme_disconnect(l, WLAN_REASON_DEAUTH_LEAVING, true, "device removed");
    if (l->started) {
        l->ops->remove_interface(&l->hw, l->vif);
        l->ops->stop(&l->hw, false);
        l->started = false;
    }
    if (l->ndev) l->ndev->link_up = 0;
    l->registered = false;
}

void wlan_kick(wlan_local_t *l)
{
    kpi_wake_up(&l->events);
}

void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta, struct sk_buff *skb, void *napi)
{
    (void)sta; (void)napi;
    wlan_local_t *l = hw_to_local(hw);
    if (skb_queue_len(&l->rx_q) > 512) {
        kpi_free_skb(skb);
        if (l->ndev) l->ndev->rx_dropped++;
        return;
    }
    skb_queue_tail(&l->rx_q, skb);
    wlan_kick(l);
}

void ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
    ieee80211_rx_napi(hw, NULL, skb, NULL);
}

void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
    wlan_local_t *l = hw_to_local(hw);
    if (!skb->kpi_cookie) {
        kpi_free_skb(skb);
        return;
    }
    skb_queue_tail(&l->status_q, skb);
    wlan_kick(l);
}

void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb)
{
    (void)hw;
    kpi_free_skb(skb);
}

void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{
    wlan_local_t *l = hw_to_local(hw);
    kpi_queue_work(l->hw_wq ? l->hw_wq : system_wq, work);
}

void ieee80211_queue_delayed_work(struct ieee80211_hw *hw, struct delayed_work *dwork, unsigned long delay)
{
    wlan_local_t *l = hw_to_local(hw);
    kpi_queue_delayed_work(l->hw_wq ? l->hw_wq : system_wq, dwork, delay);
}

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
    wlan_local_t *l = hw_to_local(hw);
    __atomic_store_n(&l->stopped_queues, 0xf, __ATOMIC_RELEASE);
}

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
    wlan_local_t *l = hw_to_local(hw);
    __atomic_store_n(&l->stopped_queues, 0, __ATOMIC_RELEASE);
}

void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
    wlan_local_t *l = hw_to_local(hw);
    __atomic_or_fetch(&l->stopped_queues, 1UL << queue, __ATOMIC_ACQ_REL);
}

void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
    wlan_local_t *l = hw_to_local(hw);
    __atomic_and_fetch(&l->stopped_queues, ~(1UL << queue), __ATOMIC_ACQ_REL);
}

struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
    wlan_local_t *l = hw_to_local(hw);
    if (__atomic_load_n(&l->stopped_queues, __ATOMIC_ACQUIRE) & (1UL << txq->ac)) return NULL;
    return skb_dequeue(&txq->frags);
}

void ieee80211_txq_get_depth(struct ieee80211_txq *txq, unsigned long *frame_cnt, unsigned long *byte_cnt)
{
    unsigned long n = 0, b = 0;
    uint64_t f = spinlock_acquire_irqsave(&txq->frags.lock);
    struct sk_buff *s;
    skb_queue_walk(&txq->frags, s) { n++; b += s->len; }
    spinlock_release_irqrestore(&txq->frags.lock, f);
    if (frame_cnt) *frame_cnt = n;
    if (byte_cnt) *byte_cnt = b;
}

void ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw, u32 iter_flags,
        void (*iterator)(void *data, u8 *mac, struct ieee80211_vif *vif), void *data)
{
    (void)iter_flags;
    wlan_local_t *l = hw_to_local(hw);
    if (l->vif && l->started) iterator(data, l->vif->addr, l->vif);
}

void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
        void (*iterator)(void *data, struct ieee80211_sta *sta), void *data)
{
    wlan_local_t *l = hw_to_local(hw);
    struct ieee80211_sta *s = __atomic_load_n(&l->ap_sta, __ATOMIC_ACQUIRE);
    if (s) iterator(data, s);
}

struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif, const u8 *addr)
{
    wlan_local_t *l = vif_to_local(vif);
    struct ieee80211_sta *s = l ? l->ap_sta : NULL;
    if (s && ether_addr_equal(s->addr, addr)) return s;
    return NULL;
}

struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw, const u8 *addr, const u8 *localaddr)
{
    wlan_local_t *l = hw_to_local(hw);
    struct ieee80211_sta *s = l->ap_sta;
    if (!s || !ether_addr_equal(s->addr, addr)) return NULL;
    if (localaddr && l->vif && !ether_addr_equal(l->vif->addr, localaddr)) return NULL;
    return s;
}

void ieee80211_iter_keys(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
        void (*iter)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta,
                     struct ieee80211_key_conf *key, void *data), void *iter_data)
{
    wlan_local_t *l = hw_to_local(hw);
    if (!vif) vif = l->vif;
    if (l->pkey) iter(hw, vif, l->ap_sta, l->pkey, iter_data);
    for (int i = 0; i < 4; i++)
        if (l->gkey[i]) iter(hw, vif, NULL, l->gkey[i], iter_data);
}

void ieee80211_scan_completed(struct ieee80211_hw *hw, struct cfg80211_scan_info *info)
{
    (void)hw; (void)info;
}

void ieee80211_connection_loss(struct ieee80211_vif *vif)
{
    wlan_local_t *l = vif_to_local(vif);
    if (!l) return;
    __atomic_store_n(&l->probe_misses, 99, __ATOMIC_RELEASE);
    wlan_kick(l);
}

void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif, enum nl80211_cqm_rssi_threshold_event ev, s32 level, gfp_t gfp)
{
    (void)ev; (void)gfp;
    wlan_local_t *l = vif_to_local(vif);
    if (l) l->last_signal = (s8)level;
}

void ieee80211_restart_hw(struct ieee80211_hw *hw)
{
    wlan_local_t *l = hw_to_local(hw);
    WLAN_LOG(l, "%s: driver requested a restart\n", l->name);
    __atomic_store_n(&l->probe_misses, 99, __ATOMIC_RELEASE);
    wlan_kick(l);
}

struct sk_buff *wlan_alloc_frame(wlan_local_t *l, size_t len)
{
    unsigned int head = l->hw.extra_tx_headroom + 32;
    struct sk_buff *skb = kpi_alloc_skb((unsigned int)len + head + 16, GFP_ATOMIC);
    if (!skb) return NULL;
    skb_reserve(skb, (int)head);
    memset(skb->cb, 0, sizeof(skb->cb));
    return skb;
}

struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif, int link_id, bool qos_ok)
{
    (void)link_id;
    wlan_local_t *l = hw_to_local(hw);
    bool qos = qos_ok && l->ap_wmm;
    struct sk_buff *skb = wlan_alloc_frame(l, 26);
    if (!skb) return NULL;
    struct ieee80211_qos_hdr *h = skb_put_zero(skb, qos ? 26 : 24);
    h->frame_control = IEEE80211_FTYPE_DATA | (qos ? IEEE80211_STYPE_QOS_NULLFUNC : IEEE80211_STYPE_NULLFUNC) |
                       IEEE80211_FCTL_TODS;
    ether_addr_copy(h->addr1, vif->bss_conf.bssid);
    ether_addr_copy(h->addr2, vif->addr);
    ether_addr_copy(h->addr3, vif->bss_conf.bssid);
    if (qos) h->qos_ctrl = 7;
    skb->priority = qos ? 7 : 0;
    return skb;
}

struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
    wlan_local_t *l = hw_to_local(hw);
    struct sk_buff *skb = wlan_alloc_frame(l, 16);
    if (!skb) return NULL;
    u8 *p = skb_put_zero(skb, 16);
    u16 fc = IEEE80211_FTYPE_CTL | IEEE80211_STYPE_PSPOLL;
    memcpy(p, &fc, 2);
    u16 aid = (u16)(vif->cfg.aid | 0xc000);
    memcpy(p + 2, &aid, 2);
    memcpy(p + 4, vif->bss_conf.bssid, ETH_ALEN);
    memcpy(p + 10, vif->addr, ETH_ALEN);
    return skb;
}

static size_t put_rates(wlan_local_t *l, u8 *p)
{
    struct ieee80211_supported_band *sb = l->band;
    u8 rates[16] = { 0 };
    int n = 0;
    for (int i = 0; sb && i < sb->n_bitrates && n < 16; i++)
        rates[n++] = (u8)(sb->bitrates[i].bitrate / 5);
    size_t off = 0;
    int first = n > 8 ? 8 : n;
    p[off++] = WLAN_EID_SUPP_RATES;
    p[off++] = (u8)first;
    memcpy(p + off, rates, (size_t)first);
    off += (size_t)first;
    if (n > 8) {
        p[off++] = WLAN_EID_EXT_SUPP_RATES;
        p[off++] = (u8)(n - 8);
        memcpy(p + off, rates + 8, (size_t)(n - 8));
        off += (size_t)(n - 8);
    }
    return off;
}

static size_t put_ht_cap(wlan_local_t *l, u8 *p)
{
    struct ieee80211_supported_band *sb = l->band;
    if (!sb || !sb->ht_cap.ht_supported) return 0;
    struct ieee80211_sta_ht_cap *c = &sb->ht_cap;
    p[0] = WLAN_EID_HT_CAPABILITY;
    p[1] = 26;
    u16 cap = c->cap & ~(IEEE80211_HT_CAP_SUP_WIDTH_20_40 | IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_DSSSCCK40);
    cap |= IEEE80211_HT_CAP_SM_PS;
    memcpy(p + 2, &cap, 2);
    p[4] = (u8)((c->ampdu_factor & 3) | ((c->ampdu_density & 7) << 2));
    memcpy(p + 5, &c->mcs, 16);
    memset(p + 21, 0, 7);
    return 28;
}

size_t wlan_put_rates(wlan_local_t *l, u8 *p) { return put_rates(l, p); }
size_t wlan_put_ht_cap(wlan_local_t *l, u8 *p) { return put_ht_cap(l, p); }

struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *hw, const u8 *src_addr, const u8 *ssid,
                                       size_t ssid_len, size_t tailroom)
{
    wlan_local_t *l = hw_to_local(hw);
    if (ssid_len > IEEE80211_MAX_SSID_LEN) ssid_len = IEEE80211_MAX_SSID_LEN;
    struct sk_buff *skb = wlan_alloc_frame(l, 24 + 2 + ssid_len + 64 + tailroom);
    if (!skb) return NULL;
    struct ieee80211_hdr_3addr *h = skb_put_zero(skb, 24);
    h->frame_control = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ;
    eth_broadcast_addr(h->addr1);
    ether_addr_copy(h->addr2, src_addr);
    eth_broadcast_addr(h->addr3);
    u8 *p = skb_put(skb, 2 + ssid_len);
    p[0] = WLAN_EID_SSID;
    p[1] = (u8)ssid_len;
    if (ssid_len) memcpy(p + 2, ssid, ssid_len);
    u8 tmp[64];
    size_t n = put_rates(l, tmp);
    skb_put_data(skb, tmp, (unsigned int)n);
    return skb;
}

struct sk_buff *ieee80211_proberesp_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
    (void)hw; (void)vif;
    return NULL;
}

struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                                         u16 *tim_offset, u16 *tim_length, unsigned int link_id)
{
    (void)hw; (void)vif; (void)link_id;
    if (tim_offset) *tim_offset = 0;
    if (tim_length) *tim_length = 0;
    return NULL;
}

void wlan_set_channel(wlan_local_t *l, struct ieee80211_channel *c)
{
    if (!c) return;
    l->cur_chan = c;
    l->hw.conf.chandef.chan = c;
    l->hw.conf.chandef.width = (l->band && l->band->ht_cap.ht_supported) ? NL80211_CHAN_WIDTH_20
                                                                         : NL80211_CHAN_WIDTH_20_NOHT;
    l->hw.conf.chandef.center_freq1 = c->center_freq;
    l->hw.conf.chandef.center_freq2 = 0;
    l->ops->config(&l->hw, -1, IEEE80211_CONF_CHANGE_CHANNEL);
}

u32 cfg80211_calculate_bitrate(struct rate_info *r)
{
    static const u16 ht20[8]  = { 65, 130, 195, 260, 390, 520, 585, 650 };
    static const u16 ht20s[8] = { 72, 144, 217, 289, 433, 578, 650, 722 };
    static const u16 ht40[8]  = { 135, 270, 405, 540, 810, 1080, 1215, 1350 };
    static const u16 ht40s[8] = { 150, 300, 450, 600, 900, 1200, 1350, 1500 };
    static const u16 vht20[10] = { 65, 130, 195, 260, 390, 520, 585, 650, 780, 867 };
    static const u16 vht40[10] = { 135, 270, 405, 540, 810, 1080, 1215, 1350, 1620, 1800 };
    static const u16 vht80[10] = { 293, 585, 878, 1170, 1755, 2340, 2633, 2925, 3510, 3900 };
    bool sgi = (r->flags & RATE_INFO_FLAGS_SHORT_GI) != 0;
    if (r->flags & RATE_INFO_FLAGS_MCS) {
        int idx = r->mcs & 7;
        int nss = (r->mcs >> 3) + 1;
        const u16 *t = r->bw == RATE_INFO_BW_40 ? (sgi ? ht40s : ht40) : (sgi ? ht20s : ht20);
        return (u32)t[idx] * (u32)nss;
    }
    if (r->flags & RATE_INFO_FLAGS_VHT_MCS) {
        int idx = r->mcs > 9 ? 9 : r->mcs;
        int nss = r->nss ? r->nss : 1;
        const u16 *t = r->bw == RATE_INFO_BW_80 ? vht80 : r->bw == RATE_INFO_BW_40 ? vht40 : vht20;
        u32 v = (u32)t[idx] * (u32)nss;
        return sgi ? v * 10 / 9 : v;
    }
    return r->legacy;
}

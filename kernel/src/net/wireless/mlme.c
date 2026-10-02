#include "wlan.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/sched/sched.h"
#include <stdio.h>

#define MS(x) ((u64)(x) * 1000000ULL)

#define SCAN_ACTIVE_NS   MS(45)
#define SCAN_PASSIVE_NS  MS(115)
#define AUTH_TIMEOUT_NS  MS(400)
#define ASSOC_TIMEOUT_NS MS(500)
#define HS_TIMEOUT_NS    MS(8000)
#define IDLE_PROBE_NS    MS(12000)
#define PROBE_WAIT_NS    MS(1000)
#define BA_WAIT_NS       MS(1500)
#define MAX_TRIES        3
#define MAX_PROBE_MISSES 3

enum { BA_IDLE = 0, BA_STARTING, BA_OPERATIONAL, BA_BLOCKED };

static struct sk_buff *mgmt_frame(wlan_local_t *l, u16 stype, const u8 *da, const u8 *bssid, size_t body)
{
    struct sk_buff *skb = wlan_alloc_frame(l, 24 + body);
    if (!skb) return NULL;
    struct ieee80211_hdr_3addr *h = skb_put_zero(skb, 24);
    h->frame_control = IEEE80211_FTYPE_MGMT | stype;
    ether_addr_copy(h->addr1, da);
    ether_addr_copy(h->addr2, l->vif->addr);
    ether_addr_copy(h->addr3, bssid);
    return skb;
}

static void set_msg(wlan_local_t *l, const char *msg)
{
    strncpy(l->last_msg, msg, sizeof(l->last_msg) - 1);
    l->last_msg[sizeof(l->last_msg) - 1] = 0;
}

static void send_nullfunc(wlan_local_t *l, bool pm, bool want_ack)
{
    if (!l->ap_sta) return;
    struct sk_buff *skb = ieee80211_nullfunc_get(&l->hw, l->vif, -1, false);
    if (!skb) return;
    struct ieee80211_hdr *h = (struct ieee80211_hdr *)skb->data;
    if (pm) h->frame_control |= IEEE80211_FCTL_PM;
    wlan_tx_mgmt(l, skb, want_ack);
}

static void update_filter(wlan_local_t *l)
{
    if (!l->ops->configure_filter) return;
    unsigned int flags = FIF_ALLMULTI;
    if (l->scanning || l->state == WLAN_ST_IDLE) flags |= FIF_BCN_PRBRESP_PROMISC;
    unsigned int nf = flags;
    l->ops->configure_filter(&l->hw, FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC, &nf, 0);
}

static struct ieee80211_channel *chan_by_num(wlan_local_t *l, int ch)
{
    for (int i = 0; i < l->nchans; i++)
        if (l->chans[i]->hw_value == ch) return l->chans[i];
    return NULL;
}

void wlan_scan_start(wlan_local_t *l, const u8 *ssid, u8 ssid_len, bool for_connect)
{
    if (ssid_len > IEEE80211_MAX_SSID_LEN) ssid_len = IEEE80211_MAX_SSID_LEN;
    if (ssid_len) {
        memcpy(l->scan_ssid, ssid, ssid_len);
        l->scan_ssid_len = ssid_len;
    }
    if (for_connect) l->scan_for_connect = true;
    if (l->scanning) return;
    if (!ssid_len) l->scan_ssid_len = 0;
    l->scanning = true;
    l->scan_idx = 0;
    l->scan_next_ns = 0;
    if (l->state == WLAN_ST_CONNECTED) send_nullfunc(l, true, false);
    if (l->ops->sw_scan_start) l->ops->sw_scan_start(&l->hw, l->vif, l->vif->addr);
    update_filter(l);
}

static void send_probe(wlan_local_t *l, const u8 *ssid, size_t len)
{
    struct sk_buff *skb = ieee80211_probereq_get(&l->hw, l->vif->addr, ssid, len, 0);
    if (!skb) return;
    if (l->band && l->band->ht_cap.ht_supported) {
        u8 ht[32];
        size_t n = wlan_put_ht_cap(l, ht);
        if (n && skb_tailroom(skb) >= (int)n) skb_put_data(skb, ht, (unsigned int)n);
    }
    wlan_tx_mgmt(l, skb, false);
}

static void scan_finish(wlan_local_t *l, u64 now)
{
    l->scanning = false;
    if (l->ops->sw_scan_complete) l->ops->sw_scan_complete(&l->hw, l->vif);
    if (l->state != WLAN_ST_IDLE && l->cur.channel) {
        wlan_set_channel(l, chan_by_num(l, l->cur.channel));
        if (l->state == WLAN_ST_CONNECTED) send_nullfunc(l, false, false);
    }
    update_filter(l);
    l->scan_gen++;
    l->scan_done_ns = now;
    if (l->cmd.busy && !l->cmd.done && l->cmd.kind == WLAN_CMD_SCAN) wlan_cmd_finish(l, 0);
    if (l->scan_for_connect) {
        l->scan_for_connect = false;
        if (l->want_connected && l->state == WLAN_ST_IDLE) wlan_mlme_start_connect(l);
    }
}

void wlan_scan_step(wlan_local_t *l, u64 now, u64 *next)
{
    if (!l->scanning) return;
    if (now < l->scan_next_ns) {
        if (l->scan_next_ns < *next) *next = l->scan_next_ns;
        return;
    }
    if (l->scan_idx >= l->nchans) {
        scan_finish(l, now);
        return;
    }
    struct ieee80211_channel *c = l->chans[l->scan_idx++];
    wlan_set_channel(l, c);
    bool passive = (c->flags & IEEE80211_CHAN_NO_IR) != 0;
    if (!passive) {
        send_probe(l, NULL, 0);
        if (l->scan_ssid_len) send_probe(l, l->scan_ssid, l->scan_ssid_len);
    }
    l->scan_next_ns = now + (passive ? SCAN_PASSIVE_NS : SCAN_ACTIVE_NS);
    if (l->scan_next_ns < *next) *next = l->scan_next_ns;
}

static void send_auth(wlan_local_t *l)
{
    struct sk_buff *skb = mgmt_frame(l, IEEE80211_STYPE_AUTH, l->cur.bssid, l->cur.bssid, 6);
    if (!skb) return;
    u8 *b = skb_put_zero(skb, 6);
    b[0] = WLAN_AUTH_OPEN;
    b[2] = 1;
    wlan_tx_mgmt(l, skb, true);
}

static size_t build_own_rsn(wlan_local_t *l, u8 *p)
{
    u32 g = l->ap_rsn.group;
    u8 *s = p;
    *p++ = WLAN_EID_RSN;
    *p++ = 20;
    *p++ = 1; *p++ = 0;
    *p++ = (u8)(g >> 24); *p++ = (u8)(g >> 16); *p++ = (u8)(g >> 8); *p++ = (u8)g;
    *p++ = 1; *p++ = 0;
    *p++ = 0x00; *p++ = 0x0F; *p++ = 0xAC; *p++ = 0x04;
    *p++ = 1; *p++ = 0;
    *p++ = 0x00; *p++ = 0x0F; *p++ = 0xAC; *p++ = 0x02;
    *p++ = 0; *p++ = 0;
    return (size_t)(p - s);
}

static void send_assoc(wlan_local_t *l)
{
    u8 body[256];
    size_t n = 0;
    u16 capab = WLAN_CAPABILITY_ESS;
    if (l->cur.capab & WLAN_CAPABILITY_PRIVACY) capab |= WLAN_CAPABILITY_PRIVACY;
    if (l->cur.capab & WLAN_CAPABILITY_SHORT_PREAMBLE) capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;
    if (l->cur.capab & WLAN_CAPABILITY_SHORT_SLOT_TIME) capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
    memcpy(body, &capab, 2);
    u16 li = l->hw.conf.listen_interval ? l->hw.conf.listen_interval : 10;
    memcpy(body + 2, &li, 2);
    n = 4;
    body[n++] = WLAN_EID_SSID;
    body[n++] = l->cur.ssid_len;
    memcpy(body + n, l->cur.ssid, l->cur.ssid_len);
    n += l->cur.ssid_len;
    n += wlan_put_rates(l, body + n);
    if (l->ap_rsn.present) {
        size_t r = build_own_rsn(l, body + n);
        memcpy(l->own_rsn.ie, body + n, r);
        l->own_rsn.ie_len = (u8)r;
        l->own_rsn.present = true;
        n += r;
    }
    if (l->ap_ht) n += wlan_put_ht_cap(l, body + n);
    if (l->ap_wmm) {
        static const u8 wmm[] = { WLAN_EID_VENDOR_SPECIFIC, 7, 0x00, 0x50, 0xF2, 0x02, 0x00, 0x01, 0x00 };
        memcpy(body + n, wmm, sizeof(wmm));
        n += sizeof(wmm);
    }
    struct sk_buff *skb = mgmt_frame(l, IEEE80211_STYPE_ASSOC_REQ, l->cur.bssid, l->cur.bssid, n);
    if (!skb) return;
    skb_put_data(skb, body, (unsigned int)n);
    wlan_tx_mgmt(l, skb, true);
}

static const char *sec_problem(u32 sec, bool has_psk)
{
    if (sec & WLAN_SEC_WEP) return "WEP networks are insecure and not supported";
    if ((sec & WLAN_SEC_SAE) && !(sec & WLAN_SEC_PSK)) return "WPA3-only networks are not supported yet";
    if ((sec & WLAN_SEC_EAP) && !(sec & WLAN_SEC_PSK)) return "WPA2-Enterprise is not supported yet";
    if (sec & WLAN_SEC_PMF_REQUIRED) return "the network requires 802.11w, not supported yet";
    if ((sec & WLAN_SEC_TKIP) && !(sec & WLAN_SEC_OPEN)) return "the network uses TKIP, switch the router to AES/CCMP";
    if ((sec & WLAN_SEC_OPEN) && has_psk) return "the network is open but a password was given";
    if (!(sec & WLAN_SEC_OPEN) && !has_psk) return "the network needs a password";
    if (!(sec & WLAN_SEC_SUPPORTED)) return "unsupported security";
    return NULL;
}

static void fail_and_retry(wlan_local_t *l, const char *why, int err)
{
    l->last_error = err;
    wlan_mlme_disconnect(l, 0, false, why);
}

void wlan_mlme_start_connect(wlan_local_t *l)
{
    if (!l->want_connected || l->state != WLAN_ST_IDLE) return;
    if (l->scanning) {
        l->scan_for_connect = true;
        return;
    }
    u64 now = sched_now_ns();
    wlan_bss_t *best = NULL;
    uint64_t f = spinlock_acquire_irqsave(&l->bss_lock);
    for (int i = 0; i < WLAN_MAX_BSS; i++) {
        wlan_bss_t *b = &l->bss[i];
        if (!b->valid || now - b->last_seen_ns > MS(60000)) continue;
        if (b->ssid_len != l->target.ssid_len || memcmp(b->ssid, l->target.ssid, b->ssid_len)) continue;
        if (l->target.has_bssid && !ether_addr_equal(b->bssid, l->target.bssid)) continue;
        if (!chan_by_num(l, b->channel)) continue;
        if (!best || b->signal > best->signal) best = b;
    }
    if (best) memcpy(&l->cur, best, sizeof(l->cur));
    spinlock_release_irqrestore(&l->bss_lock, f);
    if (!best) {
        if (!l->scan_done_ns || now - l->scan_done_ns > MS(3000)) {
            wlan_scan_start(l, (const u8 *)l->target.ssid, l->target.ssid_len, true);
            return;
        }
        fail_and_retry(l, "network not found", -ENOENT);
        return;
    }
    const char *problem = sec_problem(l->cur.security, l->target.has_psk);
    if (problem) {
        l->want_connected = false;
        fail_and_retry(l, problem, -EOPNOTSUPP);
        return;
    }
    wlan_parse_rsn(wlan_find_ie(l->cur.ies, l->cur.ie_len, WLAN_EID_RSN), &l->ap_rsn);
    l->own_rsn.present = false;
    l->ap_wmm = wlan_find_vendor_ie(l->cur.ies, l->cur.ie_len, 0x0050F2, 2) != NULL;
    l->ap_ht = l->band && l->band->ht_cap.ht_supported &&
               wlan_find_ie(l->cur.ies, l->cur.ie_len, WLAN_EID_HT_CAPABILITY) != NULL;
    if (l->target.has_psk) memcpy(l->pmk, l->target.psk, 32);
    wlan_set_channel(l, chan_by_num(l, l->cur.channel));
    memcpy(l->vif->bss_conf.bssid_storage, l->cur.bssid, ETH_ALEN);
    l->vif->bss_conf.bssid = l->vif->bss_conf.bssid_storage;
    l->vif->bss_conf.beacon_int = l->cur.beacon_int;
    l->state = WLAN_ST_AUTH;
    update_filter(l);
    if (l->ops->bss_info_changed)
        l->ops->bss_info_changed(&l->hw, l->vif, &l->vif->bss_conf, BSS_CHANGED_BSSID | BSS_CHANGED_BEACON_INT);
    if (l->ops->mgd_prepare_tx) {
        struct ieee80211_prep_tx_info info = { .duration = 0, .subtype = IEEE80211_STYPE_AUTH };
        l->ops->mgd_prepare_tx(&l->hw, l->vif, &info);
    }
    char ssid[33];
    memcpy(ssid, l->cur.ssid, l->cur.ssid_len);
    ssid[l->cur.ssid_len] = 0;
    WLAN_LOG(l, "%s: authenticating with %02x:%02x:%02x:%02x:%02x:%02x (\"%s\", channel %d, %d dBm)\n",
             l->name, l->cur.bssid[0], l->cur.bssid[1], l->cur.bssid[2], l->cur.bssid[3],
             l->cur.bssid[4], l->cur.bssid[5], ssid, l->cur.channel, l->cur.signal);
    set_msg(l, "authenticating");
    l->tries = 1;
    send_auth(l);
    l->deadline_ns = sched_now_ns() + AUTH_TIMEOUT_NS;
}

static u32 rates_to_mask(wlan_local_t *l, const u8 *ies, size_t len, bool basic_only)
{
    u32 mask = 0;
    const u8 *ie[2] = { wlan_find_ie(ies, len, WLAN_EID_SUPP_RATES), wlan_find_ie(ies, len, WLAN_EID_EXT_SUPP_RATES) };
    for (int k = 0; k < 2; k++) {
        if (!ie[k]) continue;
        for (int i = 0; i < ie[k][1]; i++) {
            u8 r = ie[k][2 + i];
            if (basic_only && !(r & 0x80)) continue;
            int rate = (r & 0x7f) * 5;
            for (int j = 0; l->band && j < l->band->n_bitrates; j++)
                if (l->band->bitrates[j].bitrate == rate) mask |= 1u << j;
        }
    }
    return mask;
}

static void apply_wmm(wlan_local_t *l, const u8 *ies, size_t len)
{
    if (!l->ops->conf_tx) return;
    const u8 *w = wlan_find_vendor_ie(ies, len, 0x0050F2, 2);
    bool param = w && w[1] >= 24 && w[6] == 1;
    static const u8 aci_to_ac[4] = { IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_VI, IEEE80211_AC_VO };
    if (!param) {
        for (u16 ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
            struct ieee80211_tx_queue_params p = { .txop = 0, .cw_min = 15, .cw_max = 1023, .aifs = 2 };
            l->ops->conf_tx(&l->hw, l->vif, 0, ac, &p);
        }
        return;
    }
    const u8 *rec = w + 2 + 8;
    for (int i = 0; i < 4; i++, rec += 4) {
        u8 aci = (rec[0] >> 5) & 3;
        struct ieee80211_tx_queue_params p;
        memset(&p, 0, sizeof(p));
        p.aifs = rec[0] & 0x0f;
        p.acm = (rec[0] & 0x10) != 0;
        p.cw_min = (u16)((1u << (rec[1] & 0x0f)) - 1);
        p.cw_max = (u16)((1u << (rec[1] >> 4)) - 1);
        p.txop = (u16)(rec[2] | (rec[3] << 8));
        l->ops->conf_tx(&l->hw, l->vif, 0, aci_to_ac[aci], &p);
    }
}

static void fill_ht(wlan_local_t *l, struct ieee80211_sta *sta, const u8 *ies, size_t len)
{
    const u8 *ht = wlan_find_ie(ies, len, WLAN_EID_HT_CAPABILITY);
    if (!ht || ht[1] < 26 || !l->band || !l->band->ht_cap.ht_supported) return;
    struct ieee80211_sta_ht_cap *c = &sta->deflink.ht_cap;
    u16 cap = (u16)(ht[2] | (ht[3] << 8));
    c->ht_supported = true;
    c->cap = cap & ~(IEEE80211_HT_CAP_SUP_WIDTH_20_40 | IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_DSSSCCK40);
    c->ampdu_factor = ht[4] & 3;
    c->ampdu_density = (ht[4] >> 2) & 7;
    memcpy(&c->mcs, ht + 5, sizeof(c->mcs));
    for (int i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++)
        c->mcs.rx_mask[i] &= l->band->ht_cap.mcs.rx_mask[i];
    sta->deflink.bandwidth = IEEE80211_STA_RX_BW_20;
    sta->deflink.rx_nss = 1;
    sta->deflink.agg.max_amsdu_len = (cap & IEEE80211_HT_CAP_MAX_AMSDU) ? 7935 : 3839;
    sta->deflink.agg.max_rc_amsdu_len = sta->deflink.agg.max_amsdu_len;
}

static void assoc_success(wlan_local_t *l, u16 aid, const u8 *ies, size_t len)
{
    struct ieee80211_sta *sta = kzalloc(sizeof(*sta) + l->hw.sta_data_size, GFP_KERNEL);
    if (!sta) {
        fail_and_retry(l, "out of memory", -ENOMEM);
        return;
    }
    ether_addr_copy(sta->addr, l->cur.bssid);
    sta->aid = aid;
    sta->wme = l->ap_wmm;
    sta->kpi_owner = l;
    sta->smps_mode = IEEE80211_SMPS_OFF;
    sta->deflink.sta = sta;
    ether_addr_copy(sta->deflink.addr, l->cur.bssid);
    enum nl80211_band band = l->cur_chan ? l->cur_chan->band : NL80211_BAND_2GHZ;
    u32 rates = rates_to_mask(l, ies, len, false);
    if (!rates) rates = rates_to_mask(l, l->cur.ies, l->cur.ie_len, false);
    sta->deflink.supp_rates[band] = rates;
    sta->deflink.bandwidth = IEEE80211_STA_RX_BW_20;
    sta->deflink.rx_nss = 1;
    sta->deflink.agg.max_amsdu_len = 3839;
    sta->deflink.agg.max_rc_amsdu_len = 3839;
    if (l->ap_ht) fill_ht(l, sta, ies, len);
    for (int t = 0; t <= IEEE80211_NUM_TIDS; t++) {
        sta->txq[t] = wlan_txq_alloc(l, l->vif, sta, (u8)t);
        if (!sta->txq[t]) {
            for (int k = 0; k < t; k++) wlan_txq_free(sta->txq[k]);
            kfree(sta);
            fail_and_retry(l, "out of memory", -ENOMEM);
            return;
        }
    }
    int r = l->ops->sta_add ? l->ops->sta_add(&l->hw, l->vif, sta) : 0;
    if (r) {
        for (int t = 0; t <= IEEE80211_NUM_TIDS; t++) wlan_txq_free(sta->txq[t]);
        kfree(sta);
        fail_and_retry(l, "driver refused the station", r);
        return;
    }
    memset(l->seq, 0, sizeof(l->seq));
    memset(l->rx_seq_valid, 0, sizeof(l->rx_seq_valid));
    __atomic_store_n(&l->ap_sta, sta, __ATOMIC_RELEASE);
    l->aid = aid;
    struct ieee80211_vif *vif = l->vif;
    vif->cfg.assoc = true;
    vif->cfg.aid = aid;
    vif->cfg.ps = false;
    vif->cfg.ssid_len = l->cur.ssid_len;
    memcpy(vif->cfg.ssid, l->cur.ssid, l->cur.ssid_len);
    vif->bss_conf.beacon_int = l->cur.beacon_int;
    vif->bss_conf.dtim_period = l->cur.dtim_period ? l->cur.dtim_period : 1;
    vif->bss_conf.basic_rates = rates_to_mask(l, l->cur.ies, l->cur.ie_len, true);
    if (!vif->bss_conf.basic_rates) vif->bss_conf.basic_rates = 1;
    vif->bss_conf.use_short_slot = (l->cur.capab & WLAN_CAPABILITY_SHORT_SLOT_TIME) != 0;
    vif->bss_conf.use_short_preamble = (l->cur.capab & WLAN_CAPABILITY_SHORT_PREAMBLE) != 0;
    vif->bss_conf.qos = l->ap_wmm;
    u64 changed = BSS_CHANGED_ASSOC | BSS_CHANGED_BEACON_INT | BSS_CHANGED_BASIC_RATES |
                  BSS_CHANGED_ERP_SLOT | BSS_CHANGED_ERP_PREAMBLE | BSS_CHANGED_QOS;
    if (l->ap_ht) changed |= BSS_CHANGED_HT;
    if (l->ops->bss_info_changed) l->ops->bss_info_changed(&l->hw, vif, &vif->bss_conf, changed);
    apply_wmm(l, ies, len);
    l->last_ap_rx_ns = sched_now_ns();
    l->probe_pending = false;
    l->probe_misses = 0;
    wlan_ba_reset(l);
    WLAN_LOG(l, "%s: associated, aid %u%s%s\n", l->name, aid, l->ap_ht ? ", 802.11n" : "",
             l->ap_wmm ? ", wmm" : "");
    if (l->ap_rsn.present) {
        l->state = WLAN_ST_HANDSHAKE;
        set_msg(l, "waiting for the 4-way handshake");
        wlan_wpa_reset(l);
        l->deadline_ns = sched_now_ns() + HS_TIMEOUT_NS;
    } else {
        l->state = WLAN_ST_CONNECTED;
        wlan_set_authorized(l, true);
    }
}

void wlan_set_authorized(wlan_local_t *l, bool on)
{
    bool was = l->authorized;
    l->authorized = on;
    if (on) {
        l->state = WLAN_ST_CONNECTED;
        l->connected_ns = sched_now_ns();
        l->fail_count = 0;
        l->hs_failures = 0;
        l->last_error = 0;
        set_msg(l, "connected");
        update_filter(l);
    }
    if (l->ndev) l->ndev->link_up = on ? 1 : 0;
    if (on && !was) {
        char ssid[33];
        memcpy(ssid, l->cur.ssid, l->cur.ssid_len);
        ssid[l->cur.ssid_len] = 0;
        printf("%s: connected to \"%s\" (channel %d, %d dBm)\n", l->name, ssid, l->cur.channel, l->cur.signal);
        WLAN_LOG(l, "%s: connected to \"%s\"\n", l->name, ssid);
    }
}

void wlan_mlme_disconnect(wlan_local_t *l, u16 reason, bool send_deauth, const char *why)
{
    bool had = l->state != WLAN_ST_IDLE;
    if (send_deauth && had) {
        struct sk_buff *skb = mgmt_frame(l, IEEE80211_STYPE_DEAUTH, l->cur.bssid, l->cur.bssid, 2);
        if (skb) {
            u16 r = reason ? reason : WLAN_REASON_DEAUTH_LEAVING;
            skb_put_data(skb, &r, 2);
            wlan_tx_mgmt(l, skb, false);
        }
    }
    bool was_connected = l->authorized;
    wlan_set_authorized(l, false);
    wlan_keys_clear(l);
    wlan_ba_reset(l);
    struct ieee80211_sta *sta = l->ap_sta;
    if (sta) {
        __atomic_store_n(&l->ap_sta, NULL, __ATOMIC_RELEASE);
        if (l->ops->sta_remove) l->ops->sta_remove(&l->hw, l->vif, sta);
        if (l->ops->flush) l->ops->flush(&l->hw, l->vif, 0xf, true);
        for (int t = 0; t <= IEEE80211_NUM_TIDS; t++) wlan_txq_free(sta->txq[t]);
        kfree(sta);
    }
    if (l->vif->cfg.assoc) {
        l->vif->cfg.assoc = false;
        l->vif->cfg.aid = 0;
        if (l->ops->bss_info_changed)
            l->ops->bss_info_changed(&l->hw, l->vif, &l->vif->bss_conf, BSS_CHANGED_ASSOC);
    }
    if (had) {
        eth_zero_addr(l->vif->bss_conf.bssid_storage);
        if (l->ops->bss_info_changed)
            l->ops->bss_info_changed(&l->hw, l->vif, &l->vif->bss_conf, BSS_CHANGED_BSSID);
    }
    l->state = WLAN_ST_IDLE;
    l->aid = 0;
    l->ptk_valid = false;
    crypto_wipe_local(l);
    update_filter(l);
    if (why) set_msg(l, why);
    if (reason) l->last_reason = reason;
    if (had || why) {
        if (why) WLAN_LOG(l, "%s: disconnected: %s\n", l->name, why);
        else WLAN_LOG(l, "%s: disconnected (reason %u)\n", l->name, reason);
        if (was_connected) printf("%s: disconnected%s%s\n", l->name, why ? ": " : "", why ? why : "");
    }
    if (l->want_connected) {
        l->fail_count++;
        int shift = l->fail_count > 5 ? 5 : l->fail_count;
        l->reconnect_ns = sched_now_ns() + MS(1000) * (1u << (shift - 1));
    }
}

static void rx_auth(wlan_local_t *l, struct ieee80211_mgmt *m, size_t len)
{
    if (l->state != WLAN_ST_AUTH || len < 24 + 6) return;
    if (!ether_addr_equal(m->bssid, l->cur.bssid)) return;
    if (m->u.auth.auth_alg != WLAN_AUTH_OPEN || m->u.auth.auth_transaction != 2) return;
    if (m->u.auth.status_code != WLAN_STATUS_SUCCESS) {
        char msg[48];
        snprintf(msg, sizeof(msg), "authentication rejected (status %u)", m->u.auth.status_code);
        fail_and_retry(l, msg, -EACCES);
        return;
    }
    l->state = WLAN_ST_ASSOC;
    set_msg(l, "associating");
    l->tries = 1;
    send_assoc(l);
    l->deadline_ns = sched_now_ns() + ASSOC_TIMEOUT_NS;
}

static void rx_assoc_resp(wlan_local_t *l, struct ieee80211_mgmt *m, size_t len)
{
    size_t fixed = offsetof(struct ieee80211_mgmt, u.assoc_resp.variable);
    if (l->state != WLAN_ST_ASSOC || len < fixed) return;
    if (!ether_addr_equal(m->bssid, l->cur.bssid)) return;
    u16 status = m->u.assoc_resp.status_code;
    if (status != WLAN_STATUS_SUCCESS) {
        char msg[48];
        snprintf(msg, sizeof(msg), "association rejected (status %u)", status);
        fail_and_retry(l, msg, -EACCES);
        return;
    }
    assoc_success(l, m->u.assoc_resp.aid & 0x3fff, m->u.assoc_resp.variable, len - fixed);
}

static void handshake_failed(wlan_local_t *l)
{
    if (++l->hs_failures < 2) return;
    l->want_connected = false;
    l->hs_failures = 0;
    l->last_error = -EACCES;
    set_msg(l, "wrong password");
    WLAN_LOG(l, "%s: giving up, the password is wrong\n", l->name);
}

void wlan_mlme_rx_mgmt(wlan_local_t *l, struct sk_buff *skb)
{
    struct ieee80211_mgmt *m = (struct ieee80211_mgmt *)skb->data;
    u16 fc = m->frame_control;
    size_t len = skb->len;
    if (ieee80211_is_auth(fc)) { rx_auth(l, m, len); return; }
    if (ieee80211_is_assoc_resp(fc) || ieee80211_is_reassoc_resp(fc)) { rx_assoc_resp(l, m, len); return; }
    if (ieee80211_is_deauth(fc) || ieee80211_is_disassoc(fc)) {
        if (l->state == WLAN_ST_IDLE || len < 26) return;
        if (!ether_addr_equal(m->sa, l->cur.bssid)) return;
        u16 reason = m->u.deauth.reason_code;
        char msg[48];
        snprintf(msg, sizeof(msg), "%s by the access point (reason %u)",
                 ieee80211_is_deauth(fc) ? "deauthenticated" : "disassociated", reason);
        bool hs = l->state == WLAN_ST_HANDSHAKE && (reason == 15 || reason == 2 || reason == 23);
        if (hs) snprintf(msg, sizeof(msg), "handshake failed, wrong password? (reason %u)", reason);
        wlan_mlme_disconnect(l, reason, false, msg);
        if (hs) handshake_failed(l);
        return;
    }
    if (ieee80211_is_action(fc)) {
        if (!l->ap_sta || !ether_addr_equal(m->sa, l->ap_sta->addr)) return;
        wlan_ba_rx_action(l, skb->data, len);
    }
}

void wlan_mlme_timeouts(wlan_local_t *l, u64 now, u64 *next)
{
    if (__atomic_load_n(&l->probe_misses, __ATOMIC_ACQUIRE) >= 99 && l->state != WLAN_ST_IDLE) {
        l->probe_misses = 0;
        wlan_mlme_disconnect(l, 4, true, "connection lost");
        return;
    }
    switch (l->state) {
    case WLAN_ST_AUTH:
    case WLAN_ST_ASSOC:
        if (now >= l->deadline_ns) {
            if (l->tries >= MAX_TRIES) {
                fail_and_retry(l, l->state == WLAN_ST_AUTH ? "no answer to authentication"
                                                           : "no answer to association", -ETIMEDOUT);
                return;
            }
            l->tries++;
            if (l->state == WLAN_ST_AUTH) {
                send_auth(l);
                l->deadline_ns = now + AUTH_TIMEOUT_NS;
            } else {
                send_assoc(l);
                l->deadline_ns = now + ASSOC_TIMEOUT_NS;
            }
        }
        if (l->deadline_ns < *next) *next = l->deadline_ns;
        break;
    case WLAN_ST_HANDSHAKE:
        if (now >= l->deadline_ns) {
            wlan_mlme_disconnect(l, 15, true, "4-way handshake timed out, wrong password?");
            handshake_failed(l);
            return;
        }
        if (l->deadline_ns < *next) *next = l->deadline_ns;
        break;
    case WLAN_ST_CONNECTED: {
        if (l->ops->sta_statistics && l->ap_sta) {
            struct station_info si;
            memset(&si, 0, sizeof(si));
            l->ops->sta_statistics(&l->hw, l->vif, l->ap_sta, &si);
            if (si.filled & BIT_ULL(NL80211_STA_INFO_TX_BITRATE))
                l->last_rate = (u16)cfg80211_calculate_bitrate(&si.txrate);
        }
        if (l->scanning) break;
        if (l->probe_pending) {
            int res = __atomic_load_n(&l->ack_result, __ATOMIC_ACQUIRE);
            if (res == 1 || l->last_ap_rx_ns > l->probe_sent_ns) {
                l->probe_pending = false;
                l->probe_misses = 0;
                if (res == 1) l->last_ap_rx_ns = now;
            } else if (res == 0 || now - l->probe_sent_ns >= PROBE_WAIT_NS) {
                l->probe_pending = false;
                if (++l->probe_misses >= MAX_PROBE_MISSES) {
                    wlan_mlme_disconnect(l, 4, true, "the access point stopped answering");
                    return;
                }
            }
        }
        u64 idle_at = l->last_ap_rx_ns + IDLE_PROBE_NS;
        if (!l->probe_pending && now >= idle_at) {
            send_nullfunc(l, false, true);
            l->probe_pending = true;
            l->probe_sent_ns = now;
        }
        u64 n = l->probe_pending ? l->probe_sent_ns + PROBE_WAIT_NS : idle_at;
        if (n < *next) *next = n;
        break;
    }
    case WLAN_ST_IDLE:
        if (l->want_connected && !l->scanning) {
            if (now >= l->reconnect_ns) wlan_mlme_start_connect(l);
            else if (l->reconnect_ns < *next) *next = l->reconnect_ns;
        }
        break;
    }
}

void wlan_ba_reset(wlan_local_t *l)
{
    for (int i = 0; i < 8; i++) {
        l->ba_state[i] = BA_IDLE;
        l->ba_deadline[i] = 0;
    }
}

static void ba_action(wlan_local_t *l, enum ieee80211_ampdu_mlme_action act, u8 tid, u16 ssn, u16 buf, bool amsdu)
{
    if (!l->ops->ampdu_action || !l->ap_sta) return;
    struct ieee80211_ampdu_params p;
    memset(&p, 0, sizeof(p));
    p.action = act;
    p.sta = l->ap_sta;
    p.tid = tid;
    p.ssn = ssn;
    p.buf_size = buf;
    p.amsdu = amsdu;
    l->ops->ampdu_action(&l->hw, l->vif, &p);
}

static void send_action(wlan_local_t *l, const u8 *body, size_t n)
{
    struct sk_buff *skb = mgmt_frame(l, IEEE80211_STYPE_ACTION, l->cur.bssid, l->cur.bssid, n);
    if (!skb) return;
    skb_put_data(skb, body, (unsigned int)n);
    wlan_tx_mgmt(l, skb, false);
}

int ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid, u16 timeout)
{
    (void)timeout;
    wlan_local_t *l = sta ? sta->kpi_owner : NULL;
    if (!l || tid >= 8 || !l->ap_ht || sta != l->ap_sta || l->state != WLAN_ST_CONNECTED) return -EINVAL;
    uint64_t f = spinlock_acquire_irqsave(&l->lock);
    u8 st = l->ba_state[tid];
    if (st != BA_IDLE) {
        spinlock_release_irqrestore(&l->lock, f);
        return st == BA_BLOCKED ? -EINVAL : -EAGAIN;
    }
    l->ba_state[tid] = BA_STARTING;
    u8 token = ++l->dialog_token ? l->dialog_token : ++l->dialog_token;
    l->ba_token[tid] = token;
    l->ba_deadline[tid] = sched_now_ns() + BA_WAIT_NS;
    u16 ssn = l->seq[l->ap_wmm ? tid : IEEE80211_NUM_TIDS];
    spinlock_release_irqrestore(&l->lock, f);
    struct ieee80211_ampdu_params p;
    memset(&p, 0, sizeof(p));
    p.action = IEEE80211_AMPDU_TX_START;
    p.sta = sta;
    p.tid = tid;
    p.ssn = ssn;
    int r = l->ops->ampdu_action ? l->ops->ampdu_action(&l->hw, l->vif, &p) : -EOPNOTSUPP;
    if (r < 0) {
        l->ba_state[tid] = BA_BLOCKED;
        return -EINVAL;
    }
    u16 params = (u16)(0x0002 | (tid << 2) | (64 << 6));
    if (ieee80211_hw_check(&l->hw, SUPPORTS_AMSDU_IN_AMPDU)) params |= 0x0001;
    u8 body[9] = { 3, 0, token, (u8)params, (u8)(params >> 8), 0, 0, (u8)(ssn << 4), (u8)(ssn >> 4) };
    send_action(l, body, sizeof(body));
    wlan_kick(l);
    return 0;
}

void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra, u16 tid)
{
    (void)vif; (void)ra; (void)tid;
}

void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra, u16 tid)
{
    (void)vif; (void)ra; (void)tid;
}

void wlan_ba_rx_action(wlan_local_t *l, const u8 *frame, size_t len)
{
    if (len < 24 + 2) return;
    const u8 *a = frame + 24;
    size_t n = len - 24;
    if (a[0] != 3) return;
    if (a[1] == 0 && n >= 9) {
        u8 resp[9] = { 3, 1, a[2], 37, 0, a[3], a[4], 0, 0 };
        send_action(l, resp, sizeof(resp));
        return;
    }
    if (a[1] == 1 && n >= 9) {
        u8 token = a[2];
        u16 status = (u16)(a[3] | (a[4] << 8));
        u16 params = (u16)(a[5] | (a[6] << 8));
        u8 tid = (params >> 2) & 0xf;
        if (tid >= 8 || l->ba_state[tid] != BA_STARTING || l->ba_token[tid] != token) return;
        if (status == 0) {
            u16 buf = params >> 6;
            if (!buf || buf > 64) buf = 64;
            l->ba_state[tid] = BA_OPERATIONAL;
            ba_action(l, IEEE80211_AMPDU_TX_OPERATIONAL, tid, 0, buf, (params & 1) != 0);
            WLAN_LOG(l, "%s: tx aggregation on tid %u (window %u)\n", l->name, tid, buf);
        } else {
            l->ba_state[tid] = BA_BLOCKED;
            ba_action(l, IEEE80211_AMPDU_TX_STOP_FLUSH, tid, 0, 0, false);
        }
        return;
    }
    if (a[1] == 2 && n >= 6) {
        u16 params = (u16)(a[2] | (a[3] << 8));
        u8 tid = (params >> 12) & 0xf;
        bool initiator = (params & 0x0800) != 0;
        if (initiator || tid >= 8) return;
        if (l->ba_state[tid] == BA_OPERATIONAL || l->ba_state[tid] == BA_STARTING) {
            ba_action(l, IEEE80211_AMPDU_TX_STOP_CONT, tid, 0, 0, false);
            l->ba_state[tid] = BA_IDLE;
        }
    }
}

void wlan_ba_timeouts(wlan_local_t *l, u64 now, u64 *next)
{
    for (u8 tid = 0; tid < 8; tid++) {
        if (l->ba_state[tid] != BA_STARTING) continue;
        if (now >= l->ba_deadline[tid]) {
            l->ba_state[tid] = BA_BLOCKED;
            ba_action(l, IEEE80211_AMPDU_TX_STOP_FLUSH, tid, 0, 0, false);
        } else if (l->ba_deadline[tid] < *next) {
            *next = l->ba_deadline[tid];
        }
    }
}

static void process_cmd(wlan_local_t *l)
{
    if (!l->cmd.busy || l->cmd.done) return;
    switch (l->cmd.kind) {
    case WLAN_CMD_SCAN:
        if (!l->scanning) {
            struct wlan_connect_req *c = &l->cmd.conn;
            wlan_scan_start(l, (const u8 *)c->ssid, c->ssid_len, false);
        }
        l->cmd.kind = WLAN_CMD_SCAN;
        break;
    case WLAN_CMD_CONNECT:
        if (l->state != WLAN_ST_IDLE)
            wlan_mlme_disconnect(l, WLAN_REASON_DEAUTH_LEAVING, true, NULL);
        memcpy(&l->target, &l->cmd.conn, sizeof(l->target));
        l->want_connected = true;
        l->fail_count = 0;
        l->hs_failures = 0;
        l->last_error = 0;
        l->reconnect_ns = 0;
        set_msg(l, "searching");
        wlan_mlme_start_connect(l);
        wlan_cmd_finish(l, 0);
        break;
    case WLAN_CMD_DISCONNECT:
        l->want_connected = false;
        wlan_mlme_disconnect(l, WLAN_REASON_DEAUTH_LEAVING, true, "disconnected by the user");
        crypto_wipe(&l->target, sizeof(l->target));
        wlan_cmd_finish(l, 0);
        break;
    default:
        wlan_cmd_finish(l, -EINVAL);
        break;
    }
}

void wlan_worker(void *arg)
{
    wlan_local_t *l = arg;
    for (;;) {
        u32 seq = __atomic_load_n(&l->events.seq, __ATOMIC_ACQUIRE);
        process_cmd(l);
        struct sk_buff *skb;
        while ((skb = skb_dequeue(&l->status_q))) wlan_tx_status(l, skb);
        int n = 0;
        while (n < 256 && (skb = skb_dequeue(&l->rx_q))) {
            wlan_rx_frame(l, skb);
            kpi_free_skb(skb);
            n++;
        }
        u64 now = sched_now_ns();
        u64 next = now + MS(1000);
        wlan_scan_step(l, now, &next);
        wlan_mlme_timeouts(l, now, &next);
        wlan_ba_timeouts(l, now, &next);
        if (!skb_queue_empty(&l->rx_q) || !skb_queue_empty(&l->status_q)) continue;
        now = sched_now_ns();
        if (next <= now) continue;
        kpi_wait_on(&l->events, seq, next - now);
    }
}

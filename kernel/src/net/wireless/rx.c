#include "wlan.h"
#include "../../../include/drivers/timer.h"

const u8 *wlan_find_ie(const u8 *ies, size_t len, u8 eid)
{
    size_t i = 0;
    while (i + 2 <= len) {
        size_t l = ies[i + 1];
        if (i + 2 + l > len) return NULL;
        if (ies[i] == eid) return &ies[i];
        i += 2 + l;
    }
    return NULL;
}

const u8 *wlan_find_vendor_ie(const u8 *ies, size_t len, u32 oui, u8 type)
{
    size_t i = 0;
    while (i + 2 <= len) {
        size_t l = ies[i + 1];
        if (i + 2 + l > len) return NULL;
        if (ies[i] == WLAN_EID_VENDOR_SPECIFIC && l >= 4 &&
            ies[i + 2] == (u8)(oui >> 16) && ies[i + 3] == (u8)(oui >> 8) &&
            ies[i + 4] == (u8)oui && ies[i + 5] == type)
            return &ies[i];
        i += 2 + l;
    }
    return NULL;
}

static u32 suite(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

bool wlan_parse_rsn(const u8 *ie, wlan_rsn_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!ie || ie[0] != WLAN_EID_RSN || ie[1] < 2) return false;
    size_t len = ie[1];
    const u8 *p = ie + 2, *end = ie + 2 + len;
    if ((p[0] | (p[1] << 8)) != 1) return false;
    p += 2;
    out->present = true;
    out->group = WLAN_CIPHER_SUITE_CCMP;
    out->pairwise = WLAN_CIPHER_SUITE_CCMP;
    out->akm = WLAN_AKM_SUITE_PSK;
    if (len <= sizeof(out->ie) - 2) {
        memcpy(out->ie, ie, len + 2);
        out->ie_len = (u8)(len + 2);
    }
    if (end - p < 4) return true;
    out->group = suite(p);
    p += 4;
    if (end - p < 2) return true;
    int n = p[0] | (p[1] << 8);
    p += 2;
    if (end - p < 4 * n) return false;
    out->pairwise = 0;
    for (int i = 0; i < n; i++) {
        u32 s = suite(p + 4 * i);
        if (s == WLAN_CIPHER_SUITE_CCMP) out->pairwise = s;
        else if (s == WLAN_CIPHER_SUITE_TKIP && !out->pairwise) out->pairwise = s;
        else if (!out->pairwise) out->pairwise = s;
    }
    p += 4 * n;
    if (end - p < 2) return true;
    n = p[0] | (p[1] << 8);
    p += 2;
    if (end - p < 4 * n) return false;
    out->akm = 0;
    for (int i = 0; i < n; i++) {
        u32 s = suite(p + 4 * i);
        if (s == WLAN_AKM_SUITE_PSK) out->akm = s;
        else if (!out->akm) out->akm = s;
    }
    p += 4 * n;
    if (end - p >= 2) out->caps = (u16)(p[0] | (p[1] << 8));
    return true;
}

u32 wlan_classify_security(u16 capab, const u8 *ies, size_t len)
{
    u32 sec = 0;
    const u8 *rsn = wlan_find_ie(ies, len, WLAN_EID_RSN);
    const u8 *wpa = wlan_find_vendor_ie(ies, len, 0x0050F2, 1);
    if (rsn) {
        size_t l = rsn[1];
        const u8 *p = rsn + 2, *end = rsn + 2 + l;
        if (end - p >= 6) {
            p += 2;
            u32 g = suite(p);
            p += 4;
            if (g == WLAN_CIPHER_SUITE_TKIP) sec |= WLAN_SEC_TKIP;
            if (g == WLAN_CIPHER_SUITE_CCMP) sec |= WLAN_SEC_CCMP;
            if (end - p >= 2) {
                int n = p[0] | (p[1] << 8);
                p += 2;
                for (int i = 0; i < n && end - p >= 4; i++, p += 4) {
                    u32 s = suite(p);
                    if (s == WLAN_CIPHER_SUITE_CCMP) sec |= WLAN_SEC_CCMP;
                    if (s == WLAN_CIPHER_SUITE_TKIP) sec |= WLAN_SEC_TKIP;
                }
            }
            bool psk = false, sae = false;
            if (end - p >= 2) {
                int n = p[0] | (p[1] << 8);
                p += 2;
                for (int i = 0; i < n && end - p >= 4; i++, p += 4) {
                    u32 s = suite(p);
                    if (s == WLAN_AKM_SUITE_PSK || s == 0x000FAC06) { sec |= WLAN_SEC_PSK; psk = true; }
                    else if (s == WLAN_AKM_SUITE_SAE || s == 0x000FAC18) { sec |= WLAN_SEC_SAE; sae = true; }
                    else if (s == 0x000FAC12) sec |= WLAN_SEC_OWE;
                    else sec |= WLAN_SEC_EAP;
                }
            }
            if (end - p >= 2) {
                u16 caps = (u16)(p[0] | (p[1] << 8));
                if (caps & 0x0040) sec |= WLAN_SEC_PMF_REQUIRED;
                if (caps & 0x0080) sec |= WLAN_SEC_PMF_CAPABLE;
            }
            if (sae) sec |= WLAN_SEC_WPA3;
            if (psk || (!sae && (sec & (WLAN_SEC_EAP | WLAN_SEC_OWE)))) sec |= WLAN_SEC_WPA2;
        } else {
            sec |= WLAN_SEC_WPA2 | WLAN_SEC_PSK | WLAN_SEC_CCMP;
        }
    }
    if (wpa) sec |= WLAN_SEC_WPA | WLAN_SEC_PSK | WLAN_SEC_TKIP;
    if (!rsn && !wpa) {
        if (capab & WLAN_CAPABILITY_PRIVACY) sec |= WLAN_SEC_WEP;
        else sec |= WLAN_SEC_OPEN;
    }
    bool ok = false;
    if (sec & WLAN_SEC_OPEN) ok = true;
    if ((sec & WLAN_SEC_WPA2) && (sec & WLAN_SEC_PSK) && (sec & WLAN_SEC_CCMP) &&
        !(sec & WLAN_SEC_PMF_REQUIRED) && !(sec & WLAN_SEC_TKIP))
        ok = true;
    if (ok) sec |= WLAN_SEC_SUPPORTED;
    return sec;
}

wlan_bss_t *wlan_bss_find(wlan_local_t *l, const u8 *bssid)
{
    for (int i = 0; i < WLAN_MAX_BSS; i++)
        if (l->bss[i].valid && ether_addr_equal(l->bss[i].bssid, bssid)) return &l->bss[i];
    return NULL;
}

void wlan_bss_update(wlan_local_t *l, struct sk_buff *skb, const struct ieee80211_rx_status *st)
{
    struct ieee80211_mgmt *m = (struct ieee80211_mgmt *)skb->data;
    size_t fixed = offsetof(struct ieee80211_mgmt, u.beacon.variable);
    if (skb->len < fixed) return;
    const u8 *ies = m->u.beacon.variable;
    size_t ielen = skb->len - fixed;
    const u8 *ssid = wlan_find_ie(ies, ielen, WLAN_EID_SSID);
    const u8 *ds = wlan_find_ie(ies, ielen, WLAN_EID_DS_PARAMS);
    int ch = ds && ds[1] >= 1 ? ds[2] : wlan_freq_to_channel(st->freq);
    if (st->freq && ds && ds[1] >= 1 && ch != wlan_freq_to_channel(st->freq) && st->band == NL80211_BAND_2GHZ) {
        int diff = ch - wlan_freq_to_channel(st->freq);
        if (diff < -4 || diff > 4) return;
    }
    u64 now = sched_now_ns();
    uint64_t f = spinlock_acquire_irqsave(&l->bss_lock);
    wlan_bss_t *b = wlan_bss_find(l, m->bssid);
    if (!b) {
        wlan_bss_t *oldest = NULL;
        for (int i = 0; i < WLAN_MAX_BSS; i++) {
            if (!l->bss[i].valid) { b = &l->bss[i]; break; }
            if (!oldest || l->bss[i].last_seen_ns < oldest->last_seen_ns) oldest = &l->bss[i];
        }
        if (!b) b = oldest;
        memset(b, 0, sizeof(*b));
        ether_addr_copy(b->bssid, m->bssid);
    }
    bool hidden_probe = ieee80211_is_beacon(m->frame_control) && ssid &&
                        (ssid[1] == 0 || ssid[2] == 0) && b->ssid_len;
    if (ssid && !hidden_probe) {
        b->ssid_len = ssid[1] > IEEE80211_MAX_SSID_LEN ? IEEE80211_MAX_SSID_LEN : ssid[1];
        memcpy(b->ssid, ssid + 2, b->ssid_len);
        bool zero = true;
        for (int i = 0; i < b->ssid_len; i++) if (b->ssid[i]) zero = false;
        if (zero) b->ssid_len = 0;
    }
    b->capab = m->u.beacon.capab_info;
    b->beacon_int = m->u.beacon.beacon_int;
    b->channel = (u8)ch;
    b->freq = (u16)ieee80211_channel_to_frequency(ch, (enum nl80211_band)st->band);
    if (!(st->flag & RX_FLAG_NO_SIGNAL_VAL)) b->signal = st->signal;
    const u8 *tim = wlan_find_ie(ies, ielen, WLAN_EID_TIM);
    if (tim && tim[1] >= 2) b->dtim_period = tim[3] ? tim[3] : 1;
    else if (!b->dtim_period) b->dtim_period = 1;
    if (!hidden_probe || !b->ie_len) {
        size_t keep = ielen > WLAN_IE_MAX ? WLAN_IE_MAX : ielen;
        memcpy(b->ies, ies, keep);
        b->ie_len = (u16)keep;
    }
    b->security = wlan_classify_security(b->capab, b->ies, b->ie_len);
    b->last_seen_ns = now;
    b->valid = true;
    spinlock_release_irqrestore(&l->bss_lock, f);
}

static void deliver_msdu(wlan_local_t *l, const u8 *da, const u8 *sa, const u8 *msdu, size_t len,
                         bool was_protected)
{
    if (len < 8) return;
    u16 ethertype;
    const u8 *payload;
    size_t plen;
    if (msdu[0] == 0xAA && msdu[1] == 0xAA && msdu[2] == 0x03 && msdu[3] == 0 && msdu[4] == 0 &&
        (msdu[5] == 0 || msdu[5] == 0xF8)) {
        ethertype = (u16)((msdu[6] << 8) | msdu[7]);
        payload = msdu + 8;
        plen = len - 8;
    } else {
        return;
    }
    if (ethertype == 0x888E) {
        wlan_eapol_rx(l, sa, payload, plen);
        return;
    }
    if (!l->authorized) return;
    if (l->pkey && !was_protected) return;
    if (plen + 14 > sizeof(l->rxbuf)) return;
    u8 *e = l->rxbuf;
    memcpy(e, da, 6);
    memcpy(e + 6, sa, 6);
    e[12] = (u8)(ethertype >> 8);
    e[13] = (u8)ethertype;
    memcpy(e + 14, payload, plen);
    l->rx_bytes += plen;
    wlan_netdev_rx(l, e, plen + 14);
}

static void rx_data(wlan_local_t *l, struct sk_buff *skb, const struct ieee80211_rx_status *st)
{
    struct ieee80211_hdr *h = (struct ieee80211_hdr *)skb->data;
    u16 fc = h->frame_control;
    if (!l->ap_sta) return;
    if (!(fc & IEEE80211_FCTL_FROMDS) || (fc & IEEE80211_FCTL_TODS)) return;
    if (!ether_addr_equal(h->addr2, l->ap_sta->addr)) return;
    bool mcast = is_multicast_ether_addr(h->addr1);
    if (!mcast && !ether_addr_equal(h->addr1, l->vif->addr)) return;
    if (mcast && ether_addr_equal(h->addr3, l->vif->addr)) return;
    l->last_ap_rx_ns = sched_now_ns();
    l->probe_misses = 0;
    if (!(st->flag & RX_FLAG_NO_SIGNAL_VAL)) l->last_signal = st->signal;
    if (!ieee80211_is_data_present(fc)) return;
    if ((fc & IEEE80211_FCTL_MOREFRAGS) || (h->seq_ctrl & IEEE80211_SCTL_FRAG)) return;
    unsigned int hdrlen = ieee80211_hdrlen(fc);
    if (skb->len < hdrlen) return;
    bool qos = ieee80211_is_data_qos(fc);
    u8 tid = IEEE80211_NUM_TIDS;
    bool amsdu = false;
    if (qos) {
        u8 *q = ieee80211_get_qos_ctl(h);
        tid = q[0] & IEEE80211_QOS_CTL_TID_MASK;
        amsdu = (q[0] & 0x80) != 0;
    }
    u16 seq = (u16)(h->seq_ctrl >> 4);
    if (!mcast) {
        if ((fc & IEEE80211_FCTL_RETRY) && l->rx_seq_valid[tid] && l->rx_last_seq[tid] == seq) return;
        l->rx_last_seq[tid] = seq;
        l->rx_seq_valid[tid] = true;
    }
    u8 *body = skb->data + hdrlen;
    size_t blen = skb->len - hdrlen;
    bool prot = (fc & IEEE80211_FCTL_PROTECTED) != 0;
    if (prot) {
        if (!(st->flag & RX_FLAG_DECRYPTED)) return;
        if (!(st->flag & RX_FLAG_IV_STRIPPED)) {
            if (blen < 8) return;
            u8 *iv = body;
            if (!(iv[3] & 0x20)) return;
            u8 keyid = iv[3] >> 6;
            u64 pn = (u64)iv[0] | ((u64)iv[1] << 8) | ((u64)iv[4] << 16) | ((u64)iv[5] << 24) |
                     ((u64)iv[6] << 32) | ((u64)iv[7] << 40);
            u64 *rsc;
            if (mcast) {
                if (!l->gkey[keyid]) return;
                rsc = &l->rx_pn_gtk[keyid];
            } else {
                if (!l->pkey) return;
                rsc = &l->rx_pn_ptk[tid];
            }
            if (!(st->flag & RX_FLAG_PN_VALIDATED)) {
                if (pn <= *rsc) return;
                *rsc = pn;
            }
            body += 8;
            blen -= 8;
        }
        if (!(st->flag & RX_FLAG_MIC_STRIPPED)) {
            if (blen < 8) return;
            blen -= 8;
        }
    }
    if (!amsdu) {
        deliver_msdu(l, h->addr1, h->addr3, body, blen, prot);
        return;
    }
    size_t off = 0;
    while (off + 14 <= blen) {
        const u8 *sub = body + off;
        size_t sl = (size_t)((sub[12] << 8) | sub[13]);
        if (off + 14 + sl > blen) break;
        deliver_msdu(l, sub, sub + 6, sub + 14, sl, prot);
        off += 14 + sl;
        off = (off + 3) & ~(size_t)3;
    }
}

void wlan_rx_frame(wlan_local_t *l, struct sk_buff *skb)
{
    struct ieee80211_rx_status st;
    memcpy(&st, skb->cb, sizeof(st));
    if (ieee80211_hw_check(&l->hw, RX_INCLUDES_FCS)) {
        if (skb->len < 4) return;
        skb_trim(skb, skb->len - 4);
    }
    if (st.flag & (RX_FLAG_FAILED_FCS_CRC | RX_FLAG_FAILED_PLCP_CRC | RX_FLAG_NO_PSDU)) return;
    if (skb->len < 10) return;
    struct ieee80211_hdr *h = (struct ieee80211_hdr *)skb->data;
    u16 fc = h->frame_control;
    if (ieee80211_is_mgmt(fc)) {
        if (skb->len < 24) return;
        if (ieee80211_is_beacon(fc) || ieee80211_is_probe_resp(fc)) {
            wlan_bss_update(l, skb, &st);
            if (l->ap_sta && ether_addr_equal(h->addr3, l->ap_sta->addr)) {
                l->last_ap_rx_ns = sched_now_ns();
                l->probe_misses = 0;
                if (!(st.flag & RX_FLAG_NO_SIGNAL_VAL)) l->last_signal = st.signal;
            }
            return;
        }
        if (!ether_addr_equal(h->addr1, l->vif->addr) && !is_broadcast_ether_addr(h->addr1)) return;
        wlan_mlme_rx_mgmt(l, skb);
        return;
    }
    if (ieee80211_is_data(fc) && skb->len >= 24) rx_data(l, skb, &st);
}

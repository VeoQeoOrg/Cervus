#include "wlan.h"

#define TXQ_LIMIT 256

static const u8 g_ac_of_tid[8] = {
    IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_BK, IEEE80211_AC_BE,
    IEEE80211_AC_VI, IEEE80211_AC_VI, IEEE80211_AC_VO, IEEE80211_AC_VO,
};

int wlan_tx_mgmt(wlan_local_t *l, struct sk_buff *skb, bool want_ack)
{
    if (!l->started) {
        kpi_free_skb(skb);
        return -ENETDOWN;
    }
    struct ieee80211_hdr *h = (struct ieee80211_hdr *)skb->data;
    struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
    memset(info, 0, sizeof(*info));
    info->control.vif = l->vif;
    info->band = l->cur_chan ? l->cur_chan->band : NL80211_BAND_2GHZ;
    info->flags = IEEE80211_TX_CTL_FIRST_FRAGMENT;
    if (is_multicast_ether_addr(h->addr1)) info->flags |= IEEE80211_TX_CTL_NO_ACK;
    int cookie = 0;
    if (want_ack && !(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
        info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
        uint64_t f = spinlock_acquire_irqsave(&l->lock);
        cookie = (int)(++l->ack_cookie ? l->ack_cookie : ++l->ack_cookie);
        l->ack_wait_cookie = (u32)cookie;
        l->ack_result = -1;
        spinlock_release_irqrestore(&l->lock, f);
        skb->kpi_cookie = (u32)cookie;
    }
    skb->priority = 7;
    skb_set_queue_mapping(skb, IEEE80211_AC_VO);
    struct ieee80211_tx_control ctl = { .sta = NULL };
    if (l->ap_sta && ether_addr_equal(h->addr1, l->ap_sta->addr)) ctl.sta = l->ap_sta;
    l->ops->tx(&l->hw, &ctl, skb);
    return cookie;
}

void wlan_tx_status(wlan_local_t *l, struct sk_buff *skb)
{
    struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
    uint64_t f = spinlock_acquire_irqsave(&l->lock);
    if (skb->kpi_cookie && skb->kpi_cookie == l->ack_wait_cookie)
        l->ack_result = (info->flags & IEEE80211_TX_STAT_ACK) ? 1 : 0;
    spinlock_release_irqrestore(&l->lock, f);
    kpi_free_skb(skb);
}

int wlan_tx_data(wlan_local_t *l, const u8 *dst, const u8 *src, u16 ethertype,
                 const void *payload, size_t len, bool eapol)
{
    struct ieee80211_sta *sta = l->ap_sta;
    if (!sta || !l->started) return -ENOTCONN;
    if (len > 2304 - 8) return -EMSGSIZE;
    bool qos = l->ap_wmm;
    u8 tid = eapol ? 7 : 0;
    struct ieee80211_txq *txq = sta->txq[qos ? tid : IEEE80211_NUM_TIDS];
    if (!txq) txq = sta->txq[0];
    if (!txq) return -ENOTCONN;
    if (skb_queue_len(&txq->frags) >= TXQ_LIMIT) return -ENOBUFS;
    size_t hdrlen = qos ? 26 : 24;
    struct sk_buff *skb = wlan_alloc_frame(l, hdrlen + 8 + 8 + len);
    if (!skb) return -ENOMEM;

    uint64_t f = spinlock_acquire_irqsave(&l->lock);
    struct ieee80211_key_conf *key = l->pkey;
    struct ieee80211_qos_hdr *h = skb_put_zero(skb, (unsigned int)hdrlen);
    u16 fc = IEEE80211_FTYPE_DATA | (qos ? IEEE80211_STYPE_QOS_DATA : IEEE80211_STYPE_DATA) |
             IEEE80211_FCTL_TODS;
    if (key) fc |= IEEE80211_FCTL_PROTECTED;
    h->frame_control = fc;
    ether_addr_copy(h->addr1, sta->addr);
    ether_addr_copy(h->addr2, src);
    ether_addr_copy(h->addr3, dst);
    u16 *seqp = &l->seq[qos ? tid : IEEE80211_NUM_TIDS];
    h->seq_ctrl = (u16)((*seqp & 0xfff) << 4);
    *seqp = (u16)((*seqp + 1) & 0xfff);
    if (qos) h->qos_ctrl = tid;
    if (key) {
        u64 pn = ++l->tx_pn;
        u8 *iv = skb_put(skb, 8);
        iv[0] = (u8)pn;
        iv[1] = (u8)(pn >> 8);
        iv[2] = 0;
        iv[3] = (u8)(0x20 | ((key->keyidx & 3) << 6));
        iv[4] = (u8)(pn >> 16);
        iv[5] = (u8)(pn >> 24);
        iv[6] = (u8)(pn >> 32);
        iv[7] = (u8)(pn >> 40);
    }
    u8 *llc = skb_put(skb, 8);
    llc[0] = 0xAA; llc[1] = 0xAA; llc[2] = 0x03;
    llc[3] = 0x00; llc[4] = 0x00;
    llc[5] = (ethertype == 0x80F3 || ethertype == 0x8137) ? 0xF8 : 0x00;
    llc[6] = (u8)(ethertype >> 8);
    llc[7] = (u8)ethertype;
    skb_put_data(skb, payload, (unsigned int)len);

    struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
    memset(info, 0, sizeof(*info));
    info->control.vif = l->vif;
    info->control.hw_key = key;
    info->band = l->cur_chan ? l->cur_chan->band : NL80211_BAND_2GHZ;
    info->flags = IEEE80211_TX_CTL_FIRST_FRAGMENT;
    if (eapol) info->flags |= IEEE80211_TX_CTL_USE_MINRATE;
    skb->priority = tid;
    skb_set_queue_mapping(skb, qos ? g_ac_of_tid[tid] : IEEE80211_AC_BE);
    skb_queue_tail(&txq->frags, skb);
    spinlock_release_irqrestore(&l->lock, f);

    l->tx_bytes += len;
    if (l->ops->wake_tx_queue) l->ops->wake_tx_queue(&l->hw, txq);
    else {
        struct sk_buff *s;
        while ((s = skb_dequeue(&txq->frags))) {
            struct ieee80211_tx_control ctl = { .sta = sta };
            l->ops->tx(&l->hw, &ctl, s);
        }
    }
    return 0;
}

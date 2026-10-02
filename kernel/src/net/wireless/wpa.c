#include "wlan.h"
#include "../../../include/drivers/timer.h"

#define KI_VER_MASK  0x0007
#define KI_PAIRWISE  0x0008
#define KI_INSTALL   0x0040
#define KI_ACK       0x0080
#define KI_MIC       0x0100
#define KI_SECURE    0x0200
#define KI_ERROR     0x0400
#define KI_REQUEST   0x0800
#define KI_ENCDATA   0x1000

#define EK_DESC      4
#define EK_INFO      5
#define EK_KEYLEN    7
#define EK_REPLAY    9
#define EK_NONCE     17
#define EK_IV        49
#define EK_RSC       65
#define EK_ID        73
#define EK_MIC       81
#define EK_DATALEN   97
#define EK_DATA      99

static u16 be16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static void put_be16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static u64 be64(const u8 *p)
{
    u64 v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

void wlan_wpa_reset(wlan_local_t *l)
{
    l->replay_valid = false;
    l->replay_ctr = 0;
    l->ptk_valid = false;
    memset(l->anonce, 0, sizeof(l->anonce));
    crypto_wipe(l->snonce, sizeof(l->snonce));
}

static void derive_ptk(wlan_local_t *l)
{
    u8 data[76];
    const u8 *aa = l->cur.bssid, *spa = l->vif->addr;
    bool a_first = memcmp(aa, spa, ETH_ALEN) < 0;
    memcpy(data, a_first ? aa : spa, ETH_ALEN);
    memcpy(data + 6, a_first ? spa : aa, ETH_ALEN);
    bool n_first = memcmp(l->anonce, l->snonce, 32) < 0;
    memcpy(data + 12, n_first ? l->anonce : l->snonce, 32);
    memcpy(data + 44, n_first ? l->snonce : l->anonce, 32);
    u8 ptk[48];
    sha1_prf(l->pmk, 32, "Pairwise key expansion", data, sizeof(data), ptk, sizeof(ptk));
    memcpy(l->kck, ptk, 16);
    memcpy(l->kek, ptk + 16, 16);
    memcpy(l->tk, ptk + 32, 16);
    crypto_wipe(ptk, sizeof(ptk));
    l->ptk_valid = true;
}

static void compute_mic(wlan_local_t *l, int ver, const u8 *frame, size_t len, u8 out[16])
{
    if (ver == 3) {
        aes_cmac(l->kck, 16, frame, len, out);
        return;
    }
    u8 h[20];
    hmac_sha1(l->kck, 16, frame, len, h);
    memcpy(out, h, 16);
    crypto_wipe(h, sizeof(h));
}

static bool check_mic(wlan_local_t *l, int ver, const u8 *frame, size_t len)
{
    u8 *copy = kmalloc(len, GFP_KERNEL);
    if (!copy) return false;
    memcpy(copy, frame, len);
    memset(copy + EK_MIC, 0, 16);
    u8 mic[16];
    compute_mic(l, ver, copy, len, mic);
    bool ok = crypto_memeq(mic, frame + EK_MIC, 16);
    kfree(copy);
    return ok;
}

static void send_key(wlan_local_t *l, u8 eapol_ver, int ver, u16 info, const u8 *replay,
                     const u8 *nonce, const u8 *data, size_t dlen)
{
    size_t body = 95 + dlen;
    size_t total = 4 + body;
    u8 *f = kzalloc(total, GFP_KERNEL);
    if (!f) return;
    f[0] = eapol_ver;
    f[1] = 3;
    put_be16(f + 2, (u16)body);
    f[EK_DESC] = 2;
    put_be16(f + EK_INFO, info);
    put_be16(f + EK_KEYLEN, 0);
    memcpy(f + EK_REPLAY, replay, 8);
    if (nonce) memcpy(f + EK_NONCE, nonce, 32);
    put_be16(f + EK_DATALEN, (u16)dlen);
    if (dlen) memcpy(f + EK_DATA, data, dlen);
    if (info & KI_MIC) compute_mic(l, ver, f, total, f + EK_MIC);
    wlan_tx_data(l, l->cur.bssid, l->vif->addr, 0x888E, f, total, true);
    kfree(f);
}

static struct ieee80211_key_conf *new_key(u32 cipher, int idx, const u8 *key, size_t len, bool pairwise)
{
    struct ieee80211_key_conf *k = kzalloc(sizeof(*k), GFP_KERNEL);
    if (!k) return NULL;
    k->cipher = cipher;
    k->keyidx = (s8)idx;
    k->keylen = (u8)len;
    k->icv_len = 8;
    k->iv_len = 8;
    k->link_id = -1;
    memcpy(k->key, key, len);
    if (pairwise) k->flags |= IEEE80211_KEY_FLAG_PAIRWISE;
    return k;
}

static int install_key(wlan_local_t *l, struct ieee80211_key_conf *k, struct ieee80211_sta *sta)
{
    if (!l->ops->set_key) return -EOPNOTSUPP;
    return l->ops->set_key(&l->hw, SET_KEY, l->vif, sta, k);
}

static void remove_key(wlan_local_t *l, struct ieee80211_key_conf *k, struct ieee80211_sta *sta)
{
    if (!k) return;
    if (l->ops->set_key) l->ops->set_key(&l->hw, DISABLE_KEY, l->vif, sta, k);
    crypto_wipe(k->key, sizeof(k->key));
    kfree(k);
}

void wlan_keys_clear(wlan_local_t *l)
{
    uint64_t f = spinlock_acquire_irqsave(&l->lock);
    struct ieee80211_key_conf *pk = l->pkey;
    struct ieee80211_key_conf *gk[4];
    l->pkey = NULL;
    for (int i = 0; i < 4; i++) { gk[i] = l->gkey[i]; l->gkey[i] = NULL; }
    spinlock_release_irqrestore(&l->lock, f);
    remove_key(l, pk, l->ap_sta);
    for (int i = 0; i < 4; i++) remove_key(l, gk[i], NULL);
    memset(l->rx_pn_ptk, 0, sizeof(l->rx_pn_ptk));
    memset(l->rx_pn_gtk, 0, sizeof(l->rx_pn_gtk));
    l->tx_pn = 0;
}

static bool find_gtk(const u8 *kd, size_t len, int *idx, const u8 **gtk, size_t *glen)
{
    size_t i = 0;
    while (i + 2 <= len) {
        if (kd[i] == 0xdd && kd[i + 1] == 0) break;
        size_t l = kd[i + 1];
        if (i + 2 + l > len) break;
        if (kd[i] == 0xdd && l >= 6 + 16 && kd[i + 2] == 0x00 && kd[i + 3] == 0x0F &&
            kd[i + 4] == 0xAC && kd[i + 5] == 0x01) {
            *idx = kd[i + 6] & 3;
            *gtk = kd + i + 8;
            *glen = l - 6;
            return true;
        }
        i += 2 + l;
    }
    return false;
}

static bool install_gtk(wlan_local_t *l, const u8 *kd, size_t kdlen, const u8 *rsc)
{
    int idx;
    const u8 *gtk;
    size_t glen;
    if (!find_gtk(kd, kdlen, &idx, &gtk, &glen)) return false;
    if (glen != 16 || l->ap_rsn.group != WLAN_CIPHER_SUITE_CCMP) return false;
    if (idx == 0) return false;
    struct ieee80211_key_conf *k = new_key(WLAN_CIPHER_SUITE_CCMP, idx, gtk, glen, false);
    if (!k) return false;
    if (install_key(l, k, NULL)) {
        crypto_wipe(k->key, sizeof(k->key));
        kfree(k);
        return false;
    }
    u64 pn = 0;
    for (int i = 5; i >= 0; i--) pn = (pn << 8) | rsc[i];
    uint64_t f = spinlock_acquire_irqsave(&l->lock);
    struct ieee80211_key_conf *old = l->gkey[idx];
    l->gkey[idx] = k;
    l->rx_pn_gtk[idx] = pn;
    spinlock_release_irqrestore(&l->lock, f);
    if (old) {
        crypto_wipe(old->key, sizeof(old->key));
        kfree(old);
    }
    return true;
}

static bool rsn_matches_beacon(wlan_local_t *l, const u8 *kd, size_t kdlen)
{
    const u8 *ie = wlan_find_ie(kd, kdlen, WLAN_EID_RSN);
    if (!ie) return false;
    if (!l->ap_rsn.ie_len) return true;
    return ie[1] + 2 == l->ap_rsn.ie_len && !memcmp(ie, l->ap_rsn.ie, l->ap_rsn.ie_len);
}

static void handle_msg1(wlan_local_t *l, u8 ev, int ver, const u8 *f)
{
    if (l->replay_valid && be64(f + EK_REPLAY) <= l->replay_ctr) return;
    if (!l->ptk_valid || memcmp(l->anonce, f + EK_NONCE, 32)) {
        memcpy(l->anonce, f + EK_NONCE, 32);
        kpi_random_bytes(l->snonce, 32);
        derive_ptk(l);
    }
    send_key(l, ev, ver, (u16)(ver | KI_PAIRWISE | KI_MIC), f + EK_REPLAY, l->snonce,
             l->own_rsn.ie, l->own_rsn.ie_len);
    l->deadline_ns = sched_now_ns() + 8000000000ULL;
}

static void handle_msg3(wlan_local_t *l, u8 ev, int ver, const u8 *f, size_t flen)
{
    if (!l->ptk_valid || memcmp(l->anonce, f + EK_NONCE, 32)) return;
    if (!check_mic(l, ver, f, flen)) {
        WLAN_LOG(l, "%s: message 3 of the handshake has a bad MIC\n", l->name);
        return;
    }
    u64 rc = be64(f + EK_REPLAY);
    if (l->replay_valid && rc <= l->replay_ctr) return;
    l->replay_ctr = rc;
    l->replay_valid = true;
    u16 dlen = be16(f + EK_DATALEN);
    if (dlen < 24 || dlen % 8 || EK_DATA + (size_t)dlen > flen) return;
    u8 *kd = kmalloc(dlen, GFP_KERNEL);
    if (!kd) return;
    if (aes_key_unwrap(l->kek, 16, f + EK_DATA, dlen, kd)) {
        kfree(kd);
        wlan_mlme_disconnect(l, 2, true, "could not decrypt the group key");
        return;
    }
    size_t kdlen = dlen - 8;
    if (!rsn_matches_beacon(l, kd, kdlen)) {
        crypto_wipe(kd, kdlen);
        kfree(kd);
        wlan_mlme_disconnect(l, 17, true, "RSN element mismatch, possible downgrade attack");
        return;
    }
    u8 zero[32] = { 0 };
    send_key(l, ev, ver, (u16)(ver | KI_PAIRWISE | KI_MIC | KI_SECURE), f + EK_REPLAY, zero, NULL, 0);
    struct ieee80211_key_conf *pk = new_key(WLAN_CIPHER_SUITE_CCMP, 0, l->tk, 16, true);
    if (!pk || install_key(l, pk, l->ap_sta)) {
        if (pk) { crypto_wipe(pk->key, sizeof(pk->key)); kfree(pk); }
        crypto_wipe(kd, kdlen);
        kfree(kd);
        wlan_mlme_disconnect(l, 1, true, "the driver could not install the pairwise key");
        return;
    }
    uint64_t fl = spinlock_acquire_irqsave(&l->lock);
    struct ieee80211_key_conf *old = l->pkey;
    l->pkey = pk;
    l->tx_pn = 0;
    memset(l->rx_pn_ptk, 0, sizeof(l->rx_pn_ptk));
    spinlock_release_irqrestore(&l->lock, fl);
    if (old) remove_key(l, old, l->ap_sta);
    bool g = install_gtk(l, kd, kdlen, f + EK_RSC);
    crypto_wipe(kd, kdlen);
    kfree(kd);
    if (!g) {
        wlan_mlme_disconnect(l, 1, true, "no usable group key in message 3");
        return;
    }
    wlan_set_authorized(l, true);
}

static void handle_group(wlan_local_t *l, u8 ev, int ver, const u8 *f, size_t flen)
{
    if (!l->ptk_valid || !check_mic(l, ver, f, flen)) return;
    u64 rc = be64(f + EK_REPLAY);
    if (l->replay_valid && rc <= l->replay_ctr) return;
    l->replay_ctr = rc;
    l->replay_valid = true;
    u16 dlen = be16(f + EK_DATALEN);
    if (dlen < 24 || dlen % 8 || EK_DATA + (size_t)dlen > flen) return;
    u8 *kd = kmalloc(dlen, GFP_KERNEL);
    if (!kd) return;
    if (aes_key_unwrap(l->kek, 16, f + EK_DATA, dlen, kd)) {
        kfree(kd);
        return;
    }
    bool ok = install_gtk(l, kd, dlen - 8, f + EK_RSC);
    crypto_wipe(kd, dlen);
    kfree(kd);
    if (!ok) return;
    send_key(l, ev, ver, (u16)(ver | KI_MIC | KI_SECURE), f + EK_REPLAY, NULL, NULL, 0);
    WLAN_LOG(l, "%s: group key renewed\n", l->name);
}

void wlan_eapol_rx(wlan_local_t *l, const u8 *src, const u8 *data, size_t len)
{
    if (l->state != WLAN_ST_HANDSHAKE && l->state != WLAN_ST_CONNECTED) return;
    if (!l->ap_rsn.present || !ether_addr_equal(src, l->cur.bssid)) return;
    if (len < 4 || data[1] != 3) return;
    size_t body = be16(data + 2);
    if (4 + body > len || body < 95) return;
    size_t flen = 4 + body;
    if (data[EK_DESC] != 2) return;
    u16 info = be16(data + EK_INFO);
    int ver = info & KI_VER_MASK;
    if (ver != 2 && ver != 3) return;
    if (EK_DATA + (size_t)be16(data + EK_DATALEN) > flen) return;
    u8 ev = data[0] > 2 ? 2 : (data[0] ? data[0] : 1);
    if ((info & KI_REQUEST) || (info & KI_ERROR)) return;
    if (info & KI_PAIRWISE) {
        if ((info & KI_ACK) && !(info & KI_MIC)) handle_msg1(l, ev, ver, data);
        else if ((info & KI_ACK) && (info & KI_MIC) && (info & KI_INSTALL) && (info & KI_ENCDATA))
            handle_msg3(l, ev, ver, data, flen);
        return;
    }
    if ((info & KI_ACK) && (info & KI_MIC) && (info & KI_SECURE) && (info & KI_ENCDATA) &&
        l->state == WLAN_ST_CONNECTED)
        handle_group(l, ev, ver, data, flen);
}

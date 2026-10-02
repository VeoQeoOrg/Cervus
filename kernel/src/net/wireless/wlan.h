#ifndef CERVUS_NET_WIRELESS_WLAN_H
#define CERVUS_NET_WIRELESS_WLAN_H

#include "mac80211.h"
#include "../../../include/net/netdev.h"
#include "../../../include/net/wlan_ioctl.h"
#include "../../../include/crypto/crypto.h"

#define WLAN_MAX_BSS      64
#define WLAN_IE_MAX       768
#define WLAN_MAX_CHANNELS 64
#define WLAN_BSS_EXPIRE_NS (120ULL * 1000000000ULL)

typedef struct wlan_bss {
    bool     valid;
    u8       bssid[ETH_ALEN];
    u8       ssid[IEEE80211_MAX_SSID_LEN];
    u8       ssid_len;
    u16      capab;
    u16      beacon_int;
    u8       channel;
    u16      freq;
    s8       signal;
    u8       dtim_period;
    u32      security;
    u64      last_seen_ns;
    u16      ie_len;
    u8       ies[WLAN_IE_MAX];
} wlan_bss_t;

enum wlan_state {
    WLAN_ST_IDLE = 0,
    WLAN_ST_AUTH,
    WLAN_ST_ASSOC,
    WLAN_ST_HANDSHAKE,
    WLAN_ST_CONNECTED,
};

enum wlan_cmd_kind {
    WLAN_CMD_NONE = 0,
    WLAN_CMD_SCAN,
    WLAN_CMD_CONNECT,
    WLAN_CMD_DISCONNECT,
};

typedef struct wlan_rsn {
    bool present;
    u32  group;
    u32  pairwise;
    u32  akm;
    u16  caps;
    u8   ie[64];
    u8   ie_len;
} wlan_rsn_t;

typedef struct wlan_local {
    struct ieee80211_hw     hw;
    struct wiphy            wiphy;
    const struct ieee80211_ops *ops;
    char                    name[NETDEV_NAME_MAX];
    netdev_t               *ndev;
    struct ieee80211_vif   *vif;
    struct ieee80211_sta   *ap_sta;
    void                   *thread;
    wait_queue_head_t       events;
    spinlock_t              lock;
    struct sk_buff_head     rx_q;
    struct sk_buff_head     status_q;
    struct workqueue_struct *hw_wq;
    bool                    registered;
    bool                    started;
    unsigned long           stopped_queues;
    struct ieee80211_supported_band *band;
    struct ieee80211_channel *chans[WLAN_MAX_CHANNELS];
    int                     nchans;
    struct ieee80211_channel *cur_chan;

    struct {
        enum wlan_cmd_kind kind;
        struct wlan_connect_req conn;
        int                result;
        volatile int       busy;
        volatile int       done;
        wait_queue_head_t  wait;
        kmutex_t           serial;
    } cmd;

    wlan_bss_t              bss[WLAN_MAX_BSS];
    spinlock_t              bss_lock;
    u32                     scan_gen;

    bool                    scanning;
    int                     scan_idx;
    u64                     scan_next_ns;
    bool                    scan_for_connect;
    u8                      scan_ssid[IEEE80211_MAX_SSID_LEN];
    u8                      scan_ssid_len;
    u64                     scan_done_ns;

    enum wlan_state         state;
    bool                    want_connected;
    struct wlan_connect_req target;
    wlan_bss_t              cur;
    wlan_rsn_t              ap_rsn;
    wlan_rsn_t              own_rsn;
    bool                    ap_wmm;
    bool                    ap_ht;
    u16                     aid;
    int                     tries;
    u64                     deadline_ns;
    u64                     reconnect_ns;
    int                     fail_count;
    int                     hs_failures;
    u16                     last_reason;
    int                     last_error;
    char                    last_msg[64];

    u64                     last_ap_rx_ns;
    u64                     probe_sent_ns;
    int                     probe_misses;
    bool                    probe_pending;
    u64                     connected_ns;

    u16                     seq[IEEE80211_NUM_TIDS + 1];
    u16                     rx_last_seq[IEEE80211_NUM_TIDS + 1];
    bool                    rx_seq_valid[IEEE80211_NUM_TIDS + 1];
    u32                     ack_cookie;
    u32                     ack_wait_cookie;
    int                     ack_result;

    bool                    authorized;
    u8                      pmk[32];
    u8                      anonce[32];
    u8                      snonce[32];
    u8                      kck[16];
    u8                      kek[16];
    u8                      tk[16];
    bool                    ptk_valid;
    u64                     replay_ctr;
    bool                    replay_valid;
    struct ieee80211_key_conf *pkey;
    struct ieee80211_key_conf *gkey[4];
    u64                     rx_pn_ptk[IEEE80211_NUM_TIDS + 1];
    u64                     rx_pn_gtk[4];
    u64                     tx_pn;

    u8                      ba_state[8];
    u8                      ba_token[8];
    u64                     ba_deadline[8];
    u8                      dialog_token;

    u8                      rxbuf[2400];
    u64                     rx_bytes, tx_bytes;
    s8                      last_signal;
    u16                     last_rate;

    u8                      priv[] __aligned(32);
} wlan_local_t;

static inline wlan_local_t *hw_to_local(struct ieee80211_hw *hw)
{
    return container_of(hw, wlan_local_t, hw);
}

static inline wlan_local_t *vif_to_local(struct ieee80211_vif *vif)
{
    return (wlan_local_t *)vif->kpi_owner;
}

int  wlan_freq_to_channel(int freq);
const u8 *wlan_find_ie(const u8 *ies, size_t len, u8 eid);
const u8 *wlan_find_vendor_ie(const u8 *ies, size_t len, u32 oui, u8 type);
bool wlan_parse_rsn(const u8 *ie, wlan_rsn_t *out);
u32  wlan_classify_security(u16 capab, const u8 *ies, size_t len);

void wlan_kick(wlan_local_t *l);
void wlan_worker(void *arg);
void wlan_rx_frame(wlan_local_t *l, struct sk_buff *skb);
void wlan_tx_status(wlan_local_t *l, struct sk_buff *skb);
int  wlan_tx_mgmt(wlan_local_t *l, struct sk_buff *skb, bool want_ack);
int  wlan_tx_data(wlan_local_t *l, const u8 *dst, const u8 *src, u16 ethertype,
                  const void *payload, size_t len, bool eapol);
struct sk_buff *wlan_alloc_frame(wlan_local_t *l, size_t len);
void wlan_set_channel(wlan_local_t *l, struct ieee80211_channel *c);
void wlan_bss_update(wlan_local_t *l, struct sk_buff *skb, const struct ieee80211_rx_status *st);
wlan_bss_t *wlan_bss_find(wlan_local_t *l, const u8 *bssid);

void wlan_mlme_rx_mgmt(wlan_local_t *l, struct sk_buff *skb);
void wlan_mlme_disconnect(wlan_local_t *l, u16 reason, bool send_deauth, const char *why);
void wlan_mlme_timeouts(wlan_local_t *l, u64 now, u64 *next);
void wlan_mlme_start_connect(wlan_local_t *l);
void wlan_scan_start(wlan_local_t *l, const u8 *ssid, u8 ssid_len, bool for_connect);
void wlan_scan_step(wlan_local_t *l, u64 now, u64 *next);
void wlan_set_authorized(wlan_local_t *l, bool on);
void wlan_ba_rx_action(wlan_local_t *l, const u8 *frame, size_t len);
void wlan_ba_timeouts(wlan_local_t *l, u64 now, u64 *next);
void wlan_ba_reset(wlan_local_t *l);
size_t wlan_put_rates(wlan_local_t *l, u8 *p);
size_t wlan_put_ht_cap(wlan_local_t *l, u8 *p);

void wlan_eapol_rx(wlan_local_t *l, const u8 *src, const u8 *data, size_t len);
void wlan_wpa_reset(wlan_local_t *l);
void wlan_keys_clear(wlan_local_t *l);

int  wlan_ctl_register(wlan_local_t *l);
int  wlan_netdev_register(wlan_local_t *l);
void wlan_netdev_rx(wlan_local_t *l, const u8 *frame, size_t len);

struct ieee80211_txq *wlan_txq_alloc(wlan_local_t *l, struct ieee80211_vif *vif,
                                     struct ieee80211_sta *sta, u8 tid);
void wlan_txq_free(struct ieee80211_txq *q);
void wlan_cmd_finish(wlan_local_t *l, int result);

static inline void crypto_wipe_local(wlan_local_t *l)
{
    crypto_wipe(l->kck, sizeof(l->kck));
    crypto_wipe(l->kek, sizeof(l->kek));
    crypto_wipe(l->tk, sizeof(l->tk));
    crypto_wipe(l->snonce, sizeof(l->snonce));
}

#define WLAN_LOG(l, ...) serial_printf_lvl(LOG_LEVEL_INFO, __VA_ARGS__)

#endif

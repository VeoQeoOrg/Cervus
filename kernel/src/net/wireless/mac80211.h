#ifndef CERVUS_KPI_MAC80211_H
#define CERVUS_KPI_MAC80211_H

#include "../../compat/linuxkpi/kpi.h"

#define IEEE80211_FCTL_VERS       0x0003
#define IEEE80211_FCTL_FTYPE      0x000c
#define IEEE80211_FCTL_STYPE      0x00f0
#define IEEE80211_FCTL_TODS       0x0100
#define IEEE80211_FCTL_FROMDS     0x0200
#define IEEE80211_FCTL_MOREFRAGS  0x0400
#define IEEE80211_FCTL_RETRY      0x0800
#define IEEE80211_FCTL_PM         0x1000
#define IEEE80211_FCTL_MOREDATA   0x2000
#define IEEE80211_FCTL_PROTECTED  0x4000
#define IEEE80211_FCTL_ORDER      0x8000
#define IEEE80211_SCTL_FRAG       0x000F
#define IEEE80211_SCTL_SEQ        0xFFF0
#define IEEE80211_FTYPE_MGMT      0x0000
#define IEEE80211_FTYPE_CTL       0x0004
#define IEEE80211_FTYPE_DATA      0x0008
#define IEEE80211_STYPE_ASSOC_REQ     0x0000
#define IEEE80211_STYPE_ASSOC_RESP    0x0010
#define IEEE80211_STYPE_REASSOC_REQ   0x0020
#define IEEE80211_STYPE_REASSOC_RESP  0x0030
#define IEEE80211_STYPE_PROBE_REQ     0x0040
#define IEEE80211_STYPE_PROBE_RESP    0x0050
#define IEEE80211_STYPE_BEACON        0x0080
#define IEEE80211_STYPE_ATIM          0x0090
#define IEEE80211_STYPE_DISASSOC      0x00A0
#define IEEE80211_STYPE_AUTH          0x00B0
#define IEEE80211_STYPE_DEAUTH        0x00C0
#define IEEE80211_STYPE_ACTION        0x00D0
#define IEEE80211_STYPE_PSPOLL        0x00A0
#define IEEE80211_STYPE_RTS           0x00B0
#define IEEE80211_STYPE_CTS           0x00C0
#define IEEE80211_STYPE_ACK           0x00D0
#define IEEE80211_STYPE_DATA          0x0000
#define IEEE80211_STYPE_NULLFUNC      0x0040
#define IEEE80211_STYPE_QOS_DATA      0x0080
#define IEEE80211_STYPE_QOS_NULLFUNC  0x00C0
#define IEEE80211_QOS_CTL_TID_MASK    0x000f
#define IEEE80211_QOS_CTL_LEN         2
#define IEEE80211_MAX_SSID_LEN        32
#define IEEE80211_MAX_DATA_LEN        2304
#define IEEE80211_MAX_FRAME_LEN       2352
#define IEEE80211_MAX_MPDU_LEN_HT_7935 7935
#define IEEE80211_MAX_MPDU_LEN_VHT_11454 11454
#define IEEE80211_NUM_ACS             4
#define IEEE80211_NUM_TIDS            16
#define IEEE80211_HT_MCS_MASK_LEN     10
#define IEEE80211_HT_MPDU_DENSITY_16  7
#define IEEE80211_HT_MAX_AMPDU_64K    3
#define IEEE80211_HT_MCS_TX_DEFINED   0x01
#define IEEE80211_HT_CAP_LDPC_CODING  0x0001
#define IEEE80211_HT_CAP_SUP_WIDTH_20_40 0x0002
#define IEEE80211_HT_CAP_SM_PS        0x000C
#define IEEE80211_HT_CAP_SGI_20       0x0020
#define IEEE80211_HT_CAP_SGI_40       0x0040
#define IEEE80211_HT_CAP_TX_STBC      0x0080
#define IEEE80211_HT_CAP_RX_STBC      0x0300
#define IEEE80211_HT_CAP_RX_STBC_SHIFT 8
#define IEEE80211_HT_CAP_MAX_AMSDU    0x0800
#define IEEE80211_HT_CAP_DSSSCCK40    0x1000
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 0x00000002
#define IEEE80211_VHT_CAP_RXLDPC      0x00000010
#define IEEE80211_VHT_CAP_SHORT_GI_80 0x00000020
#define IEEE80211_VHT_CAP_TXSTBC      0x00000080
#define IEEE80211_VHT_CAP_RXSTBC_1    0x00000100
#define IEEE80211_VHT_CAP_RXSTBC_MASK 0x00000700
#define IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE 0x00000800
#define IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE 0x00001000
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT 16
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK (7 << 16)
#define IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE 0x00080000
#define IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT 13
#define IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE 0x00100000
#define IEEE80211_VHT_CAP_HTC_VHT     0x00400000
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT 23
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK (7 << 23)
#define IEEE80211_VHT_MCS_SUPPORT_0_7 0
#define IEEE80211_VHT_MCS_SUPPORT_0_8 1
#define IEEE80211_VHT_MCS_SUPPORT_0_9 2
#define IEEE80211_VHT_MCS_NOT_SUPPORTED 3

#define WLAN_EID_SSID              0
#define WLAN_EID_SUPP_RATES        1
#define WLAN_EID_DS_PARAMS         3
#define WLAN_EID_TIM               5
#define WLAN_EID_COUNTRY           7
#define WLAN_EID_HT_CAPABILITY     45
#define WLAN_EID_RSN               48
#define WLAN_EID_EXT_SUPP_RATES    50
#define WLAN_EID_HT_OPERATION      61
#define WLAN_EID_EXT_CAPABILITY    127
#define WLAN_EID_VHT_CAPABILITY    191
#define WLAN_EID_VENDOR_SPECIFIC   221
#define WLAN_CAPABILITY_ESS            0x0001
#define WLAN_CAPABILITY_IBSS           0x0002
#define WLAN_CAPABILITY_PRIVACY        0x0010
#define WLAN_CAPABILITY_SHORT_PREAMBLE 0x0020
#define WLAN_CAPABILITY_SHORT_SLOT_TIME 0x0400
#define WLAN_AUTH_OPEN 0
#define WLAN_STATUS_SUCCESS 0
#define WLAN_REASON_DEAUTH_LEAVING 3
#define WLAN_CIPHER_SUITE_WEP40    0x000FAC01
#define WLAN_CIPHER_SUITE_TKIP     0x000FAC02
#define WLAN_CIPHER_SUITE_CCMP     0x000FAC04
#define WLAN_CIPHER_SUITE_WEP104   0x000FAC05
#define WLAN_CIPHER_SUITE_AES_CMAC 0x000FAC06
#define WLAN_CIPHER_SUITE_GCMP     0x000FAC08
#define WLAN_CIPHER_SUITE_GCMP_256 0x000FAC09
#define WLAN_CIPHER_SUITE_CCMP_256 0x000FAC0A
#define WLAN_CIPHER_SUITE_BIP_GMAC_128 0x000FAC0B
#define WLAN_CIPHER_SUITE_BIP_GMAC_256 0x000FAC0C
#define WLAN_CIPHER_SUITE_BIP_CMAC_256 0x000FAC0D
#define WLAN_AKM_SUITE_PSK         0x000FAC02
#define WLAN_AKM_SUITE_SAE         0x000FAC08

struct ieee80211_hdr {
    __le16 frame_control;
    __le16 duration_id;
    u8 addr1[ETH_ALEN];
    u8 addr2[ETH_ALEN];
    u8 addr3[ETH_ALEN];
    __le16 seq_ctrl;
    u8 addr4[ETH_ALEN];
} __packed __aligned(2);

struct ieee80211_hdr_3addr {
    __le16 frame_control;
    __le16 duration_id;
    u8 addr1[ETH_ALEN];
    u8 addr2[ETH_ALEN];
    u8 addr3[ETH_ALEN];
    __le16 seq_ctrl;
} __packed __aligned(2);

struct ieee80211_qos_hdr {
    __le16 frame_control;
    __le16 duration_id;
    u8 addr1[ETH_ALEN];
    u8 addr2[ETH_ALEN];
    u8 addr3[ETH_ALEN];
    __le16 seq_ctrl;
    __le16 qos_ctrl;
} __packed __aligned(2);

struct ieee80211_mgmt {
    __le16 frame_control;
    __le16 duration;
    u8 da[ETH_ALEN];
    u8 sa[ETH_ALEN];
    u8 bssid[ETH_ALEN];
    __le16 seq_ctrl;
    union {
        struct { __le16 auth_alg; __le16 auth_transaction; __le16 status_code; u8 variable[]; } __packed auth;
        struct { __le16 reason_code; } __packed deauth;
        struct { __le16 capab_info; __le16 listen_interval; u8 variable[]; } __packed assoc_req;
        struct { __le16 capab_info; __le16 status_code; __le16 aid; u8 variable[]; } __packed assoc_resp, reassoc_resp;
        struct { __le16 reason_code; } __packed disassoc;
        struct { __le64 timestamp; __le16 beacon_int; __le16 capab_info; u8 variable[]; } __packed beacon;
        struct { u8 variable[0]; } __packed probe_req;
        struct { __le64 timestamp; __le16 beacon_int; __le16 capab_info; u8 variable[]; } __packed probe_resp;
        struct { u8 category; u8 variable[]; } __packed action;
    } u;
} __packed __aligned(2);

static inline bool ieee80211_has_tods(__le16 fc) { return fc & IEEE80211_FCTL_TODS; }
static inline bool ieee80211_has_fromds(__le16 fc) { return fc & IEEE80211_FCTL_FROMDS; }
static inline bool ieee80211_has_protected(__le16 fc) { return fc & IEEE80211_FCTL_PROTECTED; }
static inline bool ieee80211_has_morefrags(__le16 fc) { return fc & IEEE80211_FCTL_MOREFRAGS; }
static inline bool ieee80211_is_mgmt(__le16 fc) { return (fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT; }
static inline bool ieee80211_is_ctl(__le16 fc) { return (fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL; }
static inline bool ieee80211_is_data(__le16 fc) { return (fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA; }
static inline bool ieee80211_is_data_qos(__le16 fc) { return (fc & (IEEE80211_FCTL_FTYPE | 0x0080)) == (IEEE80211_FTYPE_DATA | 0x0080); }
static inline bool ieee80211_is_data_present(__le16 fc) { return ieee80211_is_data(fc) && !(fc & 0x0040); }
#define IEEE80211_FC_IS(fn, ft, st) \
    static inline bool fn(__le16 fc) { return (fc & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) == ((ft) | (st)); }
IEEE80211_FC_IS(ieee80211_is_beacon, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_BEACON)
IEEE80211_FC_IS(ieee80211_is_probe_resp, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_PROBE_RESP)
IEEE80211_FC_IS(ieee80211_is_probe_req, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_PROBE_REQ)
IEEE80211_FC_IS(ieee80211_is_auth, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_AUTH)
IEEE80211_FC_IS(ieee80211_is_deauth, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_DEAUTH)
IEEE80211_FC_IS(ieee80211_is_disassoc, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_DISASSOC)
IEEE80211_FC_IS(ieee80211_is_assoc_resp, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_ASSOC_RESP)
IEEE80211_FC_IS(ieee80211_is_reassoc_resp, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_REASSOC_RESP)
IEEE80211_FC_IS(ieee80211_is_action, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_ACTION)
IEEE80211_FC_IS(ieee80211_is_nullfunc, IEEE80211_FTYPE_DATA, IEEE80211_STYPE_NULLFUNC)
IEEE80211_FC_IS(ieee80211_is_qos_nullfunc, IEEE80211_FTYPE_DATA, IEEE80211_STYPE_QOS_NULLFUNC)
IEEE80211_FC_IS(ieee80211_is_pspoll, IEEE80211_FTYPE_CTL, IEEE80211_STYPE_PSPOLL)
static inline bool ieee80211_is_any_nullfunc(__le16 fc) { return ieee80211_is_nullfunc(fc) || ieee80211_is_qos_nullfunc(fc); }
static inline u8 *ieee80211_get_qos_ctl(struct ieee80211_hdr *h) {
    return (u8 *)h + (ieee80211_has_tods(h->frame_control) && ieee80211_has_fromds(h->frame_control) ? 30 : 24);
}
static inline unsigned int ieee80211_hdrlen(__le16 fc) {
    unsigned int len = 24;
    if (ieee80211_is_data(fc)) {
        if (ieee80211_has_tods(fc) && ieee80211_has_fromds(fc)) len = 30;
        if (ieee80211_is_data_qos(fc)) len += IEEE80211_QOS_CTL_LEN;
        if (fc & IEEE80211_FCTL_ORDER) len += 4;
        return len;
    }
    if (ieee80211_is_ctl(fc)) {
        u16 st = fc & IEEE80211_FCTL_STYPE;
        if (st == 0x00C0 || st == 0x00D0) return 10;
        return 16;
    }
    return len;
}
static inline unsigned int ieee80211_get_hdrlen_from_skb(const struct sk_buff *skb) {
    if (skb->len < 10) return 0;
    return ieee80211_hdrlen(((const struct ieee80211_hdr *)skb->data)->frame_control);
}

struct ieee80211_mcs_info { u8 rx_mask[IEEE80211_HT_MCS_MASK_LEN]; __le16 rx_highest; u8 tx_params; u8 reserved[3]; } __packed;
struct ieee80211_ht_cap { __le16 cap_info; u8 ampdu_params_info; struct ieee80211_mcs_info mcs; __le16 extended_ht_cap_info; __le32 tx_BF_cap_info; u8 antenna_selection_info; } __packed;
struct ieee80211_vht_mcs_info { __le16 rx_mcs_map; __le16 rx_highest; __le16 tx_mcs_map; __le16 tx_highest; } __packed;
struct ieee80211_sta_ht_cap { u16 cap; bool ht_supported; u8 ampdu_factor; u8 ampdu_density; struct ieee80211_mcs_info mcs; };
struct ieee80211_sta_vht_cap { bool vht_supported; u32 cap; struct ieee80211_vht_mcs_info vht_mcs; };

enum nl80211_band { NL80211_BAND_2GHZ, NL80211_BAND_5GHZ, NL80211_BAND_60GHZ, NL80211_BAND_6GHZ, NUM_NL80211_BANDS };
enum nl80211_iftype { NL80211_IFTYPE_UNSPECIFIED, NL80211_IFTYPE_ADHOC, NL80211_IFTYPE_STATION, NL80211_IFTYPE_AP,
                      NL80211_IFTYPE_AP_VLAN, NL80211_IFTYPE_WDS, NL80211_IFTYPE_MONITOR, NL80211_IFTYPE_MESH_POINT,
                      NL80211_IFTYPE_P2P_CLIENT, NL80211_IFTYPE_P2P_GO, NL80211_IFTYPE_P2P_DEVICE };
enum nl80211_chan_width { NL80211_CHAN_WIDTH_20_NOHT, NL80211_CHAN_WIDTH_20, NL80211_CHAN_WIDTH_40,
                          NL80211_CHAN_WIDTH_80, NL80211_CHAN_WIDTH_80P80, NL80211_CHAN_WIDTH_160,
                          NL80211_CHAN_WIDTH_5, NL80211_CHAN_WIDTH_10 };
enum nl80211_dfs_regions { NL80211_DFS_UNSET, NL80211_DFS_FCC, NL80211_DFS_ETSI, NL80211_DFS_JP };
enum nl80211_reg_initiator { NL80211_REGDOM_SET_BY_CORE, NL80211_REGDOM_SET_BY_USER, NL80211_REGDOM_SET_BY_DRIVER,
                             NL80211_REGDOM_SET_BY_COUNTRY_IE };
enum nl80211_cqm_rssi_threshold_event { NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW, NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH };
enum nl80211_dfs_state { NL80211_DFS_USABLE, NL80211_DFS_UNAVAILABLE, NL80211_DFS_AVAILABLE };
#define NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR BIT(29)
#define NL80211_SCAN_FLAG_RANDOM_ADDR BIT(3)
#define NL80211_SCAN_FLAG_RANDOM_SN   BIT(12)
enum nl80211_ext_feature_index { NL80211_EXT_FEATURE_CAN_REPLACE_PTK0, NL80211_EXT_FEATURE_SET_SCAN_DWELL,
                                 NL80211_EXT_FEATURE_SCAN_RANDOM_SN, NL80211_EXT_FEATURE_MAX };
#define NL80211_STA_INFO_TX_BITRATE 8

#define IEEE80211_CHAN_DISABLED     BIT(0)
#define IEEE80211_CHAN_NO_IR        BIT(1)
#define IEEE80211_CHAN_RADAR        BIT(3)
#define IEEE80211_CHAN_NO_HT40PLUS  BIT(4)
#define IEEE80211_CHAN_NO_HT40MINUS BIT(5)
#define IEEE80211_CHAN_NO_80MHZ     BIT(7)
#define IEEE80211_CHAN_NO_160MHZ    BIT(8)

struct ieee80211_channel {
    enum nl80211_band band;
    u32 center_freq;
    u16 freq_offset;
    u16 hw_value;
    u32 flags;
    int max_antenna_gain;
    int max_power;
    int max_reg_power;
    bool beacon_found;
    enum nl80211_dfs_state dfs_state;
};
#define CHAN2G(_channel, _freq) { .band = NL80211_BAND_2GHZ, .center_freq = (_freq), .hw_value = (_channel), .max_power = 20 }
#define CHAN5G(_channel, _freq) { .band = NL80211_BAND_5GHZ, .center_freq = (_freq), .hw_value = (_channel), .max_power = 20 }

#define IEEE80211_RATE_SHORT_PREAMBLE BIT(0)
struct ieee80211_rate { u32 flags; u16 bitrate; u16 hw_value; u16 hw_value_short; };

struct ieee80211_supported_band {
    struct ieee80211_channel *channels;
    struct ieee80211_rate *bitrates;
    enum nl80211_band band;
    int n_channels;
    int n_bitrates;
    struct ieee80211_sta_ht_cap ht_cap;
    struct ieee80211_sta_vht_cap vht_cap;
};

struct cfg80211_chan_def {
    struct ieee80211_channel *chan;
    enum nl80211_chan_width width;
    u32 center_freq1;
    u32 center_freq2;
};

struct cfg80211_ssid { u8 ssid[IEEE80211_MAX_SSID_LEN]; u8 ssid_len; };
static inline bool cfg80211_ssid_eq(struct cfg80211_ssid *a, struct cfg80211_ssid *b) {
    return a->ssid_len == b->ssid_len && !memcmp(a->ssid, b->ssid, a->ssid_len);
}
struct cfg80211_match_set { struct cfg80211_ssid ssid; int rssi_thold; };
struct cfg80211_sched_scan_plan { u32 interval; u32 iterations; };
struct cfg80211_scan_request {
    struct cfg80211_ssid *ssids;
    int n_ssids;
    u32 n_channels;
    const u8 *ie;
    size_t ie_len;
    u16 duration;
    bool duration_mandatory;
    u32 flags;
    u8 mac_addr[ETH_ALEN] __aligned(2);
    u8 mac_addr_mask[ETH_ALEN] __aligned(2);
    bool no_cck;
    struct ieee80211_channel *channels[64];
};
struct cfg80211_sched_scan_request {
    struct cfg80211_ssid *ssids;
    int n_ssids;
    u32 n_channels;
    struct cfg80211_match_set *match_sets;
    int n_match_sets;
    struct ieee80211_channel *channels[64];
};
struct cfg80211_scan_info { u64 scan_start_tsf; u8 tsf_bssid[ETH_ALEN]; bool aborted; };
struct cfg80211_bitrate_mask {
    struct { u32 legacy; u8 ht_mcs[IEEE80211_HT_MCS_MASK_LEN]; u16 vht_mcs[8]; } control[NUM_NL80211_BANDS];
};
struct cfg80211_wowlan { bool any, disconnect, magic_pkt; int n_patterns; void *patterns; void *nd_config; };
struct wiphy_wowlan_support { u32 flags; int n_patterns; int pattern_max_len; int pattern_min_len; int max_nd_match_sets; };
struct cfg80211_sar_specs { int type; u32 num_sub_specs; struct { s32 power; u32 freq_range_index; } sub_specs[8]; };
struct cfg80211_sar_freq_ranges { u32 start_freq; u32 end_freq; };
struct cfg80211_sar_capa { int type; u32 num_freq_ranges; const struct cfg80211_sar_freq_ranges *freq_ranges; };
#define NL80211_SAR_TYPE_POWER 0
#define RATE_INFO_FLAGS_MCS       BIT(0)
#define RATE_INFO_FLAGS_VHT_MCS   BIT(1)
#define RATE_INFO_FLAGS_SHORT_GI  BIT(2)
struct rate_info { u16 flags; u16 legacy; u8 mcs; u8 nss; u8 bw; };
struct station_info { u64 filled; struct rate_info txrate; };
u32 cfg80211_calculate_bitrate(struct rate_info *r);

struct ieee80211_iface_limit { u16 max; u16 types; };
struct ieee80211_iface_combination {
    const struct ieee80211_iface_limit *limits;
    u32 num_different_channels;
    u16 max_interfaces;
    u8 n_limits;
    bool beacon_int_infra_match;
};

struct regulatory_request { int wiphy_idx; enum nl80211_reg_initiator initiator; enum nl80211_dfs_regions dfs_region; char alpha2[3]; };
struct ieee80211_regdomain { u32 n_reg_rules; char alpha2[3]; enum nl80211_dfs_regions dfs_region; };
#define REGULATORY_CUSTOM_REG        BIT(0)
#define REGULATORY_STRICT_REG        BIT(1)
#define REGULATORY_DISABLE_BEACON_HINTS BIT(2)
#define REGULATORY_COUNTRY_IE_IGNORE BIT(3)
#define REGULATORY_WIPHY_SELF_MANAGED BIT(7)
#define REGULATORY_IGNORE_STALE_KICKOFF BIT(9)

#define WIPHY_FLAG_SUPPORTS_TDLS    BIT(0)
#define WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL BIT(1)
#define WIPHY_FLAG_IBSS_RSN         BIT(2)
#define WIPHY_FLAG_TDLS_EXTERNAL_SETUP BIT(3)
#define WIPHY_FLAG_AP_UAPSD         BIT(4)

struct wiphy {
    u8 perm_addr[ETH_ALEN];
    u16 interface_modes;
    u32 flags;
    u32 regulatory_flags;
    u32 features;
    u8 ext_features[(NL80211_EXT_FEATURE_MAX + 7) / 8];
    struct ieee80211_supported_band *bands[NUM_NL80211_BANDS];
    void (*reg_notifier)(struct wiphy *wiphy, struct regulatory_request *request);
    const struct ieee80211_iface_combination *iface_combinations;
    int n_iface_combinations;
    u8 max_scan_ssids;
    u8 max_sched_scan_ssids;
    u16 max_scan_ie_len;
    u32 available_antennas_tx;
    u32 available_antennas_rx;
    u32 rts_threshold;
    char fw_version[64];
    const struct wiphy_wowlan_support *wowlan;
    const struct cfg80211_sar_capa *sar_capa;
    void *priv;
    struct ieee80211_hw *hw_back;
};
static inline void wiphy_ext_feature_set(struct wiphy *w, enum nl80211_ext_feature_index f) { w->ext_features[f / 8] |= (u8)(1 << (f % 8)); }
static inline struct ieee80211_hw *wiphy_to_ieee80211_hw(struct wiphy *w) { return w->hw_back; }
#define wiphy_name(w) "phy0"
#define wiphy_dev(w) NULL
int kpi_regulatory_hint(struct wiphy *w, const char *alpha2);
#define regulatory_hint(w, a) kpi_regulatory_hint(w, a)
#define regulatory_set_wiphy_regd(w, r) 0
#define wiphy_read_of_freq_limits(w) do { } while (0)
#define wiphy_apply_custom_regulatory(w, r) do { } while (0)

enum ieee80211_ac_numbers { IEEE80211_AC_VO = 0, IEEE80211_AC_VI = 1, IEEE80211_AC_BE = 2, IEEE80211_AC_BK = 3 };
enum ieee80211_smps_mode { IEEE80211_SMPS_AUTOMATIC, IEEE80211_SMPS_OFF, IEEE80211_SMPS_STATIC, IEEE80211_SMPS_DYNAMIC };
enum ieee80211_sta_rx_bandwidth { IEEE80211_STA_RX_BW_20 = 0, IEEE80211_STA_RX_BW_40, IEEE80211_STA_RX_BW_80, IEEE80211_STA_RX_BW_160 };
enum set_key_cmd { SET_KEY, DISABLE_KEY };
enum ieee80211_reconfig_type { IEEE80211_RECONFIG_TYPE_RESTART, IEEE80211_RECONFIG_TYPE_SUSPEND };
enum ieee80211_ampdu_mlme_action { IEEE80211_AMPDU_RX_START, IEEE80211_AMPDU_RX_STOP, IEEE80211_AMPDU_TX_START,
                                   IEEE80211_AMPDU_TX_STOP_CONT, IEEE80211_AMPDU_TX_STOP_FLUSH,
                                   IEEE80211_AMPDU_TX_STOP_FLUSH_CONT, IEEE80211_AMPDU_TX_OPERATIONAL };
#define IEEE80211_AMPDU_TX_START_IMMEDIATE 1
enum ieee80211_rssi_event_data { RSSI_EVENT_HIGH, RSSI_EVENT_LOW };
#define IEEE80211_RC_BW_CHANGED BIT(1)

struct ieee80211_tx_queue_params { u16 txop; u16 cw_min; u16 cw_max; u8 aifs; bool acm; bool uapsd; };

struct ieee80211_vif;
struct ieee80211_sta;

struct ieee80211_txq {
    struct ieee80211_vif *vif;
    struct ieee80211_sta *sta;
    u8 tid;
    u8 ac;
    struct sk_buff_head frags;
    unsigned long flags;
    struct list_head kpi_list;
    u8 drv_priv[] __aligned(sizeof(void *));
};

struct ieee80211_link_sta {
    struct ieee80211_sta *sta;
    u8 addr[ETH_ALEN];
    u32 supp_rates[NUM_NL80211_BANDS];
    struct ieee80211_sta_ht_cap ht_cap;
    struct ieee80211_sta_vht_cap vht_cap;
    u8 rx_nss;
    enum ieee80211_sta_rx_bandwidth bandwidth;
    struct { u16 max_amsdu_len; u16 max_rc_amsdu_len; u16 max_tid_amsdu_len[IEEE80211_NUM_TIDS]; } agg;
};

struct ieee80211_sta {
    u8 addr[ETH_ALEN];
    u16 aid;
    bool wme;
    bool tdls;
    enum ieee80211_smps_mode smps_mode;
    struct ieee80211_link_sta deflink;
    struct ieee80211_txq *txq[IEEE80211_NUM_TIDS + 1];
    void *kpi_owner;
    u8 drv_priv[] __aligned(sizeof(void *));
};

struct ieee80211_bss_conf {
    u8 bssid_storage[ETH_ALEN];
    const u8 *bssid;
    bool use_short_preamble;
    bool use_short_slot;
    bool enable_beacon;
    u8 dtim_period;
    u16 beacon_int;
    u32 basic_rates;
    int mcast_rate[NUM_NL80211_BANDS];
    s32 cqm_rssi_thold;
    u32 cqm_rssi_hyst;
    struct cfg80211_chan_def chanreq_oper;
    u8 txpower;
    u16 ht_operation_mode;
    struct { u8 membership[8]; u8 position[16]; } mu_group;
    bool qos;
    bool hidden_ssid;
};

struct ieee80211_vif_cfg { bool assoc; bool ibss_joined; bool ps; u16 aid; u8 ssid[IEEE80211_MAX_SSID_LEN]; size_t ssid_len; };

#define IEEE80211_VIF_BEACON_FILTER     BIT(0)
#define IEEE80211_VIF_SUPPORTS_CQM_RSSI BIT(1)

struct ieee80211_vif {
    enum nl80211_iftype type;
    struct ieee80211_vif_cfg cfg;
    struct ieee80211_bss_conf bss_conf;
    u8 addr[ETH_ALEN] __aligned(2);
    bool p2p;
    u32 driver_flags;
    struct ieee80211_txq *txq;
    void *kpi_owner;
    u8 drv_priv[] __aligned(sizeof(void *));
};
static inline enum nl80211_iftype ieee80211_vif_type_p2p(struct ieee80211_vif *vif) { return vif->type; }

#define IEEE80211_KEY_FLAG_GENERATE_IV   BIT(1)
#define IEEE80211_KEY_FLAG_GENERATE_MMIC BIT(2)
#define IEEE80211_KEY_FLAG_PAIRWISE      BIT(3)
#define IEEE80211_KEY_FLAG_SW_MGMT_TX    BIT(4)
#define IEEE80211_KEY_FLAG_PUT_IV_SPACE  BIT(5)
#define IEEE80211_KEY_FLAG_RX_MGMT       BIT(6)
struct ieee80211_key_conf {
    u64 tx_pn;
    u32 cipher;
    u8 icv_len;
    u8 iv_len;
    u8 hw_key_idx;
    s8 keyidx;
    u16 flags;
    s8 link_id;
    u8 keylen;
    u8 key[32];
};

#define IEEE80211_TX_CTL_REQ_TX_STATUS    BIT(0)
#define IEEE80211_TX_CTL_ASSIGN_SEQ       BIT(1)
#define IEEE80211_TX_CTL_NO_ACK           BIT(2)
#define IEEE80211_TX_CTL_CLEAR_PS_FILT    BIT(3)
#define IEEE80211_TX_CTL_FIRST_FRAGMENT   BIT(4)
#define IEEE80211_TX_CTL_SEND_AFTER_DTIM  BIT(5)
#define IEEE80211_TX_CTL_AMPDU            BIT(6)
#define IEEE80211_TX_CTL_INJECTED         BIT(7)
#define IEEE80211_TX_STAT_TX_FILTERED     BIT(8)
#define IEEE80211_TX_STAT_ACK             BIT(9)
#define IEEE80211_TX_STAT_AMPDU           BIT(10)
#define IEEE80211_TX_STAT_NOACK_TRANSMITTED BIT(11)
#define IEEE80211_TX_CTL_RATE_CTRL_PROBE  BIT(12)
#define IEEE80211_TX_INTFL_DONT_ENCRYPT   BIT(13)
#define IEEE80211_TX_CTL_USE_MINRATE      BIT(14)
#define IEEE80211_TX_CTL_DONTFRAG         BIT(15)
#define IEEE80211_TX_STATUS_EOSP          BIT(16)
#define IEEE80211_TX_CTL_NO_CCK_RATE      BIT(17)
#define IEEE80211_TX_CTL_HW_80211_ENCAP   BIT(18)
#define IEEE80211_TX_MAX_RATES 4
#define IEEE80211_TX_RC_USE_RTS_CTS       BIT(0)
#define IEEE80211_TX_RC_MCS               BIT(3)

struct ieee80211_tx_rate { s8 idx; u16 count:5, flags:11; } __packed;
struct ieee80211_tx_info {
    u32 flags;
    u32 band:3, status_data_idr:1, status_data:13, hw_queue:4, tx_time_est:10;
    union {
        struct {
            union {
                struct { struct ieee80211_tx_rate rates[IEEE80211_TX_MAX_RATES]; s8 rts_cts_rate_idx; u8 use_rts:1, use_cts_prot:1, short_preamble:1, skip_table:1, antennas:2; };
                unsigned long enqueue_jiffies;
            };
            struct ieee80211_vif *vif;
            struct ieee80211_key_conf *hw_key;
            u32 flags;
        } control;
        struct {
            struct ieee80211_tx_rate rates[IEEE80211_TX_MAX_RATES];
            s32 ack_signal;
            u8 ampdu_ack_len;
            u8 ampdu_len;
            u8 antenna;
            u8 pad;
            u16 tx_time;
            u8 flags;
            u8 pad2;
            void *status_driver_data[16 / sizeof(void *)];
        } status;
        struct { struct ieee80211_tx_rate driver_rates[IEEE80211_TX_MAX_RATES]; u8 pad[4]; void *rate_driver_data[24 / sizeof(void *)]; };
        void *driver_data[40 / sizeof(void *)];
    };
};
_Static_assert(sizeof(struct ieee80211_tx_info) <= 48, "tx_info fits skb->cb");
static inline struct ieee80211_tx_info *IEEE80211_SKB_CB(struct sk_buff *skb) { return (struct ieee80211_tx_info *)skb->cb; }
static inline void ieee80211_tx_info_clear_status(struct ieee80211_tx_info *info) {
    memset(&info->status, 0, sizeof(info->status));
    info->flags &= ~(IEEE80211_TX_STAT_ACK | IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_STAT_TX_FILTERED);
}
struct ieee80211_tx_control { struct ieee80211_sta *sta; };

enum mac80211_rx_encoding { RX_ENC_LEGACY = 0, RX_ENC_HT, RX_ENC_VHT, RX_ENC_HE };
#define RX_FLAG_MMIC_ERROR    BIT(0)
#define RX_FLAG_DECRYPTED     BIT(1)
#define RX_FLAG_MACTIME_START BIT(2)
#define RX_FLAG_MMIC_STRIPPED BIT(3)
#define RX_FLAG_IV_STRIPPED   BIT(4)
#define RX_FLAG_FAILED_FCS_CRC BIT(5)
#define RX_FLAG_FAILED_PLCP_CRC BIT(6)
#define RX_FLAG_MACTIME_END   BIT(7)
#define RX_FLAG_NO_SIGNAL_VAL BIT(8)
#define RX_FLAG_PN_VALIDATED  BIT(9)
#define RX_FLAG_ICV_STRIPPED  BIT(10)
#define RX_FLAG_MIC_STRIPPED  BIT(11)
#define RX_FLAG_NO_PSDU       BIT(12)
#define FCS_LEN 4
#define RX_ENC_FLAG_SHORTPRE  BIT(0)
#define RX_ENC_FLAG_SHORT_GI  BIT(2)
#define RATE_INFO_BW_20 0
#define RATE_INFO_BW_40 1
#define RATE_INFO_BW_80 2
struct ieee80211_rx_status {
    u64 mactime;
    u64 boottime_ns;
    u32 device_timestamp;
    u32 ampdu_reference;
    u32 flag;
    u16 freq;
    u8 enc_flags;
    u8 encoding:3, bw:4;
    u8 he_ru:3, he_gi:2, he_dcm:1;
    u8 rate_idx;
    u8 nss;
    u8 rx_flags;
    u8 band;
    u8 antenna;
    s8 signal;
    u8 chains;
    s8 chain_signal[4];
    u8 zero_length_psdu_type;
    u8 link_valid:1, link_id:4;
};
static inline struct ieee80211_rx_status *IEEE80211_SKB_RXCB(struct sk_buff *skb) { return (struct ieee80211_rx_status *)skb->cb; }

#define IEEE80211_CONF_IDLE           BIT(2)
#define IEEE80211_CONF_PS             BIT(1)
#define IEEE80211_CONF_MONITOR        BIT(0)
#define IEEE80211_CONF_CHANGE_SMPS    BIT(1)
#define IEEE80211_CONF_CHANGE_LISTEN_INTERVAL BIT(2)
#define IEEE80211_CONF_CHANGE_MONITOR BIT(3)
#define IEEE80211_CONF_CHANGE_PS      BIT(4)
#define IEEE80211_CONF_CHANGE_POWER   BIT(5)
#define IEEE80211_CONF_CHANGE_CHANNEL BIT(6)
#define IEEE80211_CONF_CHANGE_RETRY_LIMITS BIT(7)
#define IEEE80211_CONF_CHANGE_IDLE    BIT(8)
struct ieee80211_conf {
    u32 flags;
    int power_level;
    int dynamic_ps_timeout;
    u16 listen_interval;
    u8 ps_dtim_period;
    u8 long_frame_max_tx_count;
    u8 short_frame_max_tx_count;
    struct cfg80211_chan_def chandef;
    bool radar_enabled;
    enum ieee80211_smps_mode smps_mode;
};

enum ieee80211_hw_flags {
    IEEE80211_HW_HAS_RATE_CONTROL, IEEE80211_HW_RX_INCLUDES_FCS, IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING,
    IEEE80211_HW_SIGNAL_UNSPEC, IEEE80211_HW_SIGNAL_DBM, IEEE80211_HW_NEED_DTIM_BEFORE_ASSOC,
    IEEE80211_HW_SPECTRUM_MGMT, IEEE80211_HW_AMPDU_AGGREGATION, IEEE80211_HW_SUPPORTS_PS,
    IEEE80211_HW_PS_NULLFUNC_STACK, IEEE80211_HW_SUPPORTS_DYNAMIC_PS, IEEE80211_HW_MFP_CAPABLE,
    IEEE80211_HW_WANT_MONITOR_VIF, IEEE80211_HW_NO_VIRTUAL_MONITOR, IEEE80211_HW_NO_AUTO_VIF,
    IEEE80211_HW_SW_CRYPTO_CONTROL, IEEE80211_HW_SUPPORT_FAST_XMIT, IEEE80211_HW_REPORTS_TX_ACK_STATUS,
    IEEE80211_HW_CONNECTION_MONITOR, IEEE80211_HW_QUEUE_CONTROL, IEEE80211_HW_SUPPORTS_PER_STA_GTK,
    IEEE80211_HW_AP_LINK_PS, IEEE80211_HW_TX_AMPDU_SETUP_IN_HW, IEEE80211_HW_SUPPORTS_RC_TABLE,
    IEEE80211_HW_P2P_DEV_ADDR_FOR_INTF, IEEE80211_HW_TIMING_BEACON_ONLY, IEEE80211_HW_SUPPORTS_HT_CCK_RATES,
    IEEE80211_HW_CHANCTX_STA_CSA, IEEE80211_HW_SUPPORTS_CLONED_SKBS, IEEE80211_HW_SINGLE_SCAN_ON_ALL_BANDS,
    IEEE80211_HW_TDLS_WIDER_BW, IEEE80211_HW_SUPPORTS_AMSDU_IN_AMPDU, IEEE80211_HW_BEACON_TX_STATUS,
    IEEE80211_HW_NEEDS_UNIQUE_STA_ADDR, IEEE80211_HW_SUPPORTS_REORDERING_BUFFER, IEEE80211_HW_USES_RSS,
    IEEE80211_HW_TX_AMSDU, IEEE80211_HW_TX_FRAG_LIST, IEEE80211_HW_REPORTS_LOW_ACK,
    IEEE80211_HW_SUPPORTS_TX_FRAG, IEEE80211_HW_SUPPORTS_TDLS_BUFFER_STA, IEEE80211_HW_DEAUTH_NEED_MGD_TX_PREP,
    IEEE80211_HW_DOESNT_SUPPORT_QOS_NDP, IEEE80211_HW_BUFF_MMPDU_TXQ, IEEE80211_HW_SUPPORTS_VHT_EXT_NSS_BW,
    IEEE80211_HW_STA_MMPDU_TXQ, IEEE80211_HW_TX_STATUS_NO_AMPDU_LEN, IEEE80211_HW_SUPPORTS_MULTI_BSSID,
    IEEE80211_HW_SUPPORTS_ONLY_HE_MULTI_BSSID, IEEE80211_HW_AMPDU_KEYBORDER_SUPPORT,
    IEEE80211_HW_SUPPORTS_TX_ENCAP_OFFLOAD, IEEE80211_HW_SUPPORTS_RX_DECAP_OFFLOAD,
    IEEE80211_HW_SUPPORTS_CONC_MON_RX_DECAP, IEEE80211_HW_DETECTS_COLOR_COLLISION,
    IEEE80211_HW_MLO_MCAST_MULTI_LINK_TX, IEEE80211_HW_DISALLOW_PUNCTURING,
    IEEE80211_HW_HANDLES_QUIET_CSA, NUM_IEEE80211_HW_FLAGS
};

struct ieee80211_ops;
struct ieee80211_hw {
    struct ieee80211_conf conf;
    struct wiphy *wiphy;
    const char *rate_control_algorithm;
    void *priv;
    unsigned long flags[BITS_TO_LONGS(NUM_IEEE80211_HW_FLAGS)];
    unsigned int extra_tx_headroom;
    unsigned int extra_beacon_tailroom;
    int vif_data_size;
    int sta_data_size;
    int chanctx_data_size;
    int txq_data_size;
    u16 queues;
    u16 max_listen_interval;
    s8 max_signal;
    u8 max_rates;
    u8 max_report_rates;
    u8 max_rate_tries;
    u16 max_rx_aggregation_subframes;
    u16 max_tx_aggregation_subframes;
    u8 max_tx_fragments;
    u8 offchannel_tx_hw_queue;
    u8 radiotap_mcs_details;
    u16 radiotap_vht_details;
    u8 uapsd_queues;
    u8 uapsd_max_sp_len;
    const struct ieee80211_ops *kpi_ops;
    void *kpi_local;
    const char *kpi_driver;
};
#define ieee80211_hw_set(hw, flg) set_bit(IEEE80211_HW_##flg, (hw)->flags)
#define ieee80211_hw_check(hw, flg) test_bit(IEEE80211_HW_##flg, (hw)->flags)
#define SET_IEEE80211_DEV(hw, d) ((hw)->kpi_driver = (d) ? (d)->driver_name : NULL)
#define SET_IEEE80211_PERM_ADDR(hw, a) memcpy((hw)->wiphy->perm_addr, a, ETH_ALEN)

struct ieee80211_scan_ies { const u8 *ies[NUM_NL80211_BANDS]; size_t len[NUM_NL80211_BANDS]; const u8 *common_ies; size_t common_ie_len; };
struct ieee80211_scan_request { struct ieee80211_scan_ies ies; struct cfg80211_scan_request req; };
struct ieee80211_prep_tx_info { u16 duration; u16 subtype; u8 success:1, was_assoc:1; int link_id; };
struct ieee80211_ampdu_params {
    enum ieee80211_ampdu_mlme_action action;
    struct ieee80211_sta *sta;
    u16 tid;
    u16 ssn;
    u16 buf_size;
    bool amsdu;
    u16 timeout;
};
struct ieee80211_chanctx_conf { struct cfg80211_chan_def def; };
struct ieee80211_vif_chanctx_switch { struct ieee80211_vif *vif; struct ieee80211_chanctx_conf *old_ctx, *new_ctx; };
enum ieee80211_chanctx_switch_mode { CHANCTX_SWMODE_REASSIGN_VIF, CHANCTX_SWMODE_SWAP_CONTEXTS };

#define BSS_CHANGED_ASSOC           BIT(0)
#define BSS_CHANGED_ERP_CTS_PROT    BIT(1)
#define BSS_CHANGED_ERP_PREAMBLE    BIT(2)
#define BSS_CHANGED_ERP_SLOT        BIT(3)
#define BSS_CHANGED_HT              BIT(4)
#define BSS_CHANGED_BASIC_RATES     BIT(5)
#define BSS_CHANGED_BEACON_INT      BIT(6)
#define BSS_CHANGED_BSSID           BIT(7)
#define BSS_CHANGED_BEACON          BIT(8)
#define BSS_CHANGED_BEACON_ENABLED  BIT(9)
#define BSS_CHANGED_CQM             BIT(10)
#define BSS_CHANGED_IBSS            BIT(11)
#define BSS_CHANGED_ARP_FILTER      BIT(12)
#define BSS_CHANGED_QOS             BIT(13)
#define BSS_CHANGED_IDLE            BIT(14)
#define BSS_CHANGED_SSID            BIT(15)
#define BSS_CHANGED_AP_PROBE_RESP   BIT(16)
#define BSS_CHANGED_PS              BIT(17)
#define BSS_CHANGED_TXPOWER         BIT(18)
#define BSS_CHANGED_MU_GROUPS       BIT(23)

#define FIF_ALLMULTI      BIT(1)
#define FIF_FCSFAIL       BIT(2)
#define FIF_PLCPFAIL      BIT(3)
#define FIF_BCN_PRBRESP_PROMISC BIT(4)
#define FIF_CONTROL       BIT(5)
#define FIF_OTHER_BSS     BIT(6)
#define FIF_PSPOLL        BIT(7)
#define FIF_PROBE_REQ     BIT(8)
#define IEEE80211_IFACE_ITER_NORMAL 0
#define IEEE80211_IFACE_ITER_RESUME_ALL BIT(0)

struct ieee80211_ops {
    void (*tx)(struct ieee80211_hw *hw, struct ieee80211_tx_control *control, struct sk_buff *skb);
    void (*wake_tx_queue)(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
    int  (*start)(struct ieee80211_hw *hw);
    void (*stop)(struct ieee80211_hw *hw, bool suspend);
    int  (*config)(struct ieee80211_hw *hw, int radio_idx, u32 changed);
    int  (*add_interface)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
    void (*remove_interface)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
    int  (*change_interface)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, enum nl80211_iftype type, bool p2p);
    void (*configure_filter)(struct ieee80211_hw *hw, unsigned int changed_flags, unsigned int *new_flags, u64 multicast);
    void (*bss_info_changed)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_bss_conf *conf, u64 changed);
    int  (*start_ap)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_bss_conf *link_conf);
    void (*stop_ap)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_bss_conf *link_conf);
    int  (*conf_tx)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, unsigned int link_id, u16 ac, const struct ieee80211_tx_queue_params *params);
    int  (*sta_add)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta);
    int  (*sta_remove)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta);
    int  (*set_tim)(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set);
    int  (*set_key)(struct ieee80211_hw *hw, enum set_key_cmd cmd, struct ieee80211_vif *vif, struct ieee80211_sta *sta, struct ieee80211_key_conf *key);
    int  (*ampdu_action)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_ampdu_params *params);
    bool (*can_aggregate_in_amsdu)(struct ieee80211_hw *hw, struct sk_buff *head, struct sk_buff *skb);
    void (*sw_scan_start)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, const u8 *mac_addr);
    void (*sw_scan_complete)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
    void (*mgd_prepare_tx)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_prep_tx_info *info);
    int  (*set_rts_threshold)(struct ieee80211_hw *hw, int radio_idx, u32 value);
    void (*sta_statistics)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta, struct station_info *sinfo);
    void (*flush)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u32 queues, bool drop);
    int  (*set_bitrate_mask)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, const struct cfg80211_bitrate_mask *mask);
    int  (*set_antenna)(struct ieee80211_hw *hw, int radio_idx, u32 tx_antenna, u32 rx_antenna);
    int  (*get_antenna)(struct ieee80211_hw *hw, int radio_idx, u32 *tx_antenna, u32 *rx_antenna);
    void (*reconfig_complete)(struct ieee80211_hw *hw, enum ieee80211_reconfig_type reconfig_type);
    int  (*hw_scan)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_scan_request *req);
    void (*cancel_hw_scan)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
    void (*link_sta_rc_update)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_link_sta *link_sta, u32 changed);
    int  (*set_sar_specs)(struct ieee80211_hw *hw, const struct cfg80211_sar_specs *sar);
    int  (*suspend)(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan);
    int  (*resume)(struct ieee80211_hw *hw);
    void (*set_wakeup)(struct ieee80211_hw *hw, bool enabled);
    int  (*add_chanctx)(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx);
    void (*remove_chanctx)(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx);
    void (*change_chanctx)(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx, u32 changed);
    int  (*switch_vif_chanctx)(struct ieee80211_hw *hw, struct ieee80211_vif_chanctx_switch *vifs, int n_vifs, enum ieee80211_chanctx_switch_mode mode);
};
#define ieee80211_emulate_add_chanctx        NULL
#define ieee80211_emulate_remove_chanctx     NULL
#define ieee80211_emulate_change_chanctx     NULL
#define ieee80211_emulate_switch_vif_chanctx NULL

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len, const struct ieee80211_ops *ops);
void ieee80211_free_hw(struct ieee80211_hw *hw);
int  ieee80211_register_hw(struct ieee80211_hw *hw);
void ieee80211_unregister_hw(struct ieee80211_hw *hw);
void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta, struct sk_buff *skb, void *napi);
void ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb);
#define ieee80211_rx(hw, skb) ieee80211_rx_irqsafe(hw, skb)
void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb);
#define ieee80211_tx_status(hw, skb) ieee80211_tx_status_irqsafe(hw, skb)
#define ieee80211_tx_status_ni(hw, skb) ieee80211_tx_status_irqsafe(hw, skb)
void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb);
void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work);
void ieee80211_queue_delayed_work(struct ieee80211_hw *hw, struct delayed_work *dwork, unsigned long delay);
void ieee80211_stop_queues(struct ieee80211_hw *hw);
void ieee80211_wake_queues(struct ieee80211_hw *hw);
void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue);
void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue);
struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
#define ieee80211_tx_dequeue_ni(hw, txq) ieee80211_tx_dequeue(hw, txq)
void ieee80211_txq_get_depth(struct ieee80211_txq *txq, unsigned long *frame_cnt, unsigned long *byte_cnt);
void ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw, u32 iter_flags,
        void (*iterator)(void *data, u8 *mac, struct ieee80211_vif *vif), void *data);
#define ieee80211_iterate_active_interfaces(hw, f, it, d) ieee80211_iterate_active_interfaces_atomic(hw, f, it, d)
#define ieee80211_iterate_interfaces(hw, f, it, d) ieee80211_iterate_active_interfaces_atomic(hw, f, it, d)
void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
        void (*iterator)(void *data, struct ieee80211_sta *sta), void *data);
struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif, const u8 *addr);
struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw, const u8 *addr, const u8 *localaddr);
void ieee80211_iter_keys(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
        void (*iter)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta,
                     struct ieee80211_key_conf *key, void *data), void *iter_data);
#define ieee80211_iter_keys_rcu(hw, vif, it, d) ieee80211_iter_keys(hw, vif, it, d)
void ieee80211_scan_completed(struct ieee80211_hw *hw, struct cfg80211_scan_info *info);
void ieee80211_connection_loss(struct ieee80211_vif *vif);
void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif, enum nl80211_cqm_rssi_threshold_event ev, s32 level, gfp_t gfp);
void ieee80211_restart_hw(struct ieee80211_hw *hw);
struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif, int link_id, bool qos_ok);
struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *hw, const u8 *src_addr, const u8 *ssid, size_t ssid_len, size_t tailroom);
struct sk_buff *ieee80211_proberesp_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 *tim_offset, u16 *tim_length, unsigned int link_id);
int  ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid, u16 timeout);
void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra, u16 tid);
void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra, u16 tid);
static inline int ieee80211_request_smps(struct ieee80211_vif *vif, unsigned int link_id, enum ieee80211_smps_mode m) { (void)vif; (void)link_id; (void)m; return 0; }
int ieee80211_channel_to_frequency(int chan, enum nl80211_band band);
int ieee80211_frequency_to_channel(int freq);
static inline int cfg80211_get_ies_channel_number(const u8 *ie, size_t ielen, enum nl80211_band band) {
    (void)band;
    size_t i = 0;
    while (i + 2 <= ielen) {
        if (ie[i] == WLAN_EID_DS_PARAMS && ie[i + 1] >= 1 && i + 3 <= ielen) return ie[i + 2];
        i += 2 + ie[i + 1];
    }
    return -1;
}
static inline const u8 *cfg80211_find_ie(u8 eid, const u8 *ies, int len) {
    int i = 0;
    while (i + 2 <= len) {
        if (ies[i] == eid && i + 2 + ies[i + 1] <= len) return &ies[i];
        i += 2 + ies[i + 1];
    }
    return NULL;
}
static inline u32 ieee80211_chandef_max_power(struct cfg80211_chan_def *c) { return c->chan ? (u32)c->chan->max_power : 20; }
static inline enum nl80211_band ieee80211_skb_band(void) { return NL80211_BAND_2GHZ; }

#endif

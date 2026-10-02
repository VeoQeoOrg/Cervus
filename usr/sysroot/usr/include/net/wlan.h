#ifndef _NET_WLAN_H
#define _NET_WLAN_H

#include <stdint.h>

#define WLAN_SEC_OPEN          0x00000001u
#define WLAN_SEC_WEP           0x00000002u
#define WLAN_SEC_WPA           0x00000004u
#define WLAN_SEC_WPA2          0x00000008u
#define WLAN_SEC_WPA3          0x00000010u
#define WLAN_SEC_PSK           0x00000100u
#define WLAN_SEC_EAP           0x00000200u
#define WLAN_SEC_SAE           0x00000400u
#define WLAN_SEC_OWE           0x00000800u
#define WLAN_SEC_CCMP          0x00001000u
#define WLAN_SEC_TKIP          0x00002000u
#define WLAN_SEC_PMF_REQUIRED  0x00004000u
#define WLAN_SEC_PMF_CAPABLE   0x00008000u
#define WLAN_SEC_SUPPORTED     0x80000000u

#define WLAN_STATE_DOWN        0
#define WLAN_STATE_IDLE        1
#define WLAN_STATE_SCANNING    2
#define WLAN_STATE_AUTHENTICATING 3
#define WLAN_STATE_ASSOCIATING 4
#define WLAN_STATE_HANDSHAKE   5
#define WLAN_STATE_CONNECTED   6

#define WLAN_BSS_CONNECTED     0x01
#define WLAN_BSS_HT            0x02
#define WLAN_BSS_WMM           0x04

#define WLAN_CONNECT_HIDDEN    0x01
#define WLAN_SCAN_NOWAIT       0x01

struct wlan_bss_info {
    uint8_t  bssid[6];
    uint8_t  ssid_len;
    uint8_t  channel;
    char     ssid[32];
    int8_t   signal;
    uint8_t  flags;
    uint16_t freq;
    uint32_t security;
    uint32_t age_ms;
    uint16_t capab;
    uint16_t beacon_int;
    uint8_t  reserved[4];
};

struct wlan_scan_req {
    char     ssid[32];
    uint8_t  ssid_len;
    uint8_t  flags;
    uint8_t  reserved[2];
};

struct wlan_scan_results {
    uint64_t buf;
    uint32_t max;
    uint32_t count;
};

struct wlan_connect_req {
    char     ssid[32];
    uint8_t  ssid_len;
    uint8_t  has_bssid;
    uint8_t  has_psk;
    uint8_t  flags;
    uint8_t  bssid[6];
    uint8_t  reserved[2];
    uint8_t  psk[32];
};

struct wlan_status {
    uint32_t state;
    uint8_t  bssid[6];
    uint8_t  ssid_len;
    uint8_t  channel;
    char     ssid[32];
    int8_t   signal;
    uint8_t  authorized;
    uint16_t freq;
    uint32_t security;
    uint32_t connected_ms;
    uint16_t last_reason;
    uint16_t tx_rate;
    int32_t  last_error;
    char     message[64];
    uint8_t  mac[6];
    uint8_t  reserved[2];
    char     ifname[16];
    char     driver[16];
    char     firmware[32];
};

#define WLAN_IOC_ENC(dir, nr, size) \
    (((unsigned long)(dir) << 30) | ((unsigned long)'W' << 8) | (unsigned long)(nr) | ((unsigned long)(size) << 16))

#define WLAN_IOC_SCAN       WLAN_IOC_ENC(1u, 1, sizeof(struct wlan_scan_req))
#define WLAN_IOC_RESULTS    WLAN_IOC_ENC(3u, 2, sizeof(struct wlan_scan_results))
#define WLAN_IOC_CONNECT    WLAN_IOC_ENC(1u, 3, sizeof(struct wlan_connect_req))
#define WLAN_IOC_DISCONNECT WLAN_IOC_ENC(0u, 4, 0)
#define WLAN_IOC_STATUS     WLAN_IOC_ENC(2u, 5, sizeof(struct wlan_status))

#endif

#ifndef KERNEL_DRIVERS_USB_NET_H
#define KERNEL_DRIVERS_USB_NET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define USBNET_ECM   1
#define USBNET_NCM   2
#define USBNET_RNDIS 3

#define USBNET_RX_BUFS 8
#define USBNET_BUF     4096

typedef struct {
    bool     present;
    uint8_t  proto;
    uint8_t  cfg_value;
    uint8_t  comm_intf;
    uint8_t  data_intf;
    uint8_t  data_alt;
    uint8_t  in_ep;
    uint8_t  out_ep;
    uint8_t  int_ep;
    uint16_t in_mps;
    uint16_t out_mps;
    uint16_t int_mps;
    uint8_t  int_interval;
    uint8_t  mac_str;
    uint16_t max_segment;
} usb_net_iface_t;

bool usb_parse_net(const uint8_t *cfg, uint16_t total, usb_net_iface_t *out);

typedef struct {
    void *dev;
    int  (*control)(void *dev, uint8_t type, uint8_t req, uint16_t value,
                    uint16_t index, void *data, uint16_t len);
    int  (*set_interface)(void *dev, uint8_t intf, uint8_t alt);
    int  (*bulk_out)(void *dev, uintptr_t phys, uint32_t len);
    int  (*rx_post)(void *dev, int idx, uintptr_t phys, uint32_t len);
    void (*poll)(void *dev);
} usbnet_glue_t;

typedef struct usbnet usbnet_t;

usbnet_t *usbnet_create(const usbnet_glue_t *g, const usb_net_iface_t *ifc,
                        uint16_t vid, uint16_t pid);
int       usbnet_start(usbnet_t *u);
void      usbnet_rx_done(usbnet_t *u, int idx, uint32_t actual, bool ok);
void      usbnet_stop(usbnet_t *u);

#endif

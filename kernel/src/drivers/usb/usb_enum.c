#include "../../../include/drivers/usb/usb_enum.h"
#include "../../../include/syscall/errno.h"
#include <stddef.h>

int usb_get_device_descriptor(usb_ctrl_ops_t *o, uint8_t *desc18) {
    uint8_t setup[8] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 18, 0x00 };
    return o->control(o->handle, setup, desc18, 18, true);
}

int usb_get_config(usb_ctrl_ops_t *o, uint8_t *cfg_full, uint16_t cap,
                   uint16_t *out_total, uint8_t *out_cfg_value) {
    uint8_t hdr[9] = {0};
    uint8_t setup_hdr[8] = { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 9, 0x00 };
    int r = o->control(o->handle, setup_hdr, hdr, 9, true);
    if (r < 0) return r;

    uint16_t total = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    if (total < 9) return -EINVAL;
    if (total > cap) total = cap;

    uint8_t setup_full[8] = { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00,
                              (uint8_t)(total & 0xFF), (uint8_t)(total >> 8) };
    r = o->control(o->handle, setup_full, cfg_full, total, true);
    if (r < 0) return r;

    if (out_total)      *out_total      = total;
    if (out_cfg_value)  *out_cfg_value  = hdr[5];
    return 0;
}

int usb_set_configuration(usb_ctrl_ops_t *o, uint8_t cfg_value) {
    uint8_t setup[8] = { 0x00, 0x09, cfg_value, 0x00, 0x00, 0x00, 0x00, 0x00 };
    return o->control(o->handle, setup, NULL, 0, false);
}

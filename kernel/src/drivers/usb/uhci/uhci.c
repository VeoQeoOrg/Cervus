#include "../../../../include/drivers/usb/uhci.h"
#include "../../../../include/drivers/pci.h"
#include "../../../../include/memory/dma.h"
#include "../../../../include/memory/pmm.h"
#include "../../../../include/apic/apic.h"
#include "../../../../include/io/ports.h"
#include "../../../../include/io/serial.h"
#include "../../../../include/sched/sched.h"
#include "../../../../include/syscall/errno.h"
#include <string.h>
#include <stdio.h>

static uhci_controller_t g_ctrls[UHCI_MAX_CONTROLLERS];
static int g_ctrl_count = 0;

#define UHCI_MAX_DEVS 8
static xhci_pub_dev_t g_uhci_pub_devs[UHCI_MAX_DEVS];
static int g_uhci_pub_devs_count = 0;

static uint8_t g_uhci_next_addr = 1;

int uhci_list_devs(xhci_pub_dev_t *out, int max, uint8_t bus_offset) {
    int n = 0;
    for (int i = 0; i < g_uhci_pub_devs_count && n < max; i++) {
        if (!g_uhci_pub_devs[i].present) continue;
        out[n] = g_uhci_pub_devs[i];
        out[n].ctrl_idx = (uint8_t)(bus_offset + out[n].ctrl_idx);
        n++;
    }
    return n;
}

static int uhci_halt(uhci_controller_t *c) {
    outw(c->io_base + UHCI_USBCMD, 0);
    for (int i = 0; i < 50; i++) {
        if (inw(c->io_base + UHCI_USBSTS) & UHCI_STS_HALTED) return 0;
        hpet_sleep_ms(1);
    }
    return -ETIMEDOUT;
}

static int uhci_reset(uhci_controller_t *c) {
    outw(c->io_base + UHCI_USBCMD, UHCI_CMD_HCRESET);
    for (int i = 0; i < 100; i++) {
        uint16_t cmd = inw(c->io_base + UHCI_USBCMD);
        if (!(cmd & UHCI_CMD_HCRESET)) return 0;
        hpet_sleep_ms(1);
    }
    return -ETIMEDOUT;
}

static void uhci_disable_legacy_smi(pci_device_t *pd) {
    pci_config_write16(pd->segment, pd->bus, pd->device, pd->function, 0xC0, 0x8F00);
}

static int uhci_detect_ports(uhci_controller_t *c) {
    int n = 0;
    for (int p = 0; p < 8; p++) {
        uint16_t v = inw(c->io_base + UHCI_PORTSC(p));
        if (v == 0xFFFF) break;
        if (!(v & 0x80)) break;
        n++;
    }
    return n ? n : 2;
}

static int uhci_alloc_schedules(uhci_controller_t *c) {
    uintptr_t phys;
    void *page = dma_alloc_coherent_low(4096, &phys);
    if (!page) return -ENOMEM;
    if (phys >= 0xFFFFFFFFULL) return -EIO;
    c->frame_list      = (uint32_t *)page;
    c->frame_list_phys = phys;

    uintptr_t qh_phys;
    uhci_qh_t *qh = (uhci_qh_t *)dma_alloc_coherent_low(4096, &qh_phys);
    if (!qh || qh_phys >= 0xFFFFFFFFULL) return -ENOMEM;
    qh->qhlp = LP_T;
    qh->qelp = LP_T;
    qh->sw[0] = qh->sw[1] = 0;
    c->ctrl_qh      = qh;
    c->ctrl_qh_phys = qh_phys;

    uint32_t flp = ((uint32_t)qh_phys) | LP_Q;
    for (int i = 0; i < 1024; i++) c->frame_list[i] = flp;
    return 0;
}

static int uhci_start(uhci_controller_t *c) {
    outl(c->io_base + UHCI_FRBASEADD, (uint32_t)c->frame_list_phys);
    outw(c->io_base + UHCI_FRNUM, 0);
    outb(c->io_base + UHCI_SOFMOD, 64);
    outw(c->io_base + UHCI_USBINTR, 0);
    outw(c->io_base + UHCI_USBSTS, 0xFFFF);

    outw(c->io_base + UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);

    for (int i = 0; i < 100; i++) {
        if (!(inw(c->io_base + UHCI_USBSTS) & UHCI_STS_HALTED)) {
            c->running = true;
            return 0;
        }
        hpet_sleep_ms(1);
    }
    return -ETIMEDOUT;
}

uhci_td_t *uhci_alloc_td(uintptr_t *phys) {
    void *p = dma_alloc_coherent_low(4096, phys);
    if (!p || *phys >= 0xFFFFFFFFULL) return NULL;
    memset(p, 0, sizeof(uhci_td_t));
    return (uhci_td_t *)p;
}

void uhci_free_td(uhci_td_t *t) {
    if (t) dma_free_coherent(t, 4096);
}

static int uhci_port_reset(uhci_controller_t *c, uint8_t port, bool *out_low_speed) {
    uint16_t pc = inw(c->io_base + UHCI_PORTSC(port));
    if (!(pc & UHCI_PSC_CONNECT)) return -ENODEV;

    outw(c->io_base + UHCI_PORTSC(port), (uint16_t)(pc | UHCI_PSC_RESET));
    hpet_sleep_ms(55);
    pc = inw(c->io_base + UHCI_PORTSC(port));
    outw(c->io_base + UHCI_PORTSC(port), (uint16_t)(pc & ~UHCI_PSC_RESET));
    hpet_sleep_ms(10);

    for (int i = 0; i < 10; i++) {
        pc = inw(c->io_base + UHCI_PORTSC(port));
        outw(c->io_base + UHCI_PORTSC(port),
             (uint16_t)(pc | UHCI_PSC_ENABLE | UHCI_PSC_CONN_CHG | UHCI_PSC_ENABLE_CHG));
        hpet_sleep_ms(2);
        pc = inw(c->io_base + UHCI_PORTSC(port));
        if (pc & UHCI_PSC_ENABLE) break;
    }

    if (!(pc & UHCI_PSC_ENABLE)) {
        serial_printf("[uhci] port %u: enable failed PORTSC=0x%04x\n", port + 1, pc);
        return -EIO;
    }
    if (out_low_speed) *out_low_speed = (pc & UHCI_PSC_LOWSPEED) != 0;
    return 0;
}

static int uhci_wait_td(uhci_td_t *last, uint32_t timeout_ms) {
    uint64_t deadline = hpet_elapsed_ns() + (uint64_t)timeout_ms * 1000000ULL;
    while (1) {
        uint32_t st = last->status;
        if (!(st & TD_STATUS_ACTIVE)) {
            if (st & (TD_STATUS_STALLED | TD_STATUS_BABBLE | TD_STATUS_DBE |
                      TD_STATUS_CRC | TD_STATUS_BITSTUF)) {
                return -EIO;
            }
            return 0;
        }
        if (hpet_elapsed_ns() > deadline) return -ETIMEDOUT;
        asm volatile("pause");
    }
}

int uhci_control_xfer(uhci_controller_t *c, uint8_t addr, bool low_speed,
                      uint16_t ep0_mps,
                      const uint8_t setup[8], void *data, uint16_t len,
                      bool data_in)
{
    if (ep0_mps == 0) ep0_mps = low_speed ? 8 : 64;
    uint32_t ls_bit = low_speed ? TD_LS_DEV : 0;

    uintptr_t setup_phys;
    void *setup_buf = dma_alloc_coherent_low(64, &setup_phys);
    if (!setup_buf || setup_phys >= 0xFFFFFFFFULL) return -ENOMEM;
    memcpy(setup_buf, setup, 8);

    uintptr_t data_phys = 0;
    void *data_buf = NULL;
    if (len > 0) {
        data_buf = dma_alloc_coherent_low(len < 64 ? 64 : len, &data_phys);
        if (!data_buf || data_phys >= 0xFFFFFFFFULL) {
            dma_free_coherent(setup_buf, 64);
            return -ENOMEM;
        }
        if (!data_in) memcpy(data_buf, data, len);
        else memset(data_buf, 0, len);
    }

    int n_data_tds = 0;
    if (len > 0) n_data_tds = (len + ep0_mps - 1) / ep0_mps;
    uintptr_t data_td_phys[16] = {0};
    uhci_td_t *data_td[16] = {NULL};
    if (n_data_tds > 16) {
        serial_writestring("[uhci] control xfer too long\n");
        dma_free_coherent(setup_buf, 64);
        if (data_buf) dma_free_coherent(data_buf, len < 64 ? 64 : len);
        return -EINVAL;
    }

    uintptr_t setup_td_phys;
    uhci_td_t *setup_td = uhci_alloc_td(&setup_td_phys);
    if (!setup_td) goto fail_alloc;
    for (int i = 0; i < n_data_tds; i++) {
        data_td[i] = uhci_alloc_td(&data_td_phys[i]);
        if (!data_td[i]) goto fail_alloc;
    }

    uintptr_t status_td_phys;
    uhci_td_t *status_td = uhci_alloc_td(&status_td_phys);
    if (!status_td) goto fail_alloc;

    setup_td->link   = n_data_tds > 0 ? (data_td_phys[0] | LP_VF) : (status_td_phys | LP_VF);
    setup_td->status = TD_STATUS_ACTIVE | TD_CERR(3) | ls_bit;
    setup_td->token  = TOKEN(8, 0, 0, addr, UHCI_PID_SETUP);
    setup_td->buffer = (uint32_t)setup_phys;

    uint8_t dt = 1;
    uint16_t remaining = len;
    uintptr_t cur_phys = data_phys;
    for (int i = 0; i < n_data_tds; i++) {
        uint16_t chunk = (remaining > ep0_mps) ? ep0_mps : remaining;
        bool last = (i == n_data_tds - 1);
        data_td[i]->link   = last ? (status_td_phys | LP_VF)
                                  : (data_td_phys[i + 1] | LP_VF);
        data_td[i]->status = TD_STATUS_ACTIVE | TD_CERR(3) | TD_SPD | ls_bit;
        data_td[i]->token  = TOKEN(chunk, dt, 0, addr, data_in ? UHCI_PID_IN : UHCI_PID_OUT);
        data_td[i]->buffer = (uint32_t)cur_phys;
        dt ^= 1;
        cur_phys += chunk;
        remaining -= chunk;
    }

    status_td->link   = LP_T;
    status_td->status = TD_STATUS_ACTIVE | TD_CERR(3) | TD_IOC | ls_bit;
    status_td->token  = TOKEN_NODATA(1, 0, addr,
                                     (len > 0 && data_in) ? UHCI_PID_OUT : UHCI_PID_IN);
    status_td->buffer = 0;

    asm volatile("" ::: "memory");
    c->ctrl_qh->qelp = (uint32_t)setup_td_phys;
    asm volatile("" ::: "memory");

    int r = uhci_wait_td(status_td, 2000);
    if (r == 0) {
        for (int i = 0; i < n_data_tds; i++) {
            if (data_td[i]->status & TD_STATUS_ACTIVE) { r = -EIO; break; }
            if (data_td[i]->status & (TD_STATUS_STALLED | TD_STATUS_BABBLE |
                                       TD_STATUS_DBE | TD_STATUS_CRC |
                                       TD_STATUS_BITSTUF)) {
                r = -EIO; break;
            }
        }
    }

    c->ctrl_qh->qelp = LP_T;
    asm volatile("" ::: "memory");
    hpet_sleep_ms(1);

    if (r == 0 && data_in && len > 0) memcpy(data, data_buf, len);

    uhci_free_td(setup_td);
    for (int i = 0; i < n_data_tds; i++) uhci_free_td(data_td[i]);
    uhci_free_td(status_td);
    dma_free_coherent(setup_buf, 64);
    if (data_buf) dma_free_coherent(data_buf, len < 64 ? 64 : len);
    return r;

fail_alloc:
    if (setup_td) uhci_free_td(setup_td);
    for (int i = 0; i < n_data_tds; i++) if (data_td[i]) uhci_free_td(data_td[i]);
    dma_free_coherent(setup_buf, 64);
    if (data_buf) dma_free_coherent(data_buf, len < 64 ? 64 : len);
    return -ENOMEM;
}

static const char *uhci_xfer_type_name(uint8_t attr) {
    switch (attr & 0x3) {
        case 0: return "Control";
        case 1: return "Isoch";
        case 2: return "Bulk";
        case 3: return "Interrupt";
        default: return "?";
    }
}

static const char *uhci_class_name(uint8_t c) {
    switch (c) {
        case 0x00: return "(per-interface)";
        case 0x01: return "Audio";
        case 0x03: return "HID";
        case 0x08: return "Mass Storage";
        case 0x09: return "Hub";
        case 0xFF: return "Vendor";
        default:   return "?";
    }
}

static void uhci_parse_config(const uint8_t *buf, uint16_t total,
                              uhci_hid_info_t *hid_out,
                              uhci_msc_info_t *msc_out,
                              uhci_hub_info_t *hub_out,
                              xhci_pub_dev_t *pd)
{
    if (hid_out) memset(hid_out, 0, sizeof(*hid_out));
    if (msc_out) memset(msc_out, 0, sizeof(*msc_out));
    if (hub_out) memset(hub_out, 0, sizeof(*hub_out));
    if (total < 9) return;
    uint8_t n_intf = buf[4];
    uint8_t cfg_val = buf[5];
    serial_printf("[uhci]   config #%u: %u interface(s)  attrs=0x%02x\n",
                  cfg_val, n_intf, buf[7]);

    bool in_kbd = false;
    bool in_msc = false;
    bool in_hub = false;
    uint8_t cur_intf = 0;
    bool first_intf = true;

    uint16_t pos = 9;
    while (pos + 2 <= total) {
        uint8_t blen  = buf[pos];
        uint8_t btype = buf[pos + 1];
        if (blen < 2 || pos + blen > total) break;
        if (btype == 0x04 && blen >= 9) {
            uint8_t inum  = buf[pos + 2];
            uint8_t alt   = buf[pos + 3];
            uint8_t neps  = buf[pos + 4];
            uint8_t cls   = buf[pos + 5];
            uint8_t sub   = buf[pos + 6];
            uint8_t proto = buf[pos + 7];
            serial_printf("[uhci]     intf %u.%u: class=0x%02x (%s) "
                          "sub=0x%02x proto=0x%02x  eps=%u\n",
                          inum, alt, cls, uhci_class_name(cls), sub, proto, neps);
            cur_intf = inum;
            in_kbd = (cls == 0x03 && sub == 0x01 && proto == 0x01 && alt == 0);
            in_msc = (cls == 0x08 && sub == 0x06 && proto == 0x50 && alt == 0);
            in_hub = (cls == 0x09 && alt == 0);
            if (hid_out && in_kbd && !hid_out->is_kbd) {
                hid_out->is_kbd = true;
                hid_out->intf   = inum;
            }
            if (msc_out && in_msc && !msc_out->is_msc) {
                msc_out->is_msc = true;
                msc_out->intf   = inum;
            }
            if (hub_out && in_hub && !hub_out->is_hub) {
                hub_out->is_hub = true;
                hub_out->intf   = inum;
            }
            if (pd && first_intf && alt == 0) {
                pd->intf_class = cls;
                pd->intf_sub   = sub;
                pd->intf_proto = proto;
                first_intf = false;
            }
        } else if (btype == 0x05 && blen >= 7) {
            uint8_t addr = buf[pos + 2];
            uint8_t attr = buf[pos + 3];
            uint16_t mps = (uint16_t)buf[pos + 4] | ((uint16_t)buf[pos + 5] << 8);
            uint8_t  ivl = buf[pos + 6];
            uint8_t  num = addr & 0x0F;
            bool is_in = (addr & 0x80) != 0;
            uint8_t type = attr & 0x3;
            serial_printf("[uhci]       ep %u %s  type=%s  mps=%u  interval=%u\n",
                          num, is_in ? "IN" : "OUT",
                          uhci_xfer_type_name(attr), mps & 0x7FF, ivl);
            if (hid_out && in_kbd && type == 3 && is_in &&
                hid_out->in_ep == 0 && hid_out->intf == cur_intf) {
                hid_out->in_ep    = num;
                hid_out->in_mps   = mps & 0x7FF;
                hid_out->interval = ivl;
            }
            if (msc_out && in_msc && type == 2 && msc_out->intf == cur_intf) {
                if (is_in && msc_out->in_ep == 0) {
                    msc_out->in_ep  = num;
                    msc_out->in_mps = mps & 0x7FF;
                } else if (!is_in && msc_out->out_ep == 0) {
                    msc_out->out_ep  = num;
                    msc_out->out_mps = mps & 0x7FF;
                }
            }
            if (hub_out && in_hub && type == 3 && is_in &&
                hub_out->in_ep == 0 && hub_out->intf == cur_intf) {
                hub_out->in_ep    = num;
                hub_out->in_mps   = mps & 0x7FF;
                hub_out->interval = ivl;
            }
        }
        pos += blen;
    }
}

static void uhci_enumerate_port(uhci_controller_t *c, uint8_t port) {
    bool low_speed = false;
    if (uhci_port_reset(c, port, &low_speed) < 0) return;

    serial_printf("[uhci] port %u: %s device\n",
                  port + 1, low_speed ? "LOW-speed" : "FULL-speed");

    uint16_t ep0_mps = low_speed ? 8 : 64;

    uint8_t pkt[8] = {0};
    uint8_t setup_dev8[8] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 8, 0x00 };
    int r = uhci_control_xfer(c, 0, low_speed, ep0_mps, setup_dev8, pkt, 8, true);
    if (r < 0) {
        serial_printf("[uhci] port %u: GET_DESCRIPTOR(DEV, 8) failed: %d\n",
                      port + 1, r);
        return;
    }
    uint8_t real_ep0_mps = pkt[7] ? pkt[7] : ep0_mps;

    uint8_t new_addr = g_uhci_next_addr++;
    uint8_t setup_addr[8] = { 0x00, 0x05, new_addr, 0x00, 0x00, 0x00, 0x00, 0x00 };
    r = uhci_control_xfer(c, 0, low_speed, real_ep0_mps, setup_addr, NULL, 0, false);
    if (r < 0) {
        serial_printf("[uhci] port %u: SET_ADDRESS failed: %d\n", port + 1, r);
        return;
    }
    hpet_sleep_ms(2);

    uint8_t desc[18] = {0};
    uint8_t setup_dev[8] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 18, 0x00 };
    r = uhci_control_xfer(c, new_addr, low_speed, real_ep0_mps,
                          setup_dev, desc, 18, true);
    if (r < 0) {
        serial_printf("[uhci] addr %u: GET_DESCRIPTOR(DEV) failed: %d\n",
                      new_addr, r);
        return;
    }
    uint16_t vid = (uint16_t)desc[8]  | ((uint16_t)desc[9]  << 8);
    uint16_t pid = (uint16_t)desc[10] | ((uint16_t)desc[11] << 8);
    uint16_t bcd = (uint16_t)desc[2]  | ((uint16_t)desc[3]  << 8);
    uint16_t bcd_dev = (uint16_t)desc[12] | ((uint16_t)desc[13] << 8);
    uint8_t  n_cfg = desc[17];
    serial_printf("[uhci] port %u addr %u: USB %u.%u  VID=0x%04x PID=0x%04x  "
                  "class=0x%02x (%s)  EP0_MPS=%u  nCfg=%u\n",
                  port + 1, new_addr,
                  (bcd >> 8) & 0xFF, (bcd >> 4) & 0xF,
                  vid, pid, desc[4], uhci_class_name(desc[4]),
                  real_ep0_mps, n_cfg);

    xhci_pub_dev_t *pd = NULL;
    if (g_uhci_pub_devs_count < UHCI_MAX_DEVS) {
        pd = &g_uhci_pub_devs[g_uhci_pub_devs_count++];
        memset(pd, 0, sizeof(*pd));
        pd->ctrl_idx   = 0;
        pd->slot_id    = new_addr;
        pd->port_id    = (uint8_t)(port + 1);
        pd->speed      = low_speed ? 2 : 1;
        pd->vid        = vid;
        pd->pid        = pid;
        pd->bcd_dev    = bcd_dev;
        pd->bcd_usb    = bcd;
        pd->dev_class  = desc[4];
        pd->dev_sub    = desc[5];
        pd->dev_proto  = desc[6];
        pd->ep0_mps    = real_ep0_mps;
        pd->n_configs  = n_cfg;
        pd->present    = 1;
    }

    if (n_cfg == 0) return;

    uint8_t cfg_hdr[9] = {0};
    uint8_t setup_chdr[8] = { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 9, 0x00 };
    r = uhci_control_xfer(c, new_addr, low_speed, real_ep0_mps,
                          setup_chdr, cfg_hdr, 9, true);
    if (r < 0) {
        serial_printf("[uhci]   GET_DESCRIPTOR(cfg hdr) failed: %d\n", r);
        return;
    }
    uint16_t total = (uint16_t)cfg_hdr[2] | ((uint16_t)cfg_hdr[3] << 8);
    uint8_t cfg_value = cfg_hdr[5];
    if (total < 9 || total > 512) {
        serial_printf("[uhci]   bogus wTotalLength=%u\n", total);
        return;
    }

    uint8_t cfg_full[512];
    uint8_t setup_cfull[8] = { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00,
                               (uint8_t)(total & 0xFF), (uint8_t)(total >> 8) };
    r = uhci_control_xfer(c, new_addr, low_speed, real_ep0_mps,
                          setup_cfull, cfg_full, total, true);
    if (r < 0) {
        serial_printf("[uhci]   GET_DESCRIPTOR(cfg full) failed: %d\n", r);
        return;
    }
    uhci_hid_info_t hid_info;
    uhci_msc_info_t msc_info;
    uhci_hub_info_t hub_info;
    uhci_parse_config(cfg_full, total, &hid_info, &msc_info, &hub_info, pd);

    uint8_t setup_setcfg[8] = { 0x00, 0x09, cfg_value, 0x00, 0x00, 0x00, 0x00, 0x00 };
    r = uhci_control_xfer(c, new_addr, low_speed, real_ep0_mps,
                          setup_setcfg, NULL, 0, false);
    if (r < 0) {
        serial_printf("[uhci]   SET_CONFIGURATION %u failed: %d\n", cfg_value, r);
        return;
    }
    serial_printf("[uhci]   SET_CONFIGURATION %u: ok\n", cfg_value);

    if (msc_info.is_msc && msc_info.in_ep && msc_info.out_ep) {
        if (uhci_msc_setup(c, new_addr, low_speed, &msc_info) < 0)
            serial_writestring("[uhci]   MSC setup failed\n");
    } else if (hub_info.is_hub && hub_info.in_ep) {
        if (uhci_hub_setup(c, new_addr, low_speed, &hub_info) < 0)
            serial_writestring("[uhci]   hub setup failed\n");
    } else if (hid_info.is_kbd && hid_info.in_ep) {
        if (uhci_hid_kbd_setup(c, new_addr, low_speed, &hid_info) < 0)
            serial_writestring("[uhci]   HID kbd setup failed\n");
    }
}

static void uhci_enumerate(uhci_controller_t *c) {
    int found = 0;
    for (uint8_t p = 0; p < c->n_ports; p++) {
        uint16_t pc = inw(c->io_base + UHCI_PORTSC(p));
        if (!(pc & UHCI_PSC_CONNECT)) continue;
        uhci_enumerate_port(c, p);
        found++;
    }
    if (!found) serial_writestring("[uhci] no devices connected\n");
}

static int uhci_find_root_dev(uint8_t port) {
    for (int i = 0; i < g_uhci_pub_devs_count; i++) {
        if (g_uhci_pub_devs[i].present && g_uhci_pub_devs[i].port_id == port)
            return i;
    }
    return -1;
}

static void uhci_disconnect_port(uint8_t port) {
    for (int i = 0; i < g_uhci_pub_devs_count; i++) {
        xhci_pub_dev_t *u = &g_uhci_pub_devs[i];
        if (!u->present) continue;
        if (u->port_id != port) continue;
        uint8_t addr = u->slot_id;
        u->present = 0;
        serial_printf("[uhci-hp] addr %u disconnected (vid:pid=%04x:%04x)\n",
                      addr, u->vid, u->pid);
        uhci_hid_kbd_deactivate_addr(addr);
        uhci_msc_deactivate_addr(addr);
        uhci_hub_deactivate_addr(addr);
    }
}

static uint8_t g_uhci_port_state[UHCI_MAX_CONTROLLERS][8];

static void uhci_handle_root_port(uhci_controller_t *c, uint8_t port) {
    int cidx = (int)(c - g_ctrls);
    if (cidx < 0 || cidx >= UHCI_MAX_CONTROLLERS || port >= 8) return;

    uint16_t pc = inw(c->io_base + UHCI_PORTSC(port));

    if (pc & (UHCI_PSC_CONN_CHG | UHCI_PSC_ENABLE_CHG)) {
        uint16_t v = (pc & (UHCI_PSC_ENABLE | UHCI_PSC_SUSPEND))
                   | UHCI_PSC_CONN_CHG | UHCI_PSC_ENABLE_CHG;
        outw(c->io_base + UHCI_PORTSC(port), v);
    }

    bool connected = (pc & UHCI_PSC_CONNECT) != 0;
    bool was = g_uhci_port_state[cidx][port] != 0;
    if (connected == was) return;
    g_uhci_port_state[cidx][port] = connected ? 1 : 0;

    int existing = uhci_find_root_dev((uint8_t)(port + 1));

    if (connected && existing < 0) {
        serial_printf("[uhci-hp] port %u: connect\n", port + 1);
        uhci_enumerate_port(c, port);
    } else if (!connected && existing >= 0) {
        serial_printf("[uhci-hp] port %u: disconnect\n", port + 1);
        uhci_disconnect_port((uint8_t)(port + 1));
    }
}

void uhci_worker(void *arg) {
    (void)arg;
    serial_writestring("[uhci-worker] started\n");
    uint32_t slow = 0;
    while (1) {
        uhci_hid_kbd_tick();
        uhci_hub_tick();

        if ((slow++ % 31) == 0) {
            int ncc = uhci_controller_count();
            for (int i = 0; i < ncc; i++) {
                uhci_controller_t *cc = uhci_get_controller(i);
                if (!cc || !cc->ready) continue;
                for (uint8_t p = 0; p < cc->n_ports; p++)
                    uhci_handle_root_port(cc, p);
            }
        }
        task_sleep_ms(16);
    }
}

static int uhci_probe(pci_device_t *pd) {
    if (g_ctrl_count >= UHCI_MAX_CONTROLLERS) return 0;
    if (pd->class_code != 0x0C || pd->subclass != 0x03 || pd->prog_if != 0x00) return 0;

    pci_bar_t *b4 = &pd->bars[4];
    if (b4->type != PCI_BAR_TYPE_IO || b4->base == 0) {
        serial_writestring("[uhci] BAR4 not I/O\n");
        return -EINVAL;
    }

    uint16_t cmd = pci_config_read16(pd->segment, pd->bus, pd->device, pd->function,
                                     PCI_COMMAND);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DIS;
    pci_config_write16(pd->segment, pd->bus, pd->device, pd->function, PCI_COMMAND, cmd);

    uhci_disable_legacy_smi(pd);

    uhci_controller_t *c = &g_ctrls[g_ctrl_count];
    memset(c, 0, sizeof(*c));
    c->pdev    = pd;
    c->io_base = (uint16_t)b4->base;

    serial_printf("[uhci] PCI %02x:%02x.%u  BAR4=0x%04x\n",
                  pd->bus, pd->device, pd->function, c->io_base);

    if (uhci_halt(c) < 0) serial_writestring("[uhci] halt timeout (continuing)\n");
    if (uhci_reset(c) < 0) {
        serial_writestring("[uhci] reset timeout\n");
        return -EIO;
    }

    c->n_ports = uhci_detect_ports(c);
    serial_printf("[uhci] detected %u port(s)\n", c->n_ports);

    if (uhci_alloc_schedules(c) < 0) {
        serial_writestring("[uhci] schedule alloc failed\n");
        return -ENOMEM;
    }
    if (uhci_start(c) < 0) {
        serial_writestring("[uhci] start failed\n");
        return -EIO;
    }
    serial_writestring("[uhci] controller running, enumerating LS/FS devices\n");

    hpet_sleep_ms(100);
    uhci_enumerate(c);

    if (g_ctrl_count < UHCI_MAX_CONTROLLERS) {
        for (uint8_t p = 0; p < c->n_ports && p < 8; p++) {
            uint16_t pc = inw(c->io_base + UHCI_PORTSC(p));
            g_uhci_port_state[g_ctrl_count][p] = (pc & UHCI_PSC_CONNECT) ? 1 : 0;
        }
    }

    c->ready = true;
    g_ctrl_count++;
    return 0;
}

static const pci_driver_t g_uhci_driver = {
    .name           = "uhci",
    .match_vendor   = -1,
    .match_device   = -1,
    .match_class    = 0x0C,
    .match_subclass = 0x03,
    .probe          = uhci_probe,
};

void uhci_init(void) {
    pci_register_driver(&g_uhci_driver);
}

void uhci_start_worker(void) {
    static bool spawned = false;
    if (spawned) return;
    if (g_ctrl_count == 0) return;
    spawned = true;
    extern void uhci_worker(void *arg);
    task_create("uhci_worker", uhci_worker, NULL, 1);
}

int uhci_controller_count(void) { return g_ctrl_count; }
uhci_controller_t *uhci_get_controller(int idx) {
    if (idx < 0 || idx >= g_ctrl_count) return NULL;
    return &g_ctrls[idx];
}

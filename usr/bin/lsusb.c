#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cervus.h>
#include <cervus_util.h>

#define MAX_USB 64

static const char *speed_name(uint8_t s) {
    switch (s) {
        case 1: return "Full-speed (12 Mb/s)";
        case 2: return "Low-speed  (1.5 Mb/s)";
        case 3: return "High-speed (480 Mb/s)";
        case 4: return "SuperSpeed (5 Gb/s)";
        case 5: return "SuperSpeed+ (10 Gb/s)";
        default: return "unknown";
    }
}

static const char *class_name(uint8_t c) {
    switch (c) {
        case 0x00: return "(per-interface)";
        case 0x01: return "Audio";
        case 0x02: return "CDC";
        case 0x03: return "HID";
        case 0x05: return "Physical";
        case 0x06: return "Image";
        case 0x07: return "Printer";
        case 0x08: return "Mass Storage";
        case 0x09: return "Hub";
        case 0x0A: return "CDC Data";
        case 0x0B: return "Smart Card";
        case 0x0D: return "Content Security";
        case 0x0E: return "Video";
        case 0x0F: return "Personal Healthcare";
        case 0x10: return "Audio/Video";
        case 0xDC: return "Diagnostic";
        case 0xE0: return "Wireless";
        case 0xEF: return "Misc";
        case 0xFE: return "Application Specific";
        case 0xFF: return "Vendor Specific";
        default:   return "?";
    }
}

static const char *hid_proto_name(uint8_t sub, uint8_t proto) {
    if (sub == 1) {
        if (proto == 1) return " (Boot Keyboard)";
        if (proto == 2) return " (Boot Mouse)";
    }
    return "";
}

static void format_path(const cervus_usb_dev_t *d, char *out, size_t n) {
    int pos = snprintf(out, n, "%u", (unsigned)d->port_id);
    for (int t = 0; t < d->depth && pos < (int)n - 4; t++) {
        uint8_t nibble = (d->route_string >> (4 * t)) & 0xF;
        if (nibble == 0) break;
        pos += snprintf(out + pos, n - pos, ".%u", (unsigned)nibble);
    }
}

static void print_one(const cervus_usb_dev_t *d, int verbose) {
    uint8_t cls = d->intf_class ? d->intf_class : d->dev_class;
    uint8_t sub = d->intf_class ? d->intf_sub   : d->dev_sub;
    uint8_t pr  = d->intf_class ? d->intf_proto : d->dev_proto;

    char path[32];
    format_path(d, path, sizeof(path));

    printf("Bus %u Path %s Slot %u  ID %04x:%04x  USB %u.%u %s  %s%s\n",
           (unsigned)d->ctrl_idx, path, (unsigned)d->slot_id,
           (unsigned)d->vid, (unsigned)d->pid,
           (d->bcd_usb >> 8) & 0xFF, (d->bcd_usb >> 4) & 0xF,
           speed_name(d->speed),
           class_name(cls), hid_proto_name(sub, pr));

    if (verbose) {
        printf("    bcdDevice         : %u.%u\n",
               (d->bcd_dev >> 8) & 0xFF, (d->bcd_dev >> 4) & 0xF);
        printf("    Device   class/sub/proto : 0x%02x / 0x%02x / 0x%02x\n",
               d->dev_class, d->dev_sub, d->dev_proto);
        printf("    Iface[0] class/sub/proto : 0x%02x / 0x%02x / 0x%02x  (%s)\n",
               d->intf_class, d->intf_sub, d->intf_proto, class_name(d->intf_class));
        printf("    bMaxPacketSize0   : %u\n", (unsigned)d->ep0_mps);
        printf("    bNumConfigurations: %u\n", (unsigned)d->n_configs);
        printf("    Topology          : root_port=%u depth=%u route=0x%05x parent_slot=%u parent_port=%u\n",
               (unsigned)d->port_id, (unsigned)d->depth,
               (unsigned)d->route_string,
               (unsigned)d->parent_slot, (unsigned)d->parent_port);
    }
}

static void usage(void) {
    printf("Usage: lsusb [-v]\n");
}

int main(int argc, char **argv) {
    int verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) verbose = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            usage();
            return 1;
        }
    }

    static cervus_usb_dev_t devs[MAX_USB];
    long n = cervus_usb_list(devs, MAX_USB);
    if (n < 0) {
        fprintf(stderr, "lsusb: cervus_usb_list failed: %ld\n", n);
        return 1;
    }
    int shown = 0;
    for (long i = 0; i < n; i++) {
        if (!devs[i].present) continue;
        print_one(&devs[i], verbose);
        shown++;
    }
    if (shown == 0) printf("No USB devices found.\n");
    return 0;
}

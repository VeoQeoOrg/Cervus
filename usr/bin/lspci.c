#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cervus.h>
#include <cervus_util.h>

#define MAX_DEVS 64

static const char *class_name(uint8_t cls)
{
    switch (cls) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass storage controller";
        case 0x02: return "Network controller";
        case 0x03: return "Display controller";
        case 0x04: return "Multimedia controller";
        case 0x05: return "Memory controller";
        case 0x06: return "Bridge";
        case 0x07: return "Communication controller";
        case 0x08: return "Generic system peripheral";
        case 0x09: return "Input device controller";
        case 0x0A: return "Docking station";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial bus controller";
        case 0x0D: return "Wireless controller";
        default:   return "Unknown device";
    }
}

static const char *subclass_name(uint8_t cls, uint8_t sub)
{
    if (cls == 0x01) {
        switch (sub) {
            case 0x01: return "IDE interface";
            case 0x06: return "SATA controller";
            case 0x08: return "NVM Express";
            default:   return NULL;
        }
    }
    if (cls == 0x02) {
        switch (sub) {
            case 0x00: return "Ethernet controller";
            case 0x80: return "Other network controller";
            default:   return NULL;
        }
    }
    if (cls == 0x03) {
        switch (sub) {
            case 0x00: return "VGA compatible controller";
            default:   return NULL;
        }
    }
    if (cls == 0x06) {
        switch (sub) {
            case 0x00: return "Host bridge";
            case 0x01: return "ISA bridge";
            case 0x04: return "PCI bridge";
            default:   return NULL;
        }
    }
    if (cls == 0x0C) {
        switch (sub) {
            case 0x03: return "USB controller";
            case 0x05: return "SMBus";
            default:   return NULL;
        }
    }
    return NULL;
}

static int parse_flags(int argc, char **argv, int *verbose)
{
    *verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) *verbose = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            fputs("usage: lspci [-v]\n", stdout);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int verbose;
    if (parse_flags(argc, argv, &verbose) < 0) return 0;

    static cervus_pci_device_t devs[MAX_DEVS];
    long n = cervus_pci_list(devs, MAX_DEVS);
    if (n < 0) {
        fputs(C_RED "lspci: syscall failed\n" C_RESET, stderr);
        return 1;
    }
    if (n == 0) {
        fputs("No PCI devices.\n", stdout);
        return 0;
    }

    for (int i = 0; i < n; i++) {
        cervus_pci_device_t *d = &devs[i];
        const char *cls = class_name(d->class_code);
        const char *sub = subclass_name(d->class_code, d->subclass);

        printf(C_BOLD "%04x:%02x:%02x.%u" C_RESET " %s",
               d->segment, d->bus, d->device, d->function, cls);
        if (sub) printf(" / %s", sub);
        printf(" [" C_CYAN "%04x:%04x" C_RESET "]",
               d->vendor_id, d->device_id);
        if (d->has_msix)     fputs(" " C_GREEN "MSI-X" C_RESET, stdout);
        else if (d->has_msi) fputs(" " C_GREEN "MSI"   C_RESET, stdout);
        putchar('\n');

        if (!verbose) continue;

        printf("    class %02x:%02x:%02x  rev %02x  hdr %02x  irq pin=%u line=%u\n",
               d->class_code, d->subclass, d->prog_if,
               d->revision, d->header_type, d->irq_pin, d->irq_line);
        for (int b = 0; b < 6; b++) {
            cervus_pci_bar_t *bar = &d->bars[b];
            if (bar->base == 0 && bar->size == 0) continue;
            if (bar->type == 0xFF) continue;
            const char *kind = bar->type == 0 ? "mem" : "io ";
            const char *wide = bar->is_64bit ? "64" : "32";
            const char *pf   = bar->prefetchable ? " prefetch" : "";
            printf("    BAR%d %s %s  base=0x%llx  size=0x%llx%s\n",
                   b, kind, wide,
                   (unsigned long long)bar->base,
                   (unsigned long long)bar->size, pf);
        }
        if (d->has_msix)
            printf("    MSI-X table entries: %u\n", d->msix_table_size);
    }
    return 0;
}

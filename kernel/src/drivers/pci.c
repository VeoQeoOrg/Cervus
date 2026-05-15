#include "../../include/drivers/pci.h"
#include "../../include/acpi/acpi.h"
#include "../../include/io/ports.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define PCI_CFG_ADDR 0xCF8
#define PCI_CFG_DATA 0xCFC

#define MCFG_MAX_SEGMENTS 16

typedef struct {
    uint64_t base;
    uint16_t segment;
    uint8_t  start_bus;
    uint8_t  end_bus;
} mcfg_seg_t;

static mcfg_seg_t g_mcfg[MCFG_MAX_SEGMENTS];
static int        g_mcfg_count = 0;

static pci_device_t g_devices[PCI_MAX_DEVICES];
static int          g_device_count = 0;

#define DRIVER_MAX 32
static const pci_driver_t *g_drivers[DRIVER_MAX];
static int                 g_driver_count = 0;

static bool g_scanned[256];

static uint32_t legacy_addr(uint8_t bus, uint8_t dev, uint8_t func, uint16_t off)
{
    return (uint32_t)0x80000000U
         | ((uint32_t)bus  << 16)
         | ((uint32_t)dev  << 11)
         | ((uint32_t)func << 8)
         | (off & 0xFCU);
}

static const mcfg_seg_t *mcfg_find(uint16_t seg, uint8_t bus)
{
    for (int i = 0; i < g_mcfg_count; i++) {
        const mcfg_seg_t *m = &g_mcfg[i];
        if (m->segment == seg && bus >= m->start_bus && bus <= m->end_bus) return m;
    }
    return NULL;
}

static void *mcfg_addr(const mcfg_seg_t *m, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off)
{
    uintptr_t phys = m->base
                   + (((uintptr_t)(bus - m->start_bus)) << 20)
                   + (((uintptr_t)dev) << 15)
                   + (((uintptr_t)func) << 12)
                   + off;
    return (void *)(phys + pmm_get_hhdm_offset());
}

uint32_t pci_config_read32(uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off)
{
    const mcfg_seg_t *m = mcfg_find(seg, bus);
    if (m) {
        volatile uint32_t *p = (volatile uint32_t *)mcfg_addr(m, bus, dev, func, off & 0xFFC);
        return *p;
    }
    if (seg != 0) return 0xFFFFFFFFu;
    outl(PCI_CFG_ADDR, legacy_addr(bus, dev, func, off));
    return inl(PCI_CFG_DATA);
}

void pci_config_write32(uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off, uint32_t v)
{
    const mcfg_seg_t *m = mcfg_find(seg, bus);
    if (m) {
        volatile uint32_t *p = (volatile uint32_t *)mcfg_addr(m, bus, dev, func, off & 0xFFC);
        *p = v;
        return;
    }
    if (seg != 0) return;
    outl(PCI_CFG_ADDR, legacy_addr(bus, dev, func, off));
    outl(PCI_CFG_DATA, v);
}

uint16_t pci_config_read16(uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off)
{
    uint32_t v = pci_config_read32(seg, bus, dev, func, off & ~3u);
    return (uint16_t)(v >> ((off & 2) * 8));
}

uint8_t pci_config_read8(uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off)
{
    uint32_t v = pci_config_read32(seg, bus, dev, func, off & ~3u);
    return (uint8_t)(v >> ((off & 3) * 8));
}

void pci_config_write16(uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off, uint16_t v)
{
    uint16_t shift = (off & 2) * 8;
    uint32_t old = pci_config_read32(seg, bus, dev, func, off & ~3u);
    uint32_t mask = 0xFFFFu << shift;
    uint32_t neu  = (old & ~mask) | (((uint32_t)v) << shift);
    pci_config_write32(seg, bus, dev, func, off & ~3u, neu);
}

void pci_config_write8(uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func, uint16_t off, uint8_t v)
{
    uint16_t shift = (off & 3) * 8;
    uint32_t old = pci_config_read32(seg, bus, dev, func, off & ~3u);
    uint32_t mask = 0xFFu << shift;
    uint32_t neu  = (old & ~mask) | (((uint32_t)v) << shift);
    pci_config_write32(seg, bus, dev, func, off & ~3u, neu);
}

static void parse_capabilities(pci_device_t *d)
{
    uint16_t status = pci_config_read16(d->segment, d->bus, d->device, d->function, PCI_STATUS);
    if (!(status & PCI_STATUS_CAPS)) return;
    uint8_t off = pci_config_read8(d->segment, d->bus, d->device, d->function, PCI_CAPABILITY_PTR) & 0xFC;
    int guard = 0;
    while (off && guard++ < 48) {
        uint8_t cap_id   = pci_config_read8(d->segment, d->bus, d->device, d->function, off);
        uint8_t next     = pci_config_read8(d->segment, d->bus, d->device, d->function, off + 1) & 0xFC;
        if (cap_id == PCI_CAP_ID_MSI)  d->cap_msi_off  = off;
        if (cap_id == PCI_CAP_ID_MSIX) {
            d->cap_msix_off = off;
            uint16_t ctrl = pci_config_read16(d->segment, d->bus, d->device, d->function, off + 2);
            d->msix_table_size = (ctrl & 0x07FF) + 1;
        }
        off = next;
    }
}

static void size_bars(pci_device_t *d)
{
    int max_bars = (d->header_type == PCI_HEADER_TYPE_BRIDGE) ? 2 : 6;
    for (int i = 0; i < max_bars; i++) {
        uint16_t off = PCI_BAR0 + i * 4;
        uint32_t orig = pci_config_read32(d->segment, d->bus, d->device, d->function, off);
        if (orig == 0) continue;

        pci_config_write32(d->segment, d->bus, d->device, d->function, off, 0xFFFFFFFFu);
        uint32_t sz_lo = pci_config_read32(d->segment, d->bus, d->device, d->function, off);
        pci_config_write32(d->segment, d->bus, d->device, d->function, off, orig);

        pci_bar_t *bar = &d->bars[i];
        if (orig & 0x1) {
            bar->type         = PCI_BAR_TYPE_IO;
            bar->is_64bit     = 0;
            bar->prefetchable = 0;
            bar->base         = orig & ~0x3u;
            uint32_t sz       = (~(sz_lo & ~0x3u)) + 1;
            bar->size         = sz;
        } else {
            uint8_t mtype     = (orig >> 1) & 0x3;
            bar->type         = PCI_BAR_TYPE_MEM;
            bar->prefetchable = (orig >> 3) & 0x1;
            bar->base         = orig & ~0xFu;
            uint32_t sz       = (~(sz_lo & ~0xFu)) + 1;
            bar->size         = sz;
            if (mtype == 0x2 && i + 1 < max_bars) {
                bar->is_64bit = 1;
                uint16_t off2 = PCI_BAR0 + (i + 1) * 4;
                uint32_t hi_orig = pci_config_read32(d->segment, d->bus, d->device, d->function, off2);
                pci_config_write32(d->segment, d->bus, d->device, d->function, off2, 0xFFFFFFFFu);
                uint32_t sz_hi = pci_config_read32(d->segment, d->bus, d->device, d->function, off2);
                pci_config_write32(d->segment, d->bus, d->device, d->function, off2, hi_orig);
                bar->base |= ((uint64_t)hi_orig) << 32;
                uint64_t fullsz = ((uint64_t)sz_hi << 32) | (sz_lo & ~0xFu);
                bar->size = (~fullsz) + 1;
                d->bars[i + 1].type = 0xFF;
                i++;
            }
        }
    }
}

static void try_drivers_for(pci_device_t *d)
{
    for (int i = 0; i < g_driver_count; i++) {
        const pci_driver_t *drv = g_drivers[i];
        bool m_vendor   = (drv->match_vendor   < 0) || (uint16_t)drv->match_vendor   == d->vendor_id;
        bool m_device   = (drv->match_device   < 0) || (uint16_t)drv->match_device   == d->device_id;
        bool m_class    = (drv->match_class    < 0) || (uint8_t) drv->match_class    == d->class_code;
        bool m_subclass = (drv->match_subclass < 0) || (uint8_t) drv->match_subclass == d->subclass;
        if (m_vendor && m_device && m_class && m_subclass && drv->probe) {
            drv->probe(d);
        }
    }
}

static void scan_bus(uint16_t seg, uint8_t bus);

static void scan_function(uint16_t seg, uint8_t bus, uint8_t dev, uint8_t func)
{
    uint32_t id = pci_config_read32(seg, bus, dev, func, 0x00);
    uint16_t vendor = (uint16_t)(id & 0xFFFF);
    if (vendor == 0xFFFF || vendor == 0x0000) return;

    if (g_device_count >= PCI_MAX_DEVICES) return;
    pci_device_t *d = &g_devices[g_device_count];
    memset(d, 0, sizeof(*d));
    d->segment   = seg;
    d->bus       = bus;
    d->device    = dev;
    d->function  = func;
    d->vendor_id = vendor;
    d->device_id = (uint16_t)(id >> 16);

    uint32_t class_word = pci_config_read32(seg, bus, dev, func, 0x08);
    d->revision   = (uint8_t)(class_word & 0xFF);
    d->prog_if    = (uint8_t)((class_word >> 8) & 0xFF);
    d->subclass   = (uint8_t)((class_word >> 16) & 0xFF);
    d->class_code = (uint8_t)((class_word >> 24) & 0xFF);

    d->header_type = pci_config_read8(seg, bus, dev, func, PCI_HEADER_TYPE);
    d->irq_line    = pci_config_read8(seg, bus, dev, func, PCI_INTERRUPT_LINE);
    d->irq_pin     = pci_config_read8(seg, bus, dev, func, PCI_INTERRUPT_PIN);

    size_bars(d);
    parse_capabilities(d);

    g_device_count++;

    serial_printf("[pci] %04x:%02x:%02x.%u %04x:%04x class=%02x:%02x:%02x hdr=%02x\n",
                  seg, bus, dev, func,
                  d->vendor_id, d->device_id,
                  d->class_code, d->subclass, d->prog_if,
                  d->header_type & 0x7F);

    if ((d->header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {
        uint8_t sec = pci_config_read8(seg, bus, dev, func, PCI_SECONDARY_BUS);
        if (sec && !g_scanned[sec]) scan_bus(seg, sec);
    }

    try_drivers_for(d);
}

static void scan_bus(uint16_t seg, uint8_t bus)
{
    if (g_scanned[bus]) return;
    g_scanned[bus] = true;
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t id = pci_config_read32(seg, bus, dev, 0, 0x00);
        if ((id & 0xFFFF) == 0xFFFF) continue;
        scan_function(seg, bus, dev, 0);
        uint8_t hdr = pci_config_read8(seg, bus, dev, 0, PCI_HEADER_TYPE);
        if (hdr & PCI_HEADER_MULTIFUNC) {
            for (uint8_t f = 1; f < 8; f++) {
                uint32_t fid = pci_config_read32(seg, bus, dev, f, 0x00);
                if ((fid & 0xFFFF) == 0xFFFF) continue;
                scan_function(seg, bus, dev, f);
            }
        }
    }
}

static void load_mcfg(void)
{
    acpi_mcfg_t *mcfg = (acpi_mcfg_t *)acpi_find_table("MCFG", 0);
    if (!mcfg) {
        serial_writestring("[pci] MCFG not present, using legacy CF8/CFC only\n");
        return;
    }
    size_t total = mcfg->header.length;
    if (total <= sizeof(acpi_mcfg_t)) return;
    size_t n = (total - sizeof(acpi_mcfg_t)) / sizeof(mcfg_entry_t);
    mcfg_entry_t *entries = (mcfg_entry_t *)((uint8_t *)mcfg + sizeof(acpi_mcfg_t));
    for (size_t i = 0; i < n && g_mcfg_count < MCFG_MAX_SEGMENTS; i++) {
        g_mcfg[g_mcfg_count].base      = entries[i].base_address;
        g_mcfg[g_mcfg_count].segment   = entries[i].pci_segment_group;
        g_mcfg[g_mcfg_count].start_bus = entries[i].start_pci_bus;
        g_mcfg[g_mcfg_count].end_bus   = entries[i].end_pci_bus;
        serial_printf("[pci] MCFG seg=%u buses=%u..%u base=0x%llx\n",
                      g_mcfg[g_mcfg_count].segment,
                      g_mcfg[g_mcfg_count].start_bus,
                      g_mcfg[g_mcfg_count].end_bus,
                      (unsigned long long)g_mcfg[g_mcfg_count].base);
        g_mcfg_count++;
    }
}

void pci_init(void)
{
    g_device_count = 0;
    g_mcfg_count   = 0;
    memset(g_scanned, 0, sizeof(g_scanned));
    load_mcfg();

    if (g_mcfg_count == 0) {
        scan_bus(0, 0);
    } else {
        for (int i = 0; i < g_mcfg_count; i++) {
            uint16_t seg = g_mcfg[i].segment;
            for (int b = g_mcfg[i].start_bus; b <= g_mcfg[i].end_bus; b++) {
                if (seg == 0 && g_scanned[b]) continue;
                scan_bus(seg, (uint8_t)b);
            }
        }
    }
    serial_printf("[pci] enumeration done: %d devices\n", g_device_count);
}

int pci_device_count(void) { return g_device_count; }

pci_device_t *pci_device_at(int i)
{
    if (i < 0 || i >= g_device_count) return NULL;
    return &g_devices[i];
}

const pci_device_t *pci_find_by_id(uint16_t vendor, uint16_t device)
{
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].vendor_id == vendor && g_devices[i].device_id == device)
            return &g_devices[i];
    }
    return NULL;
}

const pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass)
{
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].class_code == class_code && g_devices[i].subclass == subclass)
            return &g_devices[i];
    }
    return NULL;
}

void pci_register_driver(const pci_driver_t *drv)
{
    if (!drv || g_driver_count >= DRIVER_MAX) return;
    g_drivers[g_driver_count++] = drv;
    for (int i = 0; i < g_device_count; i++) {
        try_drivers_for(&g_devices[i]);
    }
}

int pci_enable_msi(pci_device_t *dev, uint8_t vector, uint32_t apic_lapic_id)
{
    if (!dev || !dev->cap_msi_off) return -1;
    uint8_t off = dev->cap_msi_off;
    uint16_t ctrl = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, off + 2);
    bool is64 = (ctrl & 0x0080) != 0;

    uint32_t addr_lo = 0xFEE00000u | ((apic_lapic_id & 0xFF) << 12);
    pci_config_write32(dev->segment, dev->bus, dev->device, dev->function, off + 4, addr_lo);
    if (is64) {
        pci_config_write32(dev->segment, dev->bus, dev->device, dev->function, off + 8, 0);
        pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, off + 12, vector);
    } else {
        pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, off + 8, vector);
    }
    ctrl = (ctrl & ~0x0070u) | 0x0001u;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, off + 2, ctrl);

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_INTX_DIS | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
    return 0;
}

int pci_enable_msix(pci_device_t *dev, uint8_t base_vector, uint32_t apic_lapic_id)
{
    if (!dev || !dev->cap_msix_off || dev->msix_table_size == 0) return -1;
    uint8_t off = dev->cap_msix_off;

    uint32_t table_off = pci_config_read32(dev->segment, dev->bus, dev->device, dev->function, off + 4);
    uint8_t bir = table_off & 0x7;
    uint32_t bar_off = table_off & ~0x7u;
    if (bir >= 6) return -1;
    pci_bar_t *bar = &dev->bars[bir];
    if (bar->type != PCI_BAR_TYPE_MEM || !bar->base) return -1;

    volatile uint32_t *table = (volatile uint32_t *)(uintptr_t)(bar->base + bar_off + pmm_get_hhdm_offset());
    uint32_t addr_lo = 0xFEE00000u | ((apic_lapic_id & 0xFF) << 12);
    for (uint16_t i = 0; i < dev->msix_table_size; i++) {
        table[i * 4 + 0] = addr_lo;
        table[i * 4 + 1] = 0;
        table[i * 4 + 2] = (uint32_t)(base_vector + i);
        table[i * 4 + 3] = 0;
    }
    uint16_t ctrl = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, off + 2);
    ctrl |= 0x8000u;
    ctrl &= ~0x4000u;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, off + 2, ctrl);

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_INTX_DIS | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
    return 0;
}

const char *pci_class_name(uint8_t class_code)
{
    switch (class_code) {
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
        case 0x0E: return "Intelligent controller";
        case 0x0F: return "Satellite communications controller";
        case 0x10: return "Encryption controller";
        case 0x11: return "Signal processing controller";
        case 0x12: return "Processing accelerator";
        case 0x13: return "Non-essential instrumentation";
        case 0x40: return "Coprocessor";
        default:   return "Unknown";
    }
}
